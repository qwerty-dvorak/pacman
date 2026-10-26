#include "scene/sprite_batch.h"

void SpriteBatchBegin(SpriteBatch *batch, Vertex *vertices, int quadCapacity, int framebufferWidth, int framebufferHeight)
{
    batch->vertices = vertices;
    batch->quadCount = 0;
    batch->quadCapacity = quadCapacity;
    batch->framebufferWidth = MAX(framebufferWidth, 1);
    batch->framebufferHeight = MAX(framebufferHeight, 1);
    batch->scale = MAX(1, MIN(batch->framebufferWidth/SCREEN_WIDTH, batch->framebufferHeight/SCREEN_HEIGHT));
    batch->originX = (batch->framebufferWidth - SCREEN_WIDTH*batch->scale)/2;
    batch->originY = (batch->framebufferHeight - SCREEN_HEIGHT*batch->scale)/2;
}

FORCE_INLINE int GlyphCell(char c)
{
    if ((c >= 'a') && (c <= 'z')) c = (char)(c - 'a' + 'A');
    return (unsigned char)c;
}

void SpriteBatchText(SpriteBatch *batch, int col, int row, const char *text, uint32_t color)
{
    for (int i = 0; text[i] != '\0'; i++)
    {
        const int cell = GlyphCell(text[i]);
        if ((cell == ' ') || (cell >= CELL_COUNT)) continue;
        SpriteBatchArcade(batch, (col + i)*CELL_SIZE, row*CELL_SIZE, AtlasCell(cell), color);
    }
}

void SpriteBatchNumber(SpriteBatch *batch, int endCol, int row, uint32_t value, int minDigits, uint32_t color)
{
    int col = endCol;
    int digits = 0;
    do
    {
        SpriteBatchArcade(batch, col*CELL_SIZE, row*CELL_SIZE, AtlasCell('0' + (int)(value%10)), color);
        value /= 10;
        col--;
        digits++;
    } while ((value != 0) || (digits < minDigits));
}

void SpriteBatchTextPx(SpriteBatch *batch, int x, int y, int glyphScale, const char *text, uint32_t color)
{
    const int size = CELL_SIZE*glyphScale;
    for (int i = 0; text[i] != '\0'; i++)
    {
        const int cell = GlyphCell(text[i]);
        if ((cell == ' ') || (cell >= CELL_COUNT)) continue;
        SpriteBatchQuad(batch, x + i*size, y, size, size, AtlasCell(cell), color);
    }
}

void SpriteBatchRectPx(SpriteBatch *batch, int x, int y, int w, int h, uint32_t color)
{
    SpriteBatchQuad(batch, x, y, w, h, AtlasCell(CELL_SOLID), color);
}
