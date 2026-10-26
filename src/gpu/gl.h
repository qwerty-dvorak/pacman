#ifndef GPU_GL_H
#define GPU_GL_H

#include "core/base.h"

// Minimal OpenGL 4.5 core loader: exactly the entry points this renderer uses, resolved
// through GLFW (linked into raylib) once raylib has created the context. Direct state
// access, immutable storage and persistent mapping are all core in 4.5.

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef unsigned int GLbitfield;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLboolean;
typedef unsigned char GLubyte;
typedef float GLfloat;
typedef char GLchar;
typedef intptr_t GLintptr;
typedef ptrdiff_t GLsizeiptr;
typedef uint64_t GLuint64;
typedef struct __GLsync *GLsync;

#define GL_FALSE                        0
#define GL_TRUE                         1
#define GL_UNSIGNED_BYTE                0x1401
#define GL_SHORT                        0x1402
#define GL_UNSIGNED_SHORT               0x1403
#define GL_TRIANGLES                    0x0004
#define GL_COLOR_BUFFER_BIT             0x00004000
#define GL_BLEND                        0x0BE2
#define GL_DEPTH_TEST                   0x0B71
#define GL_CULL_FACE                    0x0B44
#define GL_DITHER                       0x0BD0
#define GL_VERSION                      0x1F02
#define GL_RENDERER                     0x1F01
#define GL_TEXTURE_2D                   0x0DE1
#define GL_RGBA                         0x1908
#define GL_RGBA8                        0x8058
#define GL_VERTEX_SHADER                0x8B31
#define GL_FRAGMENT_SHADER              0x8B30
#define GL_COMPILE_STATUS               0x8B81
#define GL_LINK_STATUS                  0x8B82
#define GL_MAP_WRITE_BIT                0x0002
#define GL_MAP_PERSISTENT_BIT           0x0040
#define GL_MAP_COHERENT_BIT             0x0080
#define GL_MAP_FLUSH_EXPLICIT_BIT       0x0010
#define GL_SYNC_GPU_COMMANDS_COMPLETE   0x9117
#define GL_SYNC_FLUSH_COMMANDS_BIT      0x00000001
#define GL_ALREADY_SIGNALED             0x911A
#define GL_TIMEOUT_EXPIRED              0x911B
#define GL_CONDITION_SATISFIED          0x911C
#define GL_WAIT_FAILED                  0x911D
#define GL_TIME_ELAPSED                 0x88BF
#define GL_QUERY_RESULT                 0x8866
#define GL_QUERY_RESULT_AVAILABLE       0x8867

#define GL_FUNCTIONS(X) \
    X(const GLubyte *, GetString, GLenum name) \
    X(void, Enable, GLenum cap) \
    X(void, Disable, GLenum cap) \
    X(void, Viewport, GLint x, GLint y, GLsizei width, GLsizei height) \
    X(void, ClearColor, GLfloat r, GLfloat g, GLfloat b, GLfloat a) \
    X(void, Clear, GLbitfield mask) \
    X(void, CreateBuffers, GLsizei n, GLuint *buffers) \
    X(void, DeleteBuffers, GLsizei n, const GLuint *buffers) \
    X(void, NamedBufferStorage, GLuint buffer, GLsizeiptr size, const void *data, GLbitfield flags) \
    X(void *, MapNamedBufferRange, GLuint buffer, GLintptr offset, GLsizeiptr length, GLbitfield access) \
    X(void, FlushMappedNamedBufferRange, GLuint buffer, GLintptr offset, GLsizeiptr length) \
    X(GLboolean, UnmapNamedBuffer, GLuint buffer) \
    X(void, CreateVertexArrays, GLsizei n, GLuint *arrays) \
    X(void, DeleteVertexArrays, GLsizei n, const GLuint *arrays) \
    X(void, BindVertexArray, GLuint array) \
    X(void, VertexArrayVertexBuffer, GLuint vaobj, GLuint bindingindex, GLuint buffer, GLintptr offset, GLsizei stride) \
    X(void, VertexArrayElementBuffer, GLuint vaobj, GLuint buffer) \
    X(void, EnableVertexArrayAttrib, GLuint vaobj, GLuint index) \
    X(void, VertexArrayAttribFormat, GLuint vaobj, GLuint attribindex, GLint size, GLenum type, GLboolean normalized, GLuint relativeoffset) \
    X(void, VertexArrayAttribBinding, GLuint vaobj, GLuint attribindex, GLuint bindingindex) \
    X(void, DrawElementsBaseVertex, GLenum mode, GLsizei count, GLenum type, const void *indices, GLint basevertex) \
    X(GLuint, CreateShader, GLenum type) \
    X(void, ShaderSource, GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length) \
    X(void, CompileShader, GLuint shader) \
    X(void, GetShaderiv, GLuint shader, GLenum pname, GLint *params) \
    X(void, GetShaderInfoLog, GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog) \
    X(void, DeleteShader, GLuint shader) \
    X(GLuint, CreateProgram, void) \
    X(void, AttachShader, GLuint program, GLuint shader) \
    X(void, LinkProgram, GLuint program) \
    X(void, GetProgramiv, GLuint program, GLenum pname, GLint *params) \
    X(void, GetProgramInfoLog, GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog) \
    X(void, DeleteProgram, GLuint program) \
    X(void, UseProgram, GLuint program) \
    X(GLint, GetUniformLocation, GLuint program, const GLchar *name) \
    X(void, ProgramUniform2f, GLuint program, GLint location, GLfloat v0, GLfloat v1) \
    X(void, CreateTextures, GLenum target, GLsizei n, GLuint *textures) \
    X(void, DeleteTextures, GLsizei n, const GLuint *textures) \
    X(void, TextureStorage2D, GLuint texture, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height) \
    X(void, TextureSubImage2D, GLuint texture, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels) \
    X(void, BindTextureUnit, GLuint unit, GLuint texture) \
    X(GLsync, FenceSync, GLenum condition, GLbitfield flags) \
    X(GLenum, ClientWaitSync, GLsync sync, GLbitfield flags, GLuint64 timeout) \
    X(void, DeleteSync, GLsync sync) \
    X(void, GenQueries, GLsizei n, GLuint *ids) \
    X(void, DeleteQueries, GLsizei n, const GLuint *ids) \
    X(void, BeginQuery, GLenum target, GLuint id) \
    X(void, EndQuery, GLenum target) \
    X(void, GetQueryObjectuiv, GLuint id, GLenum pname, GLuint *params) \
    X(void, GetQueryObjectui64v, GLuint id, GLenum pname, GLuint64 *params)

// Function pointers live in one table (gl.Clear(...)) so they can never collide with the
// libGL symbols GLFW loads at run time.
#define GL_DECLARE_MEMBER(ret, name, ...) ret (*name)(__VA_ARGS__);
typedef struct GlApi {
    GL_FUNCTIONS(GL_DECLARE_MEMBER)
} GlApi;
#undef GL_DECLARE_MEMBER

extern GlApi gl;

// Resolves every entry point and checks for a 4.5 context. Call after InitWindow().
bool GlLoad(void);

#endif // GPU_GL_H
