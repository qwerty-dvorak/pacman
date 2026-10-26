#include "game/game.h"

#include <string.h>

#define HOUSE_DOOR_X        (14*TILE_SIZE)          // Door spans tiles 13-14; its centre is a tile edge.
#define HOUSE_EXIT_Y        TileCenterPixel(11)     // Row directly above the door.
#define HOUSE_CENTER_Y      TileCenterPixel(14)
#define HOUSE_BOB_RANGE     4
#define HOUSE_SLOT_OFFSET   (2*TILE_SIZE)

#define PAC_START_X         (14*TILE_SIZE)
#define PAC_START_Y         TileCenterPixel(23)

#define EYES_SPEED_PCT      160
#define HOUSE_SPEED_PCT     50

#define EYES_TARGET_TX      13
#define EYES_TARGET_TY      11
#define FRUIT_TILE_Y        17

#define CLYDE_SHY_DIST_SQ   64                      // Clyde retreats inside 8 tiles of Pac-Man.

#define GLOBAL_RELEASE_PINKY    7
#define GLOBAL_RELEASE_INKY     17
#define GLOBAL_RELEASE_CLYDE    32

typedef struct TilePos {
    int32_t x;
    int32_t y;
} TilePos;

static const TilePos scatterTargets[GHOST_COUNT] = { { 25, -3 }, { 2, -3 }, { 27, 31 }, { 0, 31 } };
static const int32_t houseSlotX[GHOST_COUNT] = {
    HOUSE_DOOR_X, HOUSE_DOOR_X, HOUSE_DOOR_X - HOUSE_SLOT_OFFSET, HOUSE_DOOR_X + HOUSE_SLOT_OFFSET
};
static const Dir houseStartDir[GHOST_COUNT] = { DIR_LEFT, DIR_DOWN, DIR_UP, DIR_UP };

// Ghosts break distance ties in this order.
static const Dir decisionOrder[4] = { DIR_UP, DIR_LEFT, DIR_DOWN, DIR_RIGHT };

// Tiles where roaming ghosts may not turn upwards: (12,11) (15,11) (12,23) (15,23).
#define NO_UP_MASK          ((1u << 12) | (1u << 15))
FORCE_INLINE bool IsNoUpTile(int tx, int ty) { return ((ty == 11) || (ty == 23)) && (((NO_UP_MASK >> tx) & 1u) != 0); }

FORCE_INLINE uint32_t NextRandom(Game *game)
{
    uint32_t x = game->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    game->rng = x;
    return x;
}

FORCE_INLINE int Sign(int v) { return (v > 0) - (v < 0); }

//----------------------------------------------------------------------------------
// Setup
//----------------------------------------------------------------------------------
static void EnterPhase(Game *game, GamePhase phase)
{
    game->phase = phase;
    game->phaseTicks = 0;
}

static void ActorPlace(Actor *actor, int32_t x, int32_t y, Dir dir)
{
    actor->x = x;
    actor->y = y;
    actor->prevX = x;
    actor->prevY = y;
    actor->subpixel = 0;
    actor->dir = dir;
}

static void ResetActors(Game *game)
{
    Pacman *pac = &game->pac;
    ActorPlace(&pac->actor, PAC_START_X, PAC_START_Y, DIR_LEFT);
    pac->wish = DIR_NONE;
    pac->pauseTicks = 0;
    pac->animDistance = 0;

    for (int i = 0; i < GHOST_COUNT; i++)
    {
        Ghost *ghost = &game->ghosts[i];
        const bool outside = (i == GHOST_BLINKY);
        ActorPlace(&ghost->actor, houseSlotX[i], outside? HOUSE_EXIT_Y : HOUSE_CENTER_Y, houseStartDir[i]);
        ghost->state = outside? GHOST_ACTIVE : GHOST_IN_HOUSE;
        ghost->frightened = false;
        ghost->reversePending = false;
        ghost->homeX = houseSlotX[i];
    }

    game->modeIndex = 0;
    game->modeTicksLeft = game->spec->modeTicks[0];
    game->chase = false;
    game->frightTicksLeft = 0;
    game->ghostsEatenInFright = 0;
    game->ticksSinceDot = 0;
    game->fruitTicksLeft = 0;
    game->fruitScoreTicksLeft = 0;
}

static void StartLevel(Game *game)
{
    game->spec = LevelGetSpec(game->level);
    game->mazeIndex = LevelPackMazeIndex(game->pack, game->level);
    game->maze = game->pack->mazes[game->mazeIndex].maze;
    game->dotsEaten = 0;
    game->globalDotCounterActive = false;
    game->globalDotCounter = 0;
    game->elroySuspended = false;
    for (int i = 0; i < GHOST_COUNT; i++) game->ghosts[i].dotCounter = 0;

    ResetActors(game);
    EnterPhase(game, PHASE_READY);
    game->events |= GAME_EVENT_LEVEL_START;
}

void GameNew(Game *game, const GameConfig *config)
{
    memset(game, 0, sizeof(*game));
    game->pack = config->pack;
    game->rng = (config->seed != 0)? config->seed : 0x9E3779B9u;
    game->level = MAX(config->startLevel, 1);
    game->lives = MAX(config->lives, 1);
    game->highScore = config->highScore;
    game->firstReady = true;
    StartLevel(game);
}

//----------------------------------------------------------------------------------
// Scoring and modes
//----------------------------------------------------------------------------------
static void AddScore(Game *game, uint32_t points)
{
    game->score += points;
    if (!game->extraLifeAwarded && (game->score >= EXTRA_LIFE_SCORE))
    {
        game->extraLifeAwarded = true;
        game->lives++;
        game->events |= GAME_EVENT_EXTRA_LIFE;
    }
    if (game->score > game->highScore) game->highScore = game->score;
}

static void ReverseRoamingGhosts(Game *game)
{
    for (int i = 0; i < GHOST_COUNT; i++)
    {
        if (game->ghosts[i].state == GHOST_ACTIVE) game->ghosts[i].reversePending = true;
    }
}

static void StartFright(Game *game)
{
    ReverseRoamingGhosts(game);
    if (game->spec->frightTicks == 0) return;

    game->frightTicksLeft = game->spec->frightTicks;
    game->ghostsEatenInFright = 0;
    for (int i = 0; i < GHOST_COUNT; i++)
    {
        Ghost *ghost = &game->ghosts[i];
        if ((ghost->state != GHOST_EYES) && (ghost->state != GHOST_ENTERING)) ghost->frightened = true;
    }
}

static void UpdateModeTimers(Game *game)
{
    if (game->frightTicksLeft > 0)
    {
        // The scatter/chase clock is paused while ghosts are frightened.
        if (--game->frightTicksLeft == 0)
        {
            for (int i = 0; i < GHOST_COUNT; i++) game->ghosts[i].frightened = false;
        }
        return;
    }

    if (game->modeIndex >= MODE_PHASES) return;
    if (--game->modeTicksLeft != 0) return;

    game->modeIndex++;
    game->chase = !game->chase;
    game->modeTicksLeft = (game->modeIndex < MODE_PHASES)? game->spec->modeTicks[game->modeIndex] : 0;
    ReverseRoamingGhosts(game);
}

bool GameFrightFlashWhite(const Game *game)
{
    const uint32_t flashTicks = (uint32_t)game->spec->frightFlashes*2u*FRIGHT_FLASH_TICKS;
    if (game->frightTicksLeft >= flashTicks) return false;
    return ((game->frightTicksLeft/FRIGHT_FLASH_TICKS) & 1u) != 0;
}

//----------------------------------------------------------------------------------
// Ghost house
//----------------------------------------------------------------------------------
static int PreferredHouseGhost(const Game *game)
{
    for (int i = GHOST_PINKY; i < GHOST_COUNT; i++)
    {
        if (game->ghosts[i].state == GHOST_IN_HOUSE) return i;
    }
    return -1;
}

static void ReleaseGhost(Game *game, int id)
{
    if (game->ghosts[id].state == GHOST_IN_HOUSE) game->ghosts[id].state = GHOST_LEAVING;
}

static void CountDotForHouse(Game *game)
{
    if (game->globalDotCounterActive)
    {
        // After a death a shared counter releases the ghosts at fixed thresholds.
        game->globalDotCounter++;
        switch (game->globalDotCounter)
        {
            case GLOBAL_RELEASE_PINKY: ReleaseGhost(game, GHOST_PINKY); break;
            case GLOBAL_RELEASE_INKY: ReleaseGhost(game, GHOST_INKY); break;
            case GLOBAL_RELEASE_CLYDE:
            {
                if (game->ghosts[GHOST_CLYDE].state == GHOST_IN_HOUSE)
                {
                    ReleaseGhost(game, GHOST_CLYDE);
                    game->globalDotCounterActive = false;
                }
            } break;
            default: break;
        }
        return;
    }

    const int preferred = PreferredHouseGhost(game);
    if (preferred >= 0) game->ghosts[preferred].dotCounter++;
}

static void UpdateHouse(Game *game)
{
    if (game->elroySuspended && (game->ghosts[GHOST_CLYDE].state != GHOST_IN_HOUSE)) game->elroySuspended = false;

    const int preferred = PreferredHouseGhost(game);
    if (preferred < 0) return;

    if (!game->globalDotCounterActive && (game->ghosts[preferred].dotCounter >= game->spec->houseDotLimit[preferred]))
    {
        ReleaseGhost(game, preferred);
    }
    else if (game->ticksSinceDot >= game->spec->houseTimeoutTicks)
    {
        // Pac-Man stopped eating: force the next ghost out.
        ReleaseGhost(game, preferred);
        game->ticksSinceDot = 0;
    }
}

//----------------------------------------------------------------------------------
// Ghost movement
//----------------------------------------------------------------------------------
static int ElroyLevel(const Game *game)
{
    if (game->elroySuspended) return 0;
    const int32_t left = game->maze.pelletsLeft;
    if (left <= game->spec->elroy2Dots) return 2;
    if (left <= game->spec->elroy1Dots) return 1;
    return 0;
}

static TilePos GhostTarget(const Game *game, int id)
{
    const Actor *pac = &game->pac.actor;
    const TilePos pacTile = { TileOf(pac->x), TileOf(pac->y) };

    const bool elroyChases = (id == GHOST_BLINKY) && (ElroyLevel(game) > 0);
    if (!game->chase && !elroyChases) return scatterTargets[id];

    const int dx = dirDx[pac->dir];
    const int dy = dirDy[pac->dir];
    const int upBug = (pac->dir == DIR_UP)? 1 : 0;    // Arcade overflow: "up" also shifts left.

    switch (id)
    {
        case GHOST_PINKY: return (TilePos){ pacTile.x + 4*dx - 4*upBug, pacTile.y + 4*dy };
        case GHOST_INKY:
        {
            const Actor *blinky = &game->ghosts[GHOST_BLINKY].actor;
            const TilePos pivot = { pacTile.x + 2*dx - 2*upBug, pacTile.y + 2*dy };
            return (TilePos){ 2*pivot.x - TileOf(blinky->x), 2*pivot.y - TileOf(blinky->y) };
        }
        case GHOST_CLYDE:
        {
            const Actor *clyde = &game->ghosts[GHOST_CLYDE].actor;
            const int ex = TileOf(clyde->x) - pacTile.x;
            const int ey = TileOf(clyde->y) - pacTile.y;
            return (ex*ex + ey*ey >= CLYDE_SHY_DIST_SQ)? pacTile : scatterTargets[GHOST_CLYDE];
        }
        default: return pacTile;
    }
}

static Dir GhostChooseDir(Game *game, const Ghost *ghost, int id, int tx, int ty)
{
    const Maze *maze = &game->maze;
    const Dir reverse = DirOpposite(ghost->actor.dir);

    if (ghost->frightened && (ghost->state == GHOST_ACTIVE))
    {
        // Pseudo-random pick, falling through the priority order until a legal exit.
        const uint32_t start = NextRandom(game) & 3u;
        for (uint32_t k = 0; k < 4; k++)
        {
            const Dir d = decisionOrder[(start + k) & 3u];
            if ((d != reverse) && !MazeIsWall(maze, tx + dirDx[d], ty + dirDy[d])) return d;
        }
        return reverse;
    }

    const TilePos target = (ghost->state == GHOST_EYES)? (TilePos){ EYES_TARGET_TX, EYES_TARGET_TY } : GhostTarget(game, id);
    const bool restrictUp = game->pack->mazes[game->mazeIndex].classicArt &&
                            (ghost->state == GHOST_ACTIVE) && IsNoUpTile(tx, ty);

    Dir best = reverse;
    int32_t bestDist = INT32_MAX;
    for (int k = 0; k < 4; k++)
    {
        const Dir d = decisionOrder[k];
        if ((d == reverse) || ((d == DIR_UP) && restrictUp)) continue;

        const int nx = tx + dirDx[d];
        const int ny = ty + dirDy[d];
        if (MazeIsWall(maze, nx, ny)) continue;

        const int32_t ex = nx - target.x;
        const int32_t ey = ny - target.y;
        const int32_t dist = ex*ex + ey*ey;
        if (dist < bestDist)
        {
            bestDist = dist;
            best = d;
        }
    }
    return best;
}

static void GhostMoveInMaze(Game *game, Ghost *ghost, int id)
{
    Actor *actor = &ghost->actor;
    actor->x = WrapTunnelX(actor->x + dirDx[actor->dir]);
    actor->y += dirDy[actor->dir];

    // Decisions happen only on tile centres. x & 7 is correct for the negative tunnel tile.
    if (((actor->x & 7) != TILE_CENTER) || ((actor->y & 7) != TILE_CENTER)) return;

    const int tx = TileOf(actor->x);
    const int ty = TileOf(actor->y);
    if ((ghost->state == GHOST_EYES) && (ty == EYES_TARGET_TY) && ((tx == EYES_TARGET_TX) || (tx == EYES_TARGET_TX + 1)))
    {
        ghost->state = GHOST_ENTERING;
        return;
    }
    if ((unsigned)tx >= MAZE_COLS) return;      // Inside the tunnel: keep going.

    actor->dir = GhostChooseDir(game, ghost, id, tx, ty);
}

static void StepToward(int32_t *value, int32_t goal, Actor *actor, Dir negative, Dir positive)
{
    const int s = Sign(goal - *value);
    *value += s;
    actor->dir = (s < 0)? negative : positive;
}

static void GhostStep(Game *game, Ghost *ghost, int id)
{
    Actor *actor = &ghost->actor;
    switch (ghost->state)
    {
        case GHOST_IN_HOUSE:
        {
            if (actor->dir != DIR_DOWN) actor->dir = DIR_UP;
            actor->y += dirDy[actor->dir];
            if (actor->y <= HOUSE_CENTER_Y - HOUSE_BOB_RANGE) actor->dir = DIR_DOWN;
            else if (actor->y >= HOUSE_CENTER_Y + HOUSE_BOB_RANGE) actor->dir = DIR_UP;
        } break;
        case GHOST_LEAVING:
        {
            if (actor->x != HOUSE_DOOR_X)
            {
                if (actor->y != HOUSE_CENTER_Y) StepToward(&actor->y, HOUSE_CENTER_Y, actor, DIR_UP, DIR_DOWN);
                else StepToward(&actor->x, HOUSE_DOOR_X, actor, DIR_LEFT, DIR_RIGHT);
            }
            else if (actor->y > HOUSE_EXIT_Y) StepToward(&actor->y, HOUSE_EXIT_Y, actor, DIR_UP, DIR_DOWN);
            else
            {
                ghost->state = GHOST_ACTIVE;
                actor->dir = DIR_LEFT;
            }
        } break;
        case GHOST_ENTERING:
        {
            if ((actor->y == HOUSE_EXIT_Y) && (actor->x != HOUSE_DOOR_X)) StepToward(&actor->x, HOUSE_DOOR_X, actor, DIR_LEFT, DIR_RIGHT);
            else if (actor->y < HOUSE_CENTER_Y) StepToward(&actor->y, HOUSE_CENTER_Y, actor, DIR_UP, DIR_DOWN);
            else if (actor->x != ghost->homeX) StepToward(&actor->x, ghost->homeX, actor, DIR_LEFT, DIR_RIGHT);
            else
            {
                ghost->state = GHOST_LEAVING;
                ghost->frightened = false;
            }
        } break;
        case GHOST_ACTIVE:
        case GHOST_EYES: GhostMoveInMaze(game, ghost, id); break;
        default: break;
    }
}

static uint32_t GhostSpeedPercent(const Game *game, const Ghost *ghost, int id)
{
    const LevelSpec *spec = game->spec;
    switch (ghost->state)
    {
        case GHOST_EYES:
        case GHOST_ENTERING: return EYES_SPEED_PCT;
        case GHOST_IN_HOUSE:
        case GHOST_LEAVING: return HOUSE_SPEED_PCT;
        default: break;
    }

    if (ghost->frightened) return spec->ghostFrightSpeed;

    if (MazeIsTunnel(&game->maze, TileOf(ghost->actor.x), TileOf(ghost->actor.y))) return spec->ghostTunnelSpeed;

    if (id == GHOST_BLINKY)
    {
        const int elroy = ElroyLevel(game);
        if (elroy == 2) return spec->elroy2Speed;
        if (elroy == 1) return spec->elroy1Speed;
    }
    return spec->ghostSpeed;
}

FORCE_INLINE uint32_t ActorAdvance(Actor *actor, uint32_t speedQ16)
{
    actor->subpixel += speedQ16;
    const uint32_t steps = actor->subpixel >> 16;
    actor->subpixel &= 0xFFFFu;
    return steps;
}

static void GhostTick(Game *game, int id)
{
    Ghost *ghost = &game->ghosts[id];
    if (ghost->reversePending)
    {
        ghost->reversePending = false;
        if (ghost->state == GHOST_ACTIVE) ghost->actor.dir = DirOpposite(ghost->actor.dir);
    }

    const uint32_t steps = ActorAdvance(&ghost->actor, SpeedQ16(GhostSpeedPercent(game, ghost, id)));
    for (uint32_t s = 0; s < steps; s++) GhostStep(game, ghost, id);
}

//----------------------------------------------------------------------------------
// Pac-Man
//----------------------------------------------------------------------------------
// Moves one pixel. Perpendicular wishes are accepted early ("cornering"): the off-axis
// offset is pulled back to the tile centre by one pixel per step.
static bool PacStep(Game *game)
{
    const Maze *maze = &game->maze;
    Pacman *pac = &game->pac;
    Actor *actor = &pac->actor;

    const int tx = TileOf(actor->x);
    const int ty = TileOf(actor->y);
    const int cx = TileCenterPixel(tx);
    const int cy = TileCenterPixel(ty);

    const Dir wish = pac->wish;
    if ((wish != DIR_NONE) && (wish != actor->dir))
    {
        if ((wish == DirOpposite(actor->dir)) || !MazeIsWall(maze, tx + dirDx[wish], ty + dirDy[wish])) actor->dir = wish;
    }

    const int dx = dirDx[actor->dir];
    const int dy = dirDy[actor->dir];
    if (dx != 0)
    {
        if (((actor->x - cx)*dx >= 0) && MazeIsWall(maze, tx + dx, ty)) return false;
        actor->x = WrapTunnelX(actor->x + dx);
        actor->y += Sign(cy - actor->y);
    }
    else
    {
        if (((actor->y - cy)*dy >= 0) && MazeIsWall(maze, tx, ty + dy)) return false;
        actor->y += dy;
        actor->x += Sign(cx - actor->x);
    }

    pac->animDistance++;
    return true;
}

// Returns true when a pellet was eaten (which stalls Pac-Man for the rest of the tick).
static bool PacEat(Game *game)
{
    Maze *maze = &game->maze;
    const int tx = TileOf(game->pac.actor.x);
    const int ty = TileOf(game->pac.actor.y);

    if ((game->fruitTicksLeft > 0) && (ty == FRUIT_TILE_Y) && ((tx == 13) || (tx == 14)))
    {
        AddScore(game, game->spec->fruitPoints);
        game->events |= GAME_EVENT_FRUIT;
        game->fruitTicksLeft = 0;
        game->fruitScoreTicksLeft = FRUIT_SCORE_TICKS;
    }

    if (!MazeHasPellet(maze, tx, ty)) return false;

    const bool energizer = MazeIsEnergizer(maze, tx, ty);
    MazeClearPellet(maze, tx, ty);
    game->dotsEaten++;
    game->ticksSinceDot = 0;
    game->pac.pauseTicks = energizer? 3 : 1;
    AddScore(game, energizer? 50 : 10);
    game->events |= energizer? GAME_EVENT_ENERGIZER : GAME_EVENT_DOT;
    CountDotForHouse(game);

    if ((game->dotsEaten == 70) || (game->dotsEaten == 170)) game->fruitTicksLeft = FRUIT_TICKS;
    if (energizer) StartFright(game);
    if (maze->pelletsLeft == 0)
    {
        EnterPhase(game, PHASE_LEVEL_DONE);
        game->events |= GAME_EVENT_LEVEL_DONE;
    }
    return true;
}

static void PacTick(Game *game)
{
    Pacman *pac = &game->pac;
    if (pac->pauseTicks > 0)
    {
        pac->pauseTicks--;
        return;
    }

    const uint32_t percent = (game->frightTicksLeft > 0)? game->spec->pacFrightSpeed : game->spec->pacSpeed;
    const uint32_t steps = ActorAdvance(&pac->actor, SpeedQ16(percent));
    for (uint32_t s = 0; s < steps; s++)
    {
        if (!PacStep(game)) break;
        if (PacEat(game)) break;
    }
}

//----------------------------------------------------------------------------------
// Tick
//----------------------------------------------------------------------------------
// Returns true when a collision changed the phase.
static bool ResolveCollisions(Game *game)
{
    const int ptx = TileOf(game->pac.actor.x);
    const int pty = TileOf(game->pac.actor.y);

    for (int i = 0; i < GHOST_COUNT; i++)
    {
        Ghost *ghost = &game->ghosts[i];
        if (ghost->state != GHOST_ACTIVE) continue;
        if ((TileOf(ghost->actor.x) != ptx) || (TileOf(ghost->actor.y) != pty)) continue;

        if (ghost->frightened)
        {
            const uint32_t points = 200u << MIN(game->ghostsEatenInFright, 3u);
            game->ghostsEatenInFright++;
            AddScore(game, points);
            ghost->frightened = false;
            ghost->state = GHOST_EYES;
            game->eatenGhost = i;
            game->eatenPoints = points;
            EnterPhase(game, PHASE_GHOST_EATEN);
            game->events |= GAME_EVENT_GHOST_EATEN;
        }
        else EnterPhase(game, PHASE_DYING);
        return true;
    }
    return false;
}

static void TickPlaying(Game *game)
{
    UpdateModeTimers(game);
    UpdateHouse(game);
    game->ticksSinceDot++;
    if (game->fruitTicksLeft > 0) game->fruitTicksLeft--;
    if (game->fruitScoreTicksLeft > 0) game->fruitScoreTicksLeft--;

    PacTick(game);
    if (game->phase != PHASE_PLAYING) return;
    if (ResolveCollisions(game)) return;

    for (int i = 0; i < GHOST_COUNT; i++) GhostTick(game, i);
    ResolveCollisions(game);
}

static void SnapshotPositions(Game *game)
{
    game->pac.actor.prevX = game->pac.actor.x;
    game->pac.actor.prevY = game->pac.actor.y;
    for (int i = 0; i < GHOST_COUNT; i++)
    {
        game->ghosts[i].actor.prevX = game->ghosts[i].actor.x;
        game->ghosts[i].actor.prevY = game->ghosts[i].actor.y;
    }
}

void GameTick(Game *game, const GameInput *input)
{
    game->events = 0;
    if (game->phase == PHASE_GAME_OVER) return;

    game->tick++;
    game->phaseTicks++;
    SnapshotPositions(game);
    if (input->dir != DIR_NONE) game->pac.wish = input->dir;

    switch (game->phase)
    {
        case PHASE_READY:
        {
            const uint32_t duration = game->firstReady? FIRST_READY_TICKS : READY_TICKS;
            if (game->phaseTicks >= duration)
            {
                game->firstReady = false;
                EnterPhase(game, PHASE_PLAYING);
            }
        } break;
        case PHASE_PLAYING: TickPlaying(game); break;
        case PHASE_GHOST_EATEN:
        {
            if (game->phaseTicks >= GHOST_EATEN_TICKS) EnterPhase(game, PHASE_PLAYING);
        } break;
        case PHASE_DYING:
        {
            if (game->phaseTicks == DEATH_FREEZE_TICKS) game->events |= GAME_EVENT_DEATH;
            if (game->phaseTicks < DEATH_FREEZE_TICKS + DEATH_FRAMES*DEATH_FRAME_TICKS + DEATH_TAIL_TICKS) break;

            if (--game->lives <= 0)
            {
                EnterPhase(game, PHASE_GAME_OVER);
                game->events |= GAME_EVENT_GAME_OVER;
                break;
            }
            ResetActors(game);
            game->globalDotCounterActive = true;
            game->globalDotCounter = 0;
            game->elroySuspended = true;
            EnterPhase(game, PHASE_READY);
            game->events |= GAME_EVENT_LEVEL_START;
        } break;
        case PHASE_LEVEL_DONE:
        {
            if (game->phaseTicks >= LEVEL_DONE_FREEZE_TICKS + LEVEL_FLASHES*LEVEL_FLASH_TICKS)
            {
                game->level++;
                StartLevel(game);
            }
        } break;
        default: break;
    }
}
