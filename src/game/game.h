#ifndef GAME_GAME_H
#define GAME_GAME_H

#include "core/base.h"
#include "game/level.h"
#include "game/level_pack.h"
#include "game/maze.h"

// Fixed-step arcade simulation at 60 Hz. All state is integer and lives in Game, so the
// simulation is deterministic, allocation-free and independent of rendering.

typedef enum GhostId {
    GHOST_BLINKY = 0,
    GHOST_PINKY,
    GHOST_INKY,
    GHOST_CLYDE,
    GHOST_COUNT
} GhostId;

typedef enum GhostState {
    GHOST_IN_HOUSE = 0,     // Bobbing inside the house, waiting for release.
    GHOST_LEAVING,          // Scripted path from the house through the door.
    GHOST_ACTIVE,           // Roaming the maze (scatter, chase or frightened).
    GHOST_EYES,             // Eaten; racing back to the door.
    GHOST_ENTERING          // Scripted path from the door back to its house slot.
} GhostState;

typedef enum GamePhase {
    PHASE_READY = 0,        // "READY!" countdown before play.
    PHASE_PLAYING,
    PHASE_GHOST_EATEN,      // Freeze while the ghost score is shown.
    PHASE_DYING,
    PHASE_LEVEL_DONE,       // Freeze, then maze flash.
    PHASE_GAME_OVER
} GamePhase;

// One-shot happenings of the last tick, for sound and presentation. Cleared every tick.
typedef enum GameEvent {
    GAME_EVENT_DOT          = 1u << 0,
    GAME_EVENT_ENERGIZER    = 1u << 1,
    GAME_EVENT_FRUIT        = 1u << 2,
    GAME_EVENT_GHOST_EATEN  = 1u << 3,
    GAME_EVENT_DEATH        = 1u << 4,      // Death animation starts.
    GAME_EVENT_EXTRA_LIFE   = 1u << 5,
    GAME_EVENT_LEVEL_START  = 1u << 6,      // A READY phase began.
    GAME_EVENT_LEVEL_DONE   = 1u << 7,
    GAME_EVENT_GAME_OVER    = 1u << 8
} GameEvent;

// Actor position is its centre pixel in maze space; prevX/prevY are the position at the
// start of the current tick, for render interpolation.
typedef struct Actor {
    int32_t x;
    int32_t y;
    int32_t prevX;
    int32_t prevY;
    uint32_t subpixel;      // Q16.16 fractional distance carried between ticks.
    Dir dir;
} Actor;

typedef struct Pacman {
    Actor actor;
    Dir wish;               // Latest requested direction; applied as soon as it is legal.
    uint32_t pauseTicks;    // Eating stalls: 1 tick per dot, 3 per energizer.
    uint32_t animDistance;  // Pixels travelled, drives the mouth animation.
} Pacman;

typedef struct Ghost {
    Actor actor;
    GhostState state;
    bool frightened;
    bool reversePending;
    int32_t homeX;          // House slot x, used when returning as eyes.
    uint32_t dotCounter;    // Personal house release counter.
} Ghost;

typedef struct GameInput {
    Dir dir;                // DIR_NONE keeps the previous wish.
} GameInput;

typedef struct GameConfig {
    const LevelPack *pack;  // Must outlive the game.
    uint32_t seed;
    int32_t startLevel;
    int32_t lives;
    uint32_t highScore;     // Shown in the HUD; raised as the score passes it.
} GameConfig;

typedef struct Game {
    Maze maze;
    Pacman pac;
    Ghost ghosts[GHOST_COUNT];
    const LevelSpec *spec;
    const LevelPack *pack;
    int32_t mazeIndex;      // Pack entry of the current level.
    uint32_t events;        // GameEvent bits raised by the last tick.

    GamePhase phase;
    uint32_t phaseTicks;    // Ticks spent in the current phase.
    uint64_t tick;          // Total simulated ticks.

    int32_t level;
    int32_t lives;          // Including the Pac-Man in play.
    uint32_t score;
    uint32_t highScore;
    bool extraLifeAwarded;
    bool firstReady;        // Show "PLAYER ONE" on the first READY of a game.

    // Scatter/chase schedule.
    uint32_t modeIndex;
    uint32_t modeTicksLeft;
    bool chase;

    // Frightened mode.
    uint32_t frightTicksLeft;
    uint32_t ghostsEatenInFright;
    int32_t eatenGhost;     // Ghost shown as a score during PHASE_GHOST_EATEN.
    uint32_t eatenPoints;

    // House release.
    bool globalDotCounterActive;
    uint32_t globalDotCounter;
    uint32_t ticksSinceDot;
    bool elroySuspended;

    // Fruit.
    uint32_t dotsEaten;
    uint32_t fruitTicksLeft;
    uint32_t fruitScoreTicksLeft;

    uint32_t rng;
} Game;

// Timings of the scripted phases, shared with the renderer.
#define READY_TICKS             120
#define FIRST_READY_TICKS       240
#define GHOST_EATEN_TICKS       60
#define DEATH_FREEZE_TICKS      60
#define DEATH_FRAME_TICKS       8
#define DEATH_FRAMES            11
#define DEATH_TAIL_TICKS        60
#define LEVEL_DONE_FREEZE_TICKS 60
#define LEVEL_FLASH_TICKS       12
#define LEVEL_FLASHES           8
#define FRIGHT_FLASH_TICKS      14
#define FRUIT_TICKS             570
#define FRUIT_SCORE_TICKS       120
#define START_LIVES             3
#define EXTRA_LIFE_SCORE        10000

// Fruit and its pick-up tiles (13,17)/(14,17); actors are drawn centred on x = 112.
#define FRUIT_X                 (14*TILE_SIZE)
#define FRUIT_Y                 TileCenterPixel(17)

// Starts a new game at config->startLevel in PHASE_READY.
void GameNew(Game *game, const GameConfig *config);
void GameTick(Game *game, const GameInput *input);

// Fright flashing: true when the ghost should be drawn white.
bool GameFrightFlashWhite(const Game *game);

#endif // GAME_GAME_H
