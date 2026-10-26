#include "gpu/gpu_renderer.h"

#include "core/clock.h"
#include "gpu/atlas.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SLOT_VERTICES       (GPU_MAX_QUADS*4)
#define SLOT_BYTES          ((GLsizeiptr)SLOT_VERTICES*(GLsizeiptr)sizeof(Vertex))
#define FENCE_TIMEOUT_NS    (NS_PER_SECOND/10)

// Positions arrive in framebuffer pixels; texels are fetched exactly with texelFetch, so
// there is no filtering, no normalisation and no seams at any integer scale. Transparent
// texels are discarded, which lets blending stay disabled.
static const char *vertexShaderCode =
    "#version 450 core\n"
    "layout(location = 0) in vec2 aPosition;\n"
    "layout(location = 1) in vec2 aTexel;\n"
    "layout(location = 2) in vec4 aColor;\n"
    "layout(location = 0) uniform vec2 uInvHalfScreen;\n"
    "layout(location = 0) out vec2 vTexel;\n"
    "layout(location = 1) flat out vec4 vColor;\n"
    "void main()\n"
    "{\n"
    "    vTexel = aTexel;\n"
    "    vColor = aColor;\n"
    "    gl_Position = vec4(aPosition.x*uInvHalfScreen.x - 1.0, 1.0 - aPosition.y*uInvHalfScreen.y, 0.0, 1.0);\n"
    "}\n";

static const char *fragmentShaderCode =
    "#version 450 core\n"
    "layout(binding = 0) uniform sampler2D uAtlas;\n"
    "layout(location = 0) in vec2 vTexel;\n"
    "layout(location = 1) flat in vec4 vColor;\n"
    "layout(location = 0) out vec4 fragColor;\n"
    "void main()\n"
    "{\n"
    "    vec4 texel = texelFetch(uAtlas, ivec2(vTexel), 0);\n"
    "    if (texel.a == 0.0) discard;\n"
    "    fragColor = texel*vColor;\n"
    "}\n";

static GLuint CompileShader(GLenum type, const char *source)
{
    const GLuint shader = gl.CreateShader(type);
    gl.ShaderSource(shader, 1, &source, NULL);
    gl.CompileShader(shader);

    GLint ok = GL_FALSE;
    gl.GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[1024];
        gl.GetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "gpu: shader compile failed:\n%s\n", log);
        gl.DeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint LinkProgram(void)
{
    const GLuint vs = CompileShader(GL_VERTEX_SHADER, vertexShaderCode);
    const GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fragmentShaderCode);
    GLuint program = 0;
    if ((vs != 0) && (fs != 0))
    {
        program = gl.CreateProgram();
        gl.AttachShader(program, vs);
        gl.AttachShader(program, fs);
        gl.LinkProgram(program);

        GLint ok = GL_FALSE;
        gl.GetProgramiv(program, GL_LINK_STATUS, &ok);
        if (!ok)
        {
            char log[1024];
            gl.GetProgramInfoLog(program, sizeof(log), NULL, log);
            fprintf(stderr, "gpu: program link failed:\n%s\n", log);
            gl.DeleteProgram(program);
            program = 0;
        }
    }
    if (vs != 0) gl.DeleteShader(vs);
    if (fs != 0) gl.DeleteShader(fs);
    return program;
}

// Copies a MAZE_WIDTH x MAZE_HEIGHT block out of the atlas.
static uint8_t *ExtractMaze(const uint8_t *atlas, int x0)
{
    uint8_t *pixels = malloc(MAZE_PIXELS_BYTES);
    if (pixels == NULL) return NULL;
    for (int y = 0; y < MAZE_HEIGHT; y++)
    {
        memcpy(pixels + (size_t)y*MAZE_WIDTH*4, atlas + ((size_t)(ATLAS_MAZE_Y + y)*ATLAS_SIZE + (size_t)x0)*4, (size_t)MAZE_WIDTH*4);
    }
    return pixels;
}

static bool CreateAtlasTexture(GpuRenderer *gpu, const char *spriteDir)
{
    uint8_t *pixels = AtlasBuild(spriteDir);
    if (pixels == NULL) return false;

    gpu->classicMaze[0] = ExtractMaze(pixels, ATLAS_MAZE_X);
    gpu->classicMaze[1] = ExtractMaze(pixels, ATLAS_MAZE_FLASH_X);
    gl.CreateTextures(GL_TEXTURE_2D, 1, &gpu->texture);
    gl.TextureStorage2D(gpu->texture, 1, GL_RGBA8, ATLAS_SIZE, ATLAS_SIZE);
    gl.TextureSubImage2D(gpu->texture, 0, 0, 0, ATLAS_SIZE, ATLAS_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    free(pixels);
    return (gpu->classicMaze[0] != NULL) && (gpu->classicMaze[1] != NULL);
}

void GpuRendererUploadMaze(GpuRenderer *gpu, const uint8_t *normal, const uint8_t *flash)
{
    gl.TextureSubImage2D(gpu->texture, 0, ATLAS_MAZE_X, ATLAS_MAZE_Y, MAZE_WIDTH, MAZE_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, normal);
    gl.TextureSubImage2D(gpu->texture, 0, ATLAS_MAZE_FLASH_X, ATLAS_MAZE_Y, MAZE_WIDTH, MAZE_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, flash);
}

static bool CreateBuffers(GpuRenderer *gpu)
{
    uint16_t *indices = malloc((size_t)GPU_MAX_QUADS*6*sizeof(uint16_t));
    if (indices == NULL) return false;

    // Counter-clockwise quads: TL, BL, BR, TR.
    for (int q = 0; q < GPU_MAX_QUADS; q++)
    {
        const uint16_t base = (uint16_t)(q*4);
        uint16_t *i = &indices[q*6];
        i[0] = base; i[1] = (uint16_t)(base + 1); i[2] = (uint16_t)(base + 2);
        i[3] = base; i[4] = (uint16_t)(base + 2); i[5] = (uint16_t)(base + 3);
    }
    gl.CreateBuffers(1, &gpu->indexBuffer);
    gl.NamedBufferStorage(gpu->indexBuffer, (GLsizeiptr)GPU_MAX_QUADS*6*(GLsizeiptr)sizeof(uint16_t), indices, 0);
    free(indices);

    const GLbitfield flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
    gl.CreateBuffers(1, &gpu->vertexBuffer);
    gl.NamedBufferStorage(gpu->vertexBuffer, SLOT_BYTES*GPU_RING, NULL, flags);
    gpu->mapped = gl.MapNamedBufferRange(gpu->vertexBuffer, 0, SLOT_BYTES*GPU_RING, flags);
    if (gpu->mapped == NULL) return false;

    gl.CreateVertexArrays(1, &gpu->vao);
    gl.VertexArrayVertexBuffer(gpu->vao, 0, gpu->vertexBuffer, 0, sizeof(Vertex));
    gl.VertexArrayElementBuffer(gpu->vao, gpu->indexBuffer);
    gl.VertexArrayAttribFormat(gpu->vao, 0, 2, GL_SHORT, GL_FALSE, offsetof(Vertex, x));
    gl.VertexArrayAttribFormat(gpu->vao, 1, 2, GL_UNSIGNED_SHORT, GL_FALSE, offsetof(Vertex, u));
    gl.VertexArrayAttribFormat(gpu->vao, 2, 4, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(Vertex, color));
    for (GLuint attrib = 0; attrib < 3; attrib++)
    {
        gl.VertexArrayAttribBinding(gpu->vao, attrib, 0);
        gl.EnableVertexArrayAttrib(gpu->vao, attrib);
    }
    return true;
}

bool GpuRendererInit(GpuRenderer *gpu, const char *spriteDir)
{
    memset(gpu, 0, sizeof(*gpu));
    if (!GlLoad()) return false;
    if (!CreateAtlasTexture(gpu, spriteDir)) return false;

    gpu->program = LinkProgram();
    if (gpu->program == 0) return false;
    gpu->locInvHalfScreen = gl.GetUniformLocation(gpu->program, "uInvHalfScreen");

    if (!CreateBuffers(gpu)) return false;

    // The pipeline never changes: bind everything once. raylib draws nothing itself
    // (custom frame control), so this state persists for the program's lifetime.
    gl.Disable(GL_BLEND);
    gl.Disable(GL_DEPTH_TEST);
    gl.Disable(GL_CULL_FACE);
    gl.Disable(GL_DITHER);
    gl.UseProgram(gpu->program);
    gl.BindVertexArray(gpu->vao);
    gl.BindTextureUnit(0, gpu->texture);

    GpuTimerInit(&gpu->timer);
    return true;
}

void GpuRendererShutdown(GpuRenderer *gpu)
{
    if (gl.DeleteSync != NULL)
    {
        for (int i = 0; i < GPU_RING; i++)
        {
            if (gpu->fences[i] != NULL) gl.DeleteSync(gpu->fences[i]);
        }
    }
    if (gpu->vao != 0)
    {
        GpuTimerShutdown(&gpu->timer);
        gl.DeleteVertexArrays(1, &gpu->vao);
    }
    if (gpu->mapped != NULL) gl.UnmapNamedBuffer(gpu->vertexBuffer);
    if (gpu->vertexBuffer != 0) gl.DeleteBuffers(1, &gpu->vertexBuffer);
    if (gpu->indexBuffer != 0) gl.DeleteBuffers(1, &gpu->indexBuffer);
    if (gpu->program != 0) gl.DeleteProgram(gpu->program);
    if (gpu->texture != 0) gl.DeleteTextures(1, &gpu->texture);
    free(gpu->classicMaze[0]);
    free(gpu->classicMaze[1]);
    memset(gpu, 0, sizeof(*gpu));
}

uint64_t GpuRendererAcquire(GpuRenderer *gpu, SpriteBatch *batch, int framebufferWidth, int framebufferHeight, const StaticLayer *layer)
{
    const uint32_t slot = gpu->frame%GPU_RING;
    uint64_t waited = 0;

    GLsync fence = gpu->fences[slot];
    if (fence != NULL)
    {
        const uint64_t t0 = ClockNowNs();
        GLbitfield flags = GL_SYNC_FLUSH_COMMANDS_BIT;
        while (gl.ClientWaitSync(fence, flags, FENCE_TIMEOUT_NS) == GL_TIMEOUT_EXPIRED) flags = 0;
        gl.DeleteSync(fence);
        gpu->fences[slot] = NULL;
        waited = ClockNowNs() - t0;
    }

    Vertex *base = gpu->mapped + (size_t)slot*SLOT_VERTICES;
    SpriteBatchBegin(batch, base, GPU_MAX_QUADS, framebufferWidth, framebufferHeight);
    if (layer == NULL)
    {
        gpu->slotLayerVersion[slot] = 0;
        return waited;
    }

    // The slot keeps the layer from GPU_RING frames ago; recopy only if it changed since.
    if (gpu->slotLayerVersion[slot] != layer->version)
    {
        memcpy(base, layer->vertices, (size_t)layer->quadCount*4*sizeof(Vertex));
        gpu->slotLayerVersion[slot] = layer->version;
    }
    batch->quadCount = layer->quadCount;
    return waited;
}

void GpuRendererSubmit(GpuRenderer *gpu, const SpriteBatch *batch)
{
    const uint32_t slot = gpu->frame%GPU_RING;

    GpuTimerBegin(&gpu->timer);
    if ((batch->framebufferWidth != gpu->viewportWidth) || (batch->framebufferHeight != gpu->viewportHeight))
    {
        gpu->viewportWidth = batch->framebufferWidth;
        gpu->viewportHeight = batch->framebufferHeight;
        gl.Viewport(0, 0, gpu->viewportWidth, gpu->viewportHeight);
        gl.ProgramUniform2f(gpu->program, gpu->locInvHalfScreen, 2.0f/(float)gpu->viewportWidth, 2.0f/(float)gpu->viewportHeight);
    }

    if (batch->quadCount > 0)
    {
        gl.DrawElementsBaseVertex(GL_TRIANGLES, batch->quadCount*6, GL_UNSIGNED_SHORT, NULL, (GLint)(slot*SLOT_VERTICES));
        gpu->fences[slot] = gl.FenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    }
    GpuTimerEnd(&gpu->timer);
    gpu->frame++;
}
