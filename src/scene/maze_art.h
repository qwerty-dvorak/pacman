#ifndef SCENE_MAZE_ART_H
#define SCENE_MAZE_ART_H

#include "core/base.h"
#include "game/maze.h"

#define MAZE_ART_WIDTH      MAZE_WIDTH
#define MAZE_ART_HEIGHT     MAZE_HEIGHT

// Renders the wall graphics for layout into pixels: RGBA8, row-major,
// MAZE_ART_WIDTH*MAZE_ART_HEIGHT*4 bytes. Background opaque black, wall lines in wallColor,
// house door in doorColor. Colours are packed r | g<<8 | b<<16 | a<<24 (same as RGBA() in
// scene/sprite_batch.h). Pellets are NOT drawn.
//
// Arcade-inspired inset wall outlines with rounded block corners. Coordinates match the
// tile-centred movement model; the original classic maze still uses its source artwork.
// Pure CPU, allocation-free and re-entrant.
void MazeArtRender(const MazeLayout *layout, uint32_t wallColor, uint32_t doorColor, uint8_t *pixels);

#endif // SCENE_MAZE_ART_H
