#include "game/level.h"

#define S(seconds)  ((uint32_t)((seconds)*TICKS_PER_SECOND))

#define MODES_L1    { S(7), S(20), S(7), S(20), S(5), S(20), S(5) }
#define MODES_L2    { S(7), S(20), S(7), S(20), S(5), S(1033), 1 }
#define MODES_L5    { S(5), S(20), S(5), S(20), S(5), S(1037), 1 }

#define HOUSE_L1    { 0, 0, 30, 60 }
#define HOUSE_L2    { 0, 0, 0, 50 }
#define HOUSE_L3    { 0, 0, 0, 0 }

//   pac  pacF ghost ghostF tunnel  e1Dots e1Spd e2Dots e2Spd  flashes frightTicks  fruit  points  house  timeout modes
#define ROW(pac, pacF, gh, ghF, tun, e1d, e1s, e2d, e2s, fl, frS, fruit, pts, house, tmo, modes) \
    { pac, pacF, gh, ghF, tun, e1d, e1s, e2d, e2s, fl, S(frS), fruit, pts, house, S(tmo), modes }

static const LevelSpec levelTable[] = {
    ROW( 80,  90, 75, 50, 40,  20,  80, 10,  85, 5, 6, FRUIT_CHERRY,     100, HOUSE_L1, 4, MODES_L1),
    ROW( 90,  95, 85, 55, 45,  30,  90, 15,  95, 5, 5, FRUIT_STRAWBERRY, 300, HOUSE_L2, 4, MODES_L2),
    ROW( 90,  95, 85, 55, 45,  40,  90, 20,  95, 5, 4, FRUIT_ORANGE,     500, HOUSE_L3, 4, MODES_L2),
    ROW( 90,  95, 85, 55, 45,  40,  90, 20,  95, 5, 3, FRUIT_ORANGE,     500, HOUSE_L3, 4, MODES_L2),
    ROW(100, 100, 95, 60, 50,  40, 100, 20, 105, 5, 2, FRUIT_APPLE,      700, HOUSE_L3, 3, MODES_L5),
    ROW(100, 100, 95, 60, 50,  50, 100, 25, 105, 5, 5, FRUIT_APPLE,      700, HOUSE_L3, 3, MODES_L5),
    ROW(100, 100, 95, 60, 50,  50, 100, 25, 105, 5, 2, FRUIT_MELON,     1000, HOUSE_L3, 3, MODES_L5),
    ROW(100, 100, 95, 60, 50,  50, 100, 25, 105, 5, 2, FRUIT_MELON,     1000, HOUSE_L3, 3, MODES_L5),
    ROW(100, 100, 95, 60, 50,  60, 100, 30, 105, 3, 1, FRUIT_GALAXIAN,  2000, HOUSE_L3, 3, MODES_L5),
    ROW(100, 100, 95, 60, 50,  60, 100, 30, 105, 5, 5, FRUIT_GALAXIAN,  2000, HOUSE_L3, 3, MODES_L5),
    ROW(100, 100, 95, 60, 50,  60, 100, 30, 105, 5, 2, FRUIT_BELL,      3000, HOUSE_L3, 3, MODES_L5),
    ROW(100, 100, 95, 60, 50,  80, 100, 40, 105, 3, 1, FRUIT_BELL,      3000, HOUSE_L3, 3, MODES_L5),
    ROW(100, 100, 95, 60, 50,  80, 100, 40, 105, 3, 1, FRUIT_KEY,       5000, HOUSE_L3, 3, MODES_L5),
    ROW(100, 100, 95, 60, 50,  80, 100, 40, 105, 5, 3, FRUIT_KEY,       5000, HOUSE_L3, 3, MODES_L5),
    ROW(100, 100, 95, 60, 50, 100, 100, 50, 105, 3, 1, FRUIT_KEY,       5000, HOUSE_L3, 3, MODES_L5),
    ROW(100, 100, 95, 60, 50, 100, 100, 50, 105, 3, 1, FRUIT_KEY,       5000, HOUSE_L3, 3, MODES_L5),
    ROW(100, 100, 95, 60, 50, 100, 100, 50, 105, 0, 0, FRUIT_KEY,       5000, HOUSE_L3, 3, MODES_L5),
    ROW(100, 100, 95, 60, 50, 100, 100, 50, 105, 3, 1, FRUIT_KEY,       5000, HOUSE_L3, 3, MODES_L5),
    ROW(100, 100, 95, 60, 50, 120, 100, 60, 105, 0, 0, FRUIT_KEY,       5000, HOUSE_L3, 3, MODES_L5),
    ROW(100, 100, 95, 60, 50, 120, 100, 60, 105, 0, 0, FRUIT_KEY,       5000, HOUSE_L3, 3, MODES_L5),
    ROW( 90,  90, 95, 60, 50, 120, 100, 60, 105, 0, 0, FRUIT_KEY,       5000, HOUSE_L3, 3, MODES_L5),
};

const LevelSpec *LevelGetSpec(int level)
{
    const int index = CLAMP(level, 1, (int)ARRAY_COUNT(levelTable)) - 1;
    return &levelTable[index];
}
