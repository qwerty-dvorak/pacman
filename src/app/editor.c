#include "app/editor.h"

#include "scene/scene.h"
#include "raylib.h"

#include <stdio.h>
#include <string.h>

static const char tools[] = { TILE_WALL, TILE_DOT, TILE_ENERGIZER, TILE_EMPTY };
static const uint32_t palette[] = {
    RGBA(33, 33, 255, 255), RGBA(255, 135, 170, 255), RGBA(71, 183, 255, 255),
    RGBA(255, 183, 81, 255), RGBA(0, 200, 120, 255)
};

static void Recheck(Editor *editor)
{
    MazeBuild(&editor->maze, &editor->layout);
    editor->check = MazeValidate(&editor->layout);
    editor->dirty = true;
    editor->artDirty = true;
}

static void Snapshot(Editor *editor)
{
    if (editor->historyCount == EDITOR_HISTORY)
    {
        memmove(editor->history, editor->history + 1, (EDITOR_HISTORY - 1)*sizeof(MazeLayout));
        editor->historyCount--;
        editor->historyPosition--;
    }
    editor->historyCount = editor->historyPosition + 1;
    editor->history[editor->historyCount++] = editor->layout;
    editor->historyPosition = editor->historyCount - 1;
}

void EditorInit(Editor *editor, int slot)
{
    memset(editor, 0, sizeof(*editor));
    editor->slot = CLAMP(slot, 1, MAZE_SLOTS);
    editor->layout = mazeLayoutClassic;
    editor->wallColor = palette[0];
    editor->mirror = true;
    editor->cursorX = 1;
    editor->cursorY = 1;
    snprintf(editor->name, sizeof(editor->name), "CUSTOM %02d", editor->slot);
    MazeSlotLoad(editor->slot, &editor->layout, editor->name, sizeof(editor->name), &editor->wallColor);
    MazeBuild(&editor->maze, &editor->layout);
    editor->history[0] = editor->layout;
    editor->historyCount = 1;
    editor->check = MazeValidate(&editor->layout);
    editor->artDirty = true;
}

bool EditorSave(Editor *editor)
{
    if (!MazeSlotSave(editor->slot, &editor->layout, editor->name, editor->wallColor))
    {
        snprintf(editor->message, sizeof(editor->message), "SAVE FAILED - CHECK DATA DIRECTORY");
        return false;
    }
    editor->dirty = false;
    snprintf(editor->message, sizeof(editor->message), "SAVED SLOT %02d", editor->slot);
    return true;
}

static bool PutTile(Editor *editor, int x, int y, char tile)
{
    if ((unsigned)x >= MAZE_COLS || y <= 0 || y >= MAZE_ROWS - 1 || MazeIsLandmark(x, y)) return false;
    if (editor->layout.tiles[y][x] == tile) return false;
    editor->layout.tiles[y][x] = tile;
    return true;
}

static void Paint(Editor *editor, char tile)
{
    bool changed = PutTile(editor, editor->cursorX, editor->cursorY, tile);
    if (editor->mirror) changed |= PutTile(editor, MAZE_COLS - 1 - editor->cursorX, editor->cursorY, tile);
    if (changed)
    {
        Recheck(editor);
        Snapshot(editor);
    }
}

static void FillDots(Editor *editor)
{
    bool seen[MAZE_ROWS][MAZE_COLS] = { 0 };
    uint16_t queue[MAZE_ROWS*MAZE_COLS] = { 0 };
    int head = 0;
    int tail = 1;
    queue[0] = PAC_START_ROW*MAZE_COLS + CENTER_LEFT_COL;
    seen[PAC_START_ROW][CENTER_LEFT_COL] = true;
    while (head < tail)
    {
        int node = queue[head++];
        int x = node%MAZE_COLS;
        int y = node/MAZE_COLS;
        if (!MazeIsLandmark(x, y) && editor->layout.tiles[y][x] == TILE_EMPTY) editor->layout.tiles[y][x] = TILE_DOT;
        for (int d = 0; d < 4; d++)
        {
            int nx = (x + dirDx[d] + MAZE_COLS)%MAZE_COLS;
            int ny = y + dirDy[d];
            if ((unsigned)ny >= MAZE_ROWS || seen[ny][nx]) continue;
            char c = editor->layout.tiles[ny][nx];
            if (c == TILE_WALL || c == TILE_DOOR) continue;
            seen[ny][nx] = true;
            queue[tail++] = (uint16_t)(ny*MAZE_COLS + nx);
        }
    }
    Recheck(editor);
    Snapshot(editor);
}

static void Undo(Editor *editor, int direction)
{
    int next = editor->historyPosition + direction;
    if (next < 0 || next >= editor->historyCount) return;
    editor->historyPosition = next;
    editor->layout = editor->history[next];
    Recheck(editor);
}

static bool Press(int key) { return IsKeyPressed(key) || IsKeyPressedRepeat(key); }

EditorAction EditorUpdate(Editor *editor)
{
    if (editor->renaming)
    {
        int ch = 0;
        while ((ch = GetCharPressed()) != 0)
        {
            if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
            size_t n = strlen(editor->name);
            if (n + 1 < sizeof(editor->name) && ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == ' '))
            {
                editor->name[n] = (char)ch;
                editor->name[n + 1] = '\0';
                editor->dirty = true;
            }
        }
        if (Press(KEY_BACKSPACE) && editor->name[0] != '\0') editor->name[strlen(editor->name) - 1] = '\0';
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_ESCAPE)) editor->renaming = false;
        return EDITOR_CONTINUE;
    }
    if (IsKeyPressed(KEY_F2)) editor->renaming = true;
    if (IsKeyPressed(KEY_H)) editor->help = !editor->help;
    if (IsKeyPressed(KEY_ESCAPE))
    {
        if (editor->help) editor->help = false;
        else if (!editor->dirty || EditorSave(editor)) return EDITOR_EXIT;
    }
    if (editor->help) return EDITOR_CONTINUE;
    if (Press(KEY_LEFT)) editor->cursorX = MAX(0, editor->cursorX - 1);
    if (Press(KEY_RIGHT)) editor->cursorX = MIN(MAZE_COLS - 1, editor->cursorX + 1);
    if (Press(KEY_UP)) editor->cursorY = MAX(0, editor->cursorY - 1);
    if (Press(KEY_DOWN)) editor->cursorY = MIN(MAZE_ROWS - 1, editor->cursorY + 1);
    for (int i = 0; i < 4; i++) if (IsKeyPressed(KEY_ONE + i)) editor->tool = i;
    if (IsKeyPressed(KEY_TAB)) editor->tool = (editor->tool + 1)%4;
    if (IsKeyPressed(KEY_M)) editor->mirror = !editor->mirror;
    if (IsKeyPressed(KEY_K))
    {
        editor->palette = (editor->palette + 1)%(int)ARRAY_COUNT(palette);
        editor->wallColor = palette[editor->palette];
        editor->dirty = editor->artDirty = true;
    }
    if (IsKeyPressed(KEY_S)) EditorSave(editor);
    if (IsKeyPressed(KEY_Z)) Undo(editor, -1);
    if (IsKeyPressed(KEY_Y)) Undo(editor, 1);
    if (IsKeyPressed(KEY_F)) FillDots(editor);
    if (IsKeyPressed(KEY_C))
    {
        editor->layout = mazeLayoutClassic;
        Recheck(editor);
        Snapshot(editor);
    }
    int delta = IsKeyPressed(KEY_PAGE_DOWN) - IsKeyPressed(KEY_PAGE_UP);
    if (delta != 0 && (!editor->dirty || EditorSave(editor))) EditorInit(editor, CLAMP(editor->slot + delta, 1, MAZE_SLOTS));
    if (IsKeyPressed(KEY_T))
    {
        editor->check = MazeValidate(&editor->layout);
        if (editor->check.ok) return EDITOR_TEST;
        snprintf(editor->message, sizeof(editor->message), "%s", editor->check.message);
    }
    Vector2 mouse = GetMousePosition();
    Vector2 motion = GetMouseDelta();
    int width = GetRenderWidth();
    int height = GetRenderHeight();
    int scale = MAX(1, MIN(width/SCREEN_WIDTH, height/SCREEN_HEIGHT));
    int ox = (width - SCREEN_WIDTH*scale)/2;
    int oy = (height - SCREEN_HEIGHT*scale)/2;
    float mx = mouse.x*(float)width/(float)MAX(GetScreenWidth(), 1);
    float my = mouse.y*(float)height/(float)MAX(GetScreenHeight(), 1);
    int x = (int)((mx - (float)ox)/(float)(TILE_SIZE*scale));
    int y = (int)((my - (float)(oy + MAZE_OFFSET_Y*scale))/(float)(TILE_SIZE*scale));
    bool inside = mx >= (float)ox && my >= (float)(oy + MAZE_OFFSET_Y*scale) && (unsigned)x < MAZE_COLS && (unsigned)y < MAZE_ROWS;
    if (inside && (motion.x != 0.0f || motion.y != 0.0f || IsMouseButtonDown(MOUSE_BUTTON_LEFT) || IsMouseButtonDown(MOUSE_BUTTON_RIGHT)))
    {
        editor->cursorX = x;
        editor->cursorY = y;
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) Paint(editor, tools[editor->tool]);
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) Paint(editor, TILE_EMPTY);
    }
    if (Press(KEY_SPACE) || IsKeyPressed(KEY_ENTER)) Paint(editor, tools[editor->tool]);
    return EDITOR_CONTINUE;
}

void EditorDraw(Editor *editor, SpriteBatch *batch)
{
    SceneDrawMaze(batch, false);
    SceneDrawPellets(batch, &editor->maze);
    char text[40] = { 0 };
    snprintf(text, sizeof(text), "EDITOR %02d  %s", editor->slot, editor->name);
    SpriteBatchText(batch, 0, 0, text, COLOR_YELLOW);
    SpriteBatchText(batch, 0, 1, editor->renaming? "NAME - TYPE THEN ENTER" : "1 WALL 2 DOT 3 POWER 4 ERASE", COLOR_WHITE);
    SpriteBatchText(batch, 0, 2, editor->check.ok? "VALID - T TEST S SAVE H HELP" : editor->check.message, editor->check.ok? COLOR_CYAN : COLOR_RED);
    snprintf(text, sizeof(text), "TOOL %d MIRROR %s  H HELP", editor->tool + 1, editor->mirror? "ON" : "OFF");
    SpriteBatchText(batch, 0, 34, text, COLOR_YELLOW);
    SpriteBatchText(batch, 0, 35, editor->message, COLOR_WHITE);
    int s = batch->scale;
    int x = batch->originX + editor->cursorX*TILE_SIZE*s;
    int y = batch->originY + (MAZE_OFFSET_Y + editor->cursorY*TILE_SIZE)*s;
    int size = TILE_SIZE*s;
    uint32_t color = MazeIsLandmark(editor->cursorX, editor->cursorY)? COLOR_RED : COLOR_YELLOW;
    SpriteBatchRectPx(batch, x, y, size, s, color);
    SpriteBatchRectPx(batch, x, y + size - s, size, s, color);
    SpriteBatchRectPx(batch, x, y, s, size, color);
    SpriteBatchRectPx(batch, x + size - s, y, s, size, color);
    if (editor->help)
    {
        SpriteBatchRectPx(batch, batch->originX, batch->originY + 64*s, SCREEN_WIDTH*s, 152*s, RGBA(0, 0, 0, 255));
        const char *lines[] = { "LEVEL EDITOR", "MOUSE OR ARROWS - CURSOR", "CLICK OR SPACE - PAINT", "RIGHT CLICK - ERASE", "1-4 OR TAB - TOOL", "M MIRROR  K WALL COLOR", "F FILL REACHABLE DOTS", "Z UNDO  Y REDO", "C CLASSIC TEMPLATE", "PAGE UP/DOWN - SLOT", "F2 NAME  S SAVE  T TEST", "ESC BACK - AUTOSAVE", "HOUSE AND START ARE LOCKED" };
        for (size_t i = 0; i < ARRAY_COUNT(lines); i++) SpriteBatchText(batch, 1, 9 + (int)i, lines[i], i == 0? COLOR_YELLOW : COLOR_WHITE);
    }
}
