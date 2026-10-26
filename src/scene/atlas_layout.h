#ifndef SCENE_ATLAS_LAYOUT_H
#define SCENE_ATLAS_LAYOUT_H

#include "core/base.h"
#include "game/level.h"
#include "game/maze.h"

// Every sprite, glyph and both maze variants are packed into one 512x512 RGBA texture at
// start-up, so a whole frame is a single draw call with no texture or state changes.
// This header is the contract between the atlas builder (gpu/) and the scene (scene/).
//
//   (0,0)    maze, blue walls          224x248
//   (224,0)  maze, white walls (flash) 224x248
//   (0,248)  sprite sheet block        224x208  (general_sprites.png x 456.., 16x16 grid)
//   (224,248) 8x8 cells: glyphs, pellets, solid white

#define ATLAS_SIZE              512

#define ATLAS_MAZE_X            0
#define ATLAS_MAZE_FLASH_X      MAZE_WIDTH
#define ATLAS_MAZE_Y            0

#define ATLAS_SPRITES_X         0
#define ATLAS_SPRITES_Y         MAZE_HEIGHT
#define ATLAS_SPRITES_W         224
#define ATLAS_SPRITES_H         208
#define SPRITE_SIZE             16

#define ATLAS_CELLS_X           MAZE_WIDTH
#define ATLAS_CELLS_Y           MAZE_HEIGHT
#define ATLAS_CELLS_PER_ROW     16
#define CELL_SIZE               8

// Cell indices in the 8x8 area: 0-127 follow ASCII for the glyphs the arcade font has.
#define CELL_COPYRIGHT          1
#define CELL_DOT                2
#define CELL_ENERGIZER          3
#define CELL_SOLID              4
#define CELL_COUNT              128

typedef struct AtlasRect {
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
} AtlasRect;

FORCE_INLINE AtlasRect AtlasSprite(int col, int row)
{
    return (AtlasRect){ (uint16_t)(ATLAS_SPRITES_X + col*SPRITE_SIZE), (uint16_t)(ATLAS_SPRITES_Y + row*SPRITE_SIZE), SPRITE_SIZE, SPRITE_SIZE };
}

FORCE_INLINE AtlasRect AtlasCell(int cell)
{
    return (AtlasRect){ (uint16_t)(ATLAS_CELLS_X + (cell%ATLAS_CELLS_PER_ROW)*CELL_SIZE),
                        (uint16_t)(ATLAS_CELLS_Y + (cell/ATLAS_CELLS_PER_ROW)*CELL_SIZE), CELL_SIZE, CELL_SIZE };
}

// Sprite sheet grid (16x16 cells of the sheet block).
#define SPRITE_ROW_PAC_RIGHT    0           // Pac-Man rows are indexed by Dir.
#define SPRITE_COL_PAC_WIDE     0
#define SPRITE_COL_PAC_HALF     1
#define SPRITE_COL_PAC_CLOSED   2
#define SPRITE_COL_DEATH        3           // Row 0, 11 frames.
#define SPRITE_ROW_FRUIT        3
#define SPRITE_COL_FRUIT        3           // Indexed by FruitKind.
#define SPRITE_ROW_GHOST        4           // + GhostId; column = dir*2 + frame.
#define SPRITE_COL_FRIGHT       8           // Row 4: blue x2, then white x2.
#define SPRITE_COL_EYES         8           // Row 5, indexed by Dir.
#define SPRITE_ROW_GHOST_SCORE  8           // 200, 400, 800, 1600.

// Fruit bonus labels have irregular widths in the sheet.
FORCE_INLINE AtlasRect AtlasFruitScore(FruitKind fruit)
{
    static const AtlasRect rects[FRUIT_COUNT] = {
        { 0, 144, 16, 16 }, { 16, 144, 16, 16 }, { 32, 144, 16, 16 }, { 48, 144, 16, 16 },
        { 64, 144, 18, 16 }, { 62, 160, 20, 16 }, { 62, 176, 20, 16 }, { 62, 192, 20, 16 },
    };
    AtlasRect r = rects[fruit];
    r.x += ATLAS_SPRITES_X;
    r.y += ATLAS_SPRITES_Y;
    return r;
}

#endif // SCENE_ATLAS_LAYOUT_H
