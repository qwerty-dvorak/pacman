#include "gpu/gl.h"

#include <stdio.h>

typedef void (*GlProc)(void);
extern GlProc glfwGetProcAddress(const char *name);

GlApi gl;

bool GlLoad(void)
{
    bool ok = true;
#define GL_RESOLVE(ret, name, ...) \
    gl.name = (ret (*)(__VA_ARGS__))glfwGetProcAddress("gl" #name); \
    if (gl.name == NULL) { fprintf(stderr, "gl: missing entry point gl%s\n", #name); ok = false; }
    GL_FUNCTIONS(GL_RESOLVE)
#undef GL_RESOLVE
    if (!ok) return false;

    int major = 0;
    int minor = 0;
    const char *version = (const char *)gl.GetString(GL_VERSION);
    if ((version == NULL) || (sscanf(version, "%d.%d", &major, &minor) != 2) || (major*10 + minor < 45))
    {
        fprintf(stderr, "gl: OpenGL 4.5 required, context reports %s\n", (version != NULL)? version : "nothing");
        return false;
    }
    return true;
}
