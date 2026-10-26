#ifndef GPU_GPU_RENDERER_H
#define GPU_GPU_RENDERER_H

#include "gpu/gl.h"
#include "gpu/gpu_timer.h"
#include "scene/scene.h"

#define GPU_RING            3               // Frames in flight sharing the persistent vertex buffer.
#define GPU_MAX_QUADS       2048

_Static_assert(GPU_MAX_QUADS*4 <= 65536, "Quad indices are 16-bit");

// OpenGL 4.5 sprite pipeline. One atlas texture, one program, one VAO and one persistently
// mapped vertex buffer split into GPU_RING slots guarded by fences. The CPU writes vertices
// straight into the mapped slot, so a frame costs one glDrawElementsBaseVertex and one
// fence: no uploads, no binds, no state changes, and no clear (see GpuRendererSubmit).
typedef struct GpuRenderer {
    GLuint texture;
    GLuint program;
    GLuint vao;
    GLuint vertexBuffer;
    GLuint indexBuffer;
    GLint locInvHalfScreen;

    Vertex *mapped;
    GLsync fences[GPU_RING];
    uint32_t slotLayerVersion[GPU_RING];    // StaticLayer version each slot currently holds.
    uint32_t frame;
    int viewportWidth;
    int viewportHeight;

    // The original maze art (normal, flash), kept to restore it after a generated maze.
    uint8_t *classicMaze[2];

    GpuTimer timer;
} GpuRenderer;

#define MAZE_PIXELS_BYTES   ((size_t)MAZE_WIDTH*MAZE_HEIGHT*4)

// Requires the GL context created by InitWindow().
bool GpuRendererInit(GpuRenderer *gpu, const char *spriteDir);
void GpuRendererShutdown(GpuRenderer *gpu);

// Replaces the maze images in the atlas (RGBA8, MAZE_WIDTH x MAZE_HEIGHT each).
void GpuRendererUploadMaze(GpuRenderer *gpu, const uint8_t *normal, const uint8_t *flash);

// Waits (rarely) until the next ring slot is no longer read by the GPU, refreshes the
// slot's copy of layer if it is stale (layer may be NULL), and points the batch at the
// free space after it. Returns the nanoseconds spent waiting on the fence.
uint64_t GpuRendererAcquire(GpuRenderer *gpu, SpriteBatch *batch, int framebufferWidth, int framebufferHeight, const StaticLayer *layer);

// Draws the batch. The framebuffer is never cleared: the batch must cover every pixel
// with opaque quads, which saves a full-surface clear and its resolve.
void GpuRendererSubmit(GpuRenderer *gpu, const SpriteBatch *batch);

#endif // GPU_GPU_RENDERER_H
