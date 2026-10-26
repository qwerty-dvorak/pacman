#ifndef APP_EDITOR_H
#define APP_EDITOR_H

#include "app/storage.h"
#include "scene/sprite_batch.h"

#define EDITOR_HISTORY 64

typedef enum EditorAction {
    EDITOR_CONTINUE = 0,
    EDITOR_EXIT,
    EDITOR_TEST
} EditorAction;

typedef struct Editor {
    MazeLayout layout;
    char name[MAZE_NAME_SIZE];
    Maze maze;
    uint32_t wallColor;
    bool dirty;
    bool artDirty;
    int slot;
    int cursorX;
    int cursorY;
    int tool;
    int palette;
    bool mirror;
    bool help;
    bool painting;
    bool renaming;
    char message[40];
    MazeCheck check;
    MazeLayout history[EDITOR_HISTORY];
    int historyCount;
    int historyPosition;
    int viewWidth;
    int viewHeight;
} Editor;

void EditorInit(Editor *editor, int slot);
EditorAction EditorUpdate(Editor *editor);
void EditorDraw(Editor *editor, SpriteBatch *batch);
bool EditorSave(Editor *editor);

#endif
