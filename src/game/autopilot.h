#ifndef GAME_AUTOPILOT_H
#define GAME_AUTOPILOT_H

#include "game/game.h"

// Deterministic computer player used by the attract demo and the benchmarks.
// Breadth-first search to the nearest pellet, fruit or frightened ghost, treating tiles
// around dangerous ghosts as blocked; flees when no safe path exists.
Dir AutopilotDecide(const Game *game);

#endif // GAME_AUTOPILOT_H
