#include "scene/maze_art.h"

// Distance to an open tile's pixel rectangle. A four-pixel inset keeps actor centres and
// the original maze's corridor spacing aligned. Squared distances round block corners.
static int OpenDistanceSquared(const MazeLayout *layout, int px, int py)
{
    int best = 1000;
    int tx = px/TILE_SIZE;
    int ty = py/TILE_SIZE;
    for (int y = MAX(0, ty - 2); y <= MIN(MAZE_ROWS - 1, ty + 2); y++)
    {
        for (int x = MAX(0, tx - 2); x <= MIN(MAZE_COLS - 1, tx + 2); x++)
        {
            if (layout->tiles[y][x] == TILE_WALL) continue;
            int dx = MAX(MAX(x*TILE_SIZE - px, px - (x*TILE_SIZE + 7)), 0);
            int dy = MAX(MAX(y*TILE_SIZE - py, py - (y*TILE_SIZE + 7)), 0);
            best = MIN(best, dx*dx + dy*dy);
        }
    }
    return best;
}

void MazeArtRender(const MazeLayout *layout, uint32_t wallColor, uint32_t doorColor, uint8_t *pixels)
{
    for (int y = 0; y < MAZE_HEIGHT; y++)
    {
        for (int x = 0; x < MAZE_WIDTH; x++)
        {
            uint32_t color = RGBA(0, 0, 0, 255);
            char tile = layout->tiles[y/TILE_SIZE][x/TILE_SIZE];
            if (tile == TILE_WALL)
            {
                int distance = OpenDistanceSquared(layout, x, y);
                bool line = distance >= 9 && distance < 16;
                // Retain a second outer boundary without flooding thin wall islands.
                bool edge = x < 4 || y < 4 || x >= MAZE_WIDTH - 4 || y >= MAZE_HEIGHT - 4;
                if (line || (edge && distance >= 36 && distance < 49)) color = wallColor;
            }
            else if (tile == TILE_DOOR && ((y & 7) == 3 || (y & 7) == 4)) color = doorColor;
            size_t i = ((size_t)y*MAZE_WIDTH + (size_t)x)*4;
            pixels[i] = (uint8_t)color;
            pixels[i + 1] = (uint8_t)(color >> 8);
            pixels[i + 2] = (uint8_t)(color >> 16);
            pixels[i + 3] = 255;
        }
    }
}
