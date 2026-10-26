#ifndef GAME_LEVEL_H
#define GAME_LEVEL_H

#include "core/base.h"

#define TICKS_PER_SECOND    60
#define MODE_PHASES         7       // Alternating scatter/chase durations; chase is permanent afterwards.

typedef enum FruitKind {
    FRUIT_CHERRY = 0,
    FRUIT_STRAWBERRY,
    FRUIT_ORANGE,
    FRUIT_APPLE,
    FRUIT_MELON,
    FRUIT_GALAXIAN,
    FRUIT_BELL,
    FRUIT_KEY,
    FRUIT_COUNT
} FruitKind;

// Per-level arcade parameters (The Pac-Man Dossier). Speeds are percentages of the
// 75.76 px/s reference speed; durations are 60 Hz ticks.
typedef struct LevelSpec {
    uint8_t pacSpeed;
    uint8_t pacFrightSpeed;
    uint8_t ghostSpeed;
    uint8_t ghostFrightSpeed;
    uint8_t ghostTunnelSpeed;
    uint8_t elroy1Dots;
    uint8_t elroy1Speed;
    uint8_t elroy2Dots;
    uint8_t elroy2Speed;
    uint8_t frightFlashes;
    uint16_t frightTicks;
    uint8_t fruit;                  // FruitKind
    uint16_t fruitPoints;
    uint8_t houseDotLimit[4];       // Indexed by GhostId.
    uint16_t houseTimeoutTicks;
    uint32_t modeTicks[MODE_PHASES];
} LevelSpec;

// level is 1-based; levels past the table repeat its last entry.
const LevelSpec *LevelGetSpec(int level);

// Percent of reference speed -> Q16.16 pixels per tick.
FORCE_INLINE uint32_t SpeedQ16(uint32_t percent)
{
    // 75.75757625 px/s / 60 Hz = 1.26262627 px/tick = 82747.47 in Q16.16.
    return (percent*82747u)/100u;
}

#endif // GAME_LEVEL_H
