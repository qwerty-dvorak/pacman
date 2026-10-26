#ifndef GAME_LEVEL_PACK_H
#define GAME_LEVEL_PACK_H

#include "core/base.h"
#include "game/maze.h"

#define PACK_MAX_MAZES      32
#define PACK_NAME_SIZE      16
#define MAZE_NAME_SIZE      16

typedef struct PackMaze {
    MazeLayout layout;
    Maze maze;                      // Built form, copied at every level start.
    uint32_t wallColor;             // RGBA(), see core/base.h.
    bool classicArt;                // Draw with the original arcade maze image.
    char name[MAZE_NAME_SIZE];
} PackMaze;

// An ordered maze rotation: levels 1..levelsPerMaze use maze 0, the next block maze 1, and
// so on, cycling. The per-level rules (speeds, fright time, fruit) come from LevelGetSpec.
typedef struct LevelPack {
    char name[PACK_NAME_SIZE];
    int32_t mazeCount;
    int32_t levelsPerMaze;
    PackMaze mazes[PACK_MAX_MAZES];
} LevelPack;

typedef enum BuiltinPack {
    BUILTIN_ARCADE = 0,             // The original maze on every level.
    BUILTIN_TOUR,                   // Original plus four new mazes, two levels each.
    BUILTIN_COUNT
} BuiltinPack;

void LevelPackInitBuiltin(LevelPack *pack, BuiltinPack which);
void LevelPackInitEmpty(LevelPack *pack, const char *name, int levelsPerMaze);

// Appends a maze (layout must already be validated). Returns false when the pack is full.
bool LevelPackAdd(LevelPack *pack, const MazeLayout *layout, const char *name, uint32_t wallColor, bool classicArt);

int LevelPackMazeIndex(const LevelPack *pack, int level);

// Maze text file: optional header lines "name <NAME>" and "color <R> <G> <B>", then exactly
// 31 rows of 28 tile characters (see TILE_* in game/maze.h). '\r' is ignored.
bool MazeFileParse(const char *text, MazeLayout *layout, char *name, size_t nameSize, uint32_t *wallColor);

// Writes the file form into out; returns the length, or 0 if out is too small.
size_t MazeFileFormat(const MazeLayout *layout, const char *name, uint32_t wallColor, char *out, size_t outSize);

#endif // GAME_LEVEL_PACK_H
