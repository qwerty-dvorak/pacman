#ifndef SCENE_SCENE_H
#define SCENE_SCENE_H

#include "game/game.h"
#include "scene/sprite_batch.h"

// Turns game state into quads. Pure CPU work with no graphics API calls. The renderer never
// clears, so every frame must start with opaque quads covering the whole framebuffer:
// either the game's static layer or SceneDrawBackdrop.

#define STATIC_LAYER_QUADS  1024

// Quads that change only when a dot is eaten, the maze flashes or the window resizes:
// background bands, maze image and dots. Rebuilt on change; the GPU copies it into its
// ring slots only when the version differs, so a typical frame writes no static vertices.
typedef struct StaticLayer {
    Vertex vertices[STATIC_LAYER_QUADS*4];
    int quadCount;
    uint32_t version;               // 0 = empty. Unique across layers.

    // Inputs the content was built from.
    uint32_t pellets[MAZE_ROWS];
    int framebufferWidth;
    int framebufferHeight;
    bool flashMaze;
} StaticLayer;

void SceneUpdateStaticLayer(StaticLayer *layer, const Game *game, int framebufferWidth, int framebufferHeight);

// Energizers, fruit, actors and HUD, interpolated by alpha in [0,1) between the previous
// and the current tick. Drawn after the static layer.
void SceneDrawDynamic(SpriteBatch *batch, const Game *game, float alpha);

// Building blocks for other screens (menus, editor).
void SceneDrawBackdrop(SpriteBatch *batch);                 // Opaque black over the framebuffer.
void SceneDrawMaze(SpriteBatch *batch, bool flash);         // HUD bands, letterbox and maze image.
void SceneDrawPellets(SpriteBatch *batch, const Maze *maze);

#endif // SCENE_SCENE_H
