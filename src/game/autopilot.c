#include "game/autopilot.h"

#include <string.h>

// Search grid: the 28 maze columns plus the two virtual tunnel tiles (-1 and 28).
#define GRID_COLS       (MAZE_COLS + 2)
#define GRID_NODES      (GRID_COLS*MAZE_ROWS)
#define DANGER_RADIUS   2

FORCE_INLINE int NodeOf(int tx, int ty) { return ty*GRID_COLS + (tx + 1); }

static bool IsDangerousGhost(const Ghost *ghost)
{
    return !ghost->frightened && ((ghost->state == GHOST_ACTIVE) || (ghost->state == GHOST_LEAVING));
}

static void MarkDanger(uint8_t *danger, int gx, int gy)
{
    for (int dy = -DANGER_RADIUS; dy <= DANGER_RADIUS; dy++)
    {
        const int y = gy + dy;
        if ((unsigned)y >= MAZE_ROWS) continue;

        const int span = DANGER_RADIUS - ((dy < 0)? -dy : dy);
        for (int dx = -span; dx <= span; dx++)
        {
            const int x = gx + dx;
            if ((x < -1) || (x > MAZE_COLS)) continue;
            danger[NodeOf(x, y)] = 1;
        }
    }
}

static bool IsGoal(const Game *game, int tx, int ty)
{
    if (MazeHasPellet(&game->maze, tx, ty)) return true;
    if ((game->fruitTicksLeft > 0) && (ty == 17) && ((tx == 13) || (tx == 14))) return true;

    for (int i = 0; i < GHOST_COUNT; i++)
    {
        const Ghost *ghost = &game->ghosts[i];
        if (ghost->frightened && (ghost->state == GHOST_ACTIVE) && (game->frightTicksLeft > TICKS_PER_SECOND) &&
            (TileOf(ghost->actor.x) == tx) && (TileOf(ghost->actor.y) == ty)) return true;
    }
    return false;
}

static Dir Flee(const Game *game, int px, int py)
{
    Dir best = game->pac.actor.dir;
    int bestScore = -1;
    for (int d = 0; d < 4; d++)
    {
        const int nx = px + dirDx[d];
        const int ny = py + dirDy[d];
        if (MazeIsWall(&game->maze, nx, ny)) continue;

        int nearest = INT32_MAX;
        for (int i = 0; i < GHOST_COUNT; i++)
        {
            const Ghost *ghost = &game->ghosts[i];
            if (!IsDangerousGhost(ghost)) continue;
            const int ex = TileOf(ghost->actor.x) - nx;
            const int ey = TileOf(ghost->actor.y) - ny;
            nearest = MIN(nearest, ((ex < 0)? -ex : ex) + ((ey < 0)? -ey : ey));
        }
        if (nearest > bestScore)
        {
            bestScore = nearest;
            best = (Dir)d;
        }
    }
    return best;
}

Dir AutopilotDecide(const Game *game)
{
    uint8_t danger[GRID_NODES];
    uint8_t firstDir[GRID_NODES];
    uint16_t queue[GRID_NODES];
    memset(danger, 0, sizeof(danger));
    memset(firstDir, DIR_NONE, sizeof(firstDir));

    for (int i = 0; i < GHOST_COUNT; i++)
    {
        const Ghost *ghost = &game->ghosts[i];
        if (IsDangerousGhost(ghost)) MarkDanger(danger, TileOf(ghost->actor.x), TileOf(ghost->actor.y));
    }

    const int px = TileOf(game->pac.actor.x);
    const int py = TileOf(game->pac.actor.y);
    const int start = NodeOf(px, py);

    int head = 0;
    int tail = 0;
    queue[tail++] = (uint16_t)start;
    firstDir[start] = 0;            // Any non-NONE value marks the start as visited.

    while (head < tail)
    {
        const int node = queue[head++];
        const int tx = node%GRID_COLS - 1;
        const int ty = node/GRID_COLS;

        if ((node != start) && IsGoal(game, tx, ty)) return (Dir)firstDir[node];

        for (int d = 0; d < 4; d++)
        {
            int nx = tx + dirDx[d];
            const int ny = ty + dirDy[d];
            if (nx < -1) nx = MAZE_COLS;
            else if (nx > MAZE_COLS) nx = -1;
            if (MazeIsWall(&game->maze, nx, ny)) continue;

            const int next = NodeOf(nx, ny);
            if ((firstDir[next] != DIR_NONE) || danger[next]) continue;

            firstDir[next] = (node == start)? (uint8_t)d : firstDir[node];
            queue[tail++] = (uint16_t)next;
        }
    }

    return Flee(game, px, py);
}
