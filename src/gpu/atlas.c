#include "gpu/atlas.h"

#include "scene/atlas_layout.h"

#include "raylib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Source coordinates, measured from the files in assets/sprites.
#define SHEET_FILE              "general_sprites.png"
#define SHEET_EMPTY_MAZE_X      228
#define SHEET_FILLED_MAZE_X     0
#define SHEET_SPRITES_X         456

#define FONT_FILE               "all_assets_palettes.png"
#define FONT_ORIGIN_X           201             // 8x8 glyph cells on a 9 px pitch (1 px magenta grid).
#define FONT_ORIGIN_Y           1
#define FONT_PITCH              9

typedef struct Pixels {
    const uint8_t *data;        // RGBA8
    int width;
    int height;
} Pixels;

typedef struct FontGlyph {
    char ch;
    uint8_t row;
    uint8_t col;
} FontGlyph;

static const FontGlyph fontGlyphs[] = {
    { '/', 1, 10 }, { '-', 1, 11 }, { '.', 1, 12 }, { '"', 2, 10 }, { '!', 2, 12 },
};

static bool LoadPixels(const char *dir, const char *file, Image *image, Pixels *pixels)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", dir, file);
    *image = LoadImage(path);
    if (image->data == NULL)
    {
        fprintf(stderr, "atlas: cannot load %s\n", path);
        return false;
    }
    ImageFormat(image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    *pixels = (Pixels){ image->data, image->width, image->height };
    return true;
}

FORCE_INLINE const uint8_t *PixelAt(const Pixels *p, int x, int y) { return p->data + ((size_t)y*(size_t)p->width + (size_t)x)*4; }
FORCE_INLINE uint8_t *AtlasAt(uint8_t *atlas, int x, int y) { return atlas + ((size_t)y*ATLAS_SIZE + (size_t)x)*4; }

static void CopyOpaque(uint8_t *atlas, int dx, int dy, const Pixels *src, int sx, int sy, int w, int h)
{
    for (int y = 0; y < h; y++) memcpy(AtlasAt(atlas, dx, dy + y), PixelAt(src, sx, sy + y), (size_t)w*4);
}

// Black becomes transparent; everything else is kept as is.
static void CopyKeyed(uint8_t *atlas, int dx, int dy, const Pixels *src, int sx, int sy, int w, int h)
{
    for (int y = 0; y < h; y++)
    {
        const uint8_t *s = PixelAt(src, sx, sy + y);
        uint8_t *d = AtlasAt(atlas, dx, dy + y);
        for (int x = 0; x < w; x++, s += 4, d += 4)
        {
            const bool lit = (s[0] | s[1] | s[2]) > 16;
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
            d[3] = lit? 255 : 0;
        }
    }
}

// Blue walls -> white, for the level-complete flash.
static void WhitenWalls(uint8_t *atlas, int x0, int y0, int w, int h)
{
    for (int y = 0; y < h; y++)
    {
        uint8_t *p = AtlasAt(atlas, x0, y0 + y);
        for (int x = 0; x < w; x++, p += 4)
        {
            if ((p[2] > 128) && (p[0] < 128)) p[0] = p[1] = p[2] = 255;
        }
    }
}

// Converts a palette-tinted glyph into a white coverage mask so vertex colour can tint it.
static void CopyGlyph(uint8_t *atlas, int cell, const Pixels *font, int row, int col)
{
    const AtlasRect dst = AtlasCell(cell);
    const int sx = FONT_ORIGIN_X + col*FONT_PITCH;
    const int sy = FONT_ORIGIN_Y + row*FONT_PITCH;
    for (int y = 0; y < CELL_SIZE; y++)
    {
        const uint8_t *s = PixelAt(font, sx, sy + y);
        uint8_t *d = AtlasAt(atlas, dst.x, dst.y + y);
        for (int x = 0; x < CELL_SIZE; x++, s += 4, d += 4)
        {
            const bool magenta = (s[0] > 200) && (s[1] < 60) && (s[2] > 200);
            const bool lit = !magenta && ((s[0] | s[1] | s[2]) > 16);
            const uint8_t v = lit? 255 : 0;
            d[0] = d[1] = d[2] = d[3] = v;
        }
    }
}

// Small UI punctuation absent from the original arcade letter set.
static void BuildUiGlyph(uint8_t *atlas, char ch, const uint8_t rows[8])
{
    const AtlasRect dst = AtlasCell(ch);
    for (int y = 0; y < CELL_SIZE; y++)
    {
        for (int x = 0; x < CELL_SIZE; x++)
        {
            if ((rows[y] & (1u << (7 - x))) != 0) memset(AtlasAt(atlas, dst.x + x, dst.y + y), 255, 4);
        }
    }
}

static void BuildCells(uint8_t *atlas, const Pixels *sheet, const Pixels *font)
{
    for (int i = 0; i < 10; i++) CopyGlyph(atlas, '0' + i, font, 2, i);
    for (int i = 0; i < 13; i++) CopyGlyph(atlas, 'A' + i, font, 3, i);
    for (int i = 0; i < 13; i++) CopyGlyph(atlas, 'N' + i, font, 4, i);
    for (size_t i = 0; i < ARRAY_COUNT(fontGlyphs); i++) CopyGlyph(atlas, fontGlyphs[i].ch, font, fontGlyphs[i].row, fontGlyphs[i].col);
    CopyGlyph(atlas, CELL_COPYRIGHT, font, 2, 11);
    const uint8_t colon[8] = { 0, 0x18, 0x18, 0, 0x18, 0x18, 0, 0 };
    const uint8_t arrow[8] = { 0x40, 0x60, 0x30, 0x18, 0x30, 0x60, 0x40, 0 };
    BuildUiGlyph(atlas, ':', colon);
    BuildUiGlyph(atlas, '>', arrow);

    // Pellets are cut from the filled maze: tile (1,1) holds a dot, tile (1,3) an energizer.
    const AtlasRect dot = AtlasCell(CELL_DOT);
    const AtlasRect energizer = AtlasCell(CELL_ENERGIZER);
    CopyKeyed(atlas, dot.x, dot.y, sheet, SHEET_FILLED_MAZE_X + 1*TILE_SIZE, 1*TILE_SIZE, CELL_SIZE, CELL_SIZE);
    CopyKeyed(atlas, energizer.x, energizer.y, sheet, SHEET_FILLED_MAZE_X + 1*TILE_SIZE, 3*TILE_SIZE, CELL_SIZE, CELL_SIZE);

    const AtlasRect solid = AtlasCell(CELL_SOLID);
    for (int y = 0; y < CELL_SIZE; y++) memset(AtlasAt(atlas, solid.x, solid.y + y), 255, CELL_SIZE*4);
}

uint8_t *AtlasBuild(const char *spriteDir)
{
    Image sheetImage = { 0 };
    Image fontImage = { 0 };
    Pixels sheet = { 0 };
    Pixels font = { 0 };
    uint8_t *atlas = NULL;

    if (!LoadPixels(spriteDir, SHEET_FILE, &sheetImage, &sheet) || !LoadPixels(spriteDir, FONT_FILE, &fontImage, &font)) goto done;
    if ((sheet.width < SHEET_SPRITES_X + ATLAS_SPRITES_W) || (sheet.height < MAZE_HEIGHT) || (font.width < 400) || (font.height < 60))
    {
        fprintf(stderr, "atlas: unexpected sprite sheet dimensions\n");
        goto done;
    }

    atlas = calloc((size_t)ATLAS_SIZE*ATLAS_SIZE, 4);
    if (atlas == NULL) goto done;

    CopyOpaque(atlas, ATLAS_MAZE_X, ATLAS_MAZE_Y, &sheet, SHEET_EMPTY_MAZE_X, 0, MAZE_WIDTH, MAZE_HEIGHT);
    CopyOpaque(atlas, ATLAS_MAZE_FLASH_X, ATLAS_MAZE_Y, &sheet, SHEET_EMPTY_MAZE_X, 0, MAZE_WIDTH, MAZE_HEIGHT);
    WhitenWalls(atlas, ATLAS_MAZE_FLASH_X, ATLAS_MAZE_Y, MAZE_WIDTH, MAZE_HEIGHT);
    CopyKeyed(atlas, ATLAS_SPRITES_X, ATLAS_SPRITES_Y, &sheet, SHEET_SPRITES_X, 0, ATLAS_SPRITES_W, ATLAS_SPRITES_H);
    BuildCells(atlas, &sheet, &font);

done:
    if (sheetImage.data != NULL) UnloadImage(sheetImage);
    if (fontImage.data != NULL) UnloadImage(fontImage);
    return atlas;
}
