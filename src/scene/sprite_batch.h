#ifndef SCENE_SPRITE_BATCH_H
#define SCENE_SPRITE_BATCH_H

#include "core/base.h"
#include "scene/atlas_layout.h"

// Arcade screen: 28x36 tiles; the maze occupies rows 3-33.
#define SCREEN_WIDTH        224
#define SCREEN_HEIGHT       288
#define MAZE_OFFSET_Y       (3*TILE_SIZE)

// 12-byte vertex: framebuffer-pixel position, atlas texel coordinate, RGBA8 tint.
typedef struct Vertex {
    int16_t x;
    int16_t y;
    uint16_t u;
    uint16_t v;
    uint32_t color;
} Vertex;

_Static_assert(sizeof(Vertex) == 12, "Vertex layout must stay packed");

#define COLOR_WHITE         RGBA(222, 222, 255, 255)
#define COLOR_YELLOW        RGBA(255, 255, 0, 255)
#define COLOR_RED           RGBA(255, 0, 0, 255)
#define COLOR_CYAN          RGBA(0, 255, 255, 255)
#define COLOR_OPAQUE        RGBA(255, 255, 255, 255)

// CPU side of the sprite pipeline: writes quads (4 vertices, counter-clockwise from the
// top-left) straight into a caller-provided buffer, which is the GPU's persistently
// mapped ring slot at run time and plain memory in the headless benchmark.
typedef struct SpriteBatch {
    Vertex *vertices;
    int quadCount;
    int quadCapacity;

    // Integer scale and letterbox offset of the arcade screen in the framebuffer.
    int framebufferWidth;
    int framebufferHeight;
    int scale;
    int originX;
    int originY;
} SpriteBatch;

void SpriteBatchBegin(SpriteBatch *batch, Vertex *vertices, int quadCapacity, int framebufferWidth, int framebufferHeight);

FORCE_INLINE void SpriteBatchQuad(SpriteBatch *batch, int x, int y, int w, int h, AtlasRect src, uint32_t color)
{
    if (UNLIKELY(batch->quadCount >= batch->quadCapacity)) return;

    Vertex *v = batch->vertices + (size_t)batch->quadCount*4;
    batch->quadCount++;

    const int16_t x0 = (int16_t)x;
    const int16_t y0 = (int16_t)y;
    const int16_t x1 = (int16_t)(x + w);
    const int16_t y1 = (int16_t)(y + h);
    const uint16_t u0 = src.x;
    const uint16_t v0 = src.y;
    const uint16_t u1 = (uint16_t)(src.x + src.w);
    const uint16_t v1 = (uint16_t)(src.y + src.h);

    v[0] = (Vertex){ x0, y0, u0, v0, color };
    v[1] = (Vertex){ x0, y1, u0, v1, color };
    v[2] = (Vertex){ x1, y1, u1, v1, color };
    v[3] = (Vertex){ x1, y0, u1, v0, color };
}

// Integer arcade-pixel position.
FORCE_INLINE void SpriteBatchArcade(SpriteBatch *batch, int ax, int ay, AtlasRect src, uint32_t color)
{
    const int s = batch->scale;
    SpriteBatchQuad(batch, batch->originX + ax*s, batch->originY + ay*s, src.w*s, src.h*s, src, color);
}

// Text in arcade tile coordinates, drawn with the arcade font.
void SpriteBatchText(SpriteBatch *batch, int col, int row, const char *text, uint32_t color);

// Right-aligns value so its last digit lands in column endCol; at least minDigits digits.
void SpriteBatchNumber(SpriteBatch *batch, int endCol, int row, uint32_t value, int minDigits, uint32_t color);

// Framebuffer-pixel text and boxes (debug overlay); glyphScale multiplies the 8 px glyphs.
void SpriteBatchTextPx(SpriteBatch *batch, int x, int y, int glyphScale, const char *text, uint32_t color);
void SpriteBatchRectPx(SpriteBatch *batch, int x, int y, int w, int h, uint32_t color);

#endif // SCENE_SPRITE_BATCH_H
