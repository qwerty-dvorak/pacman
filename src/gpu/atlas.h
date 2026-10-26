#ifndef GPU_ATLAS_H
#define GPU_ATLAS_H

#include <stdint.h>

// Decodes the PNGs in spriteDir with raylib and composes the RGBA8 atlas described by
// scene/atlas_layout.h. Returns NULL on failure; the caller releases it with free().
uint8_t *AtlasBuild(const char *spriteDir);

#endif // GPU_ATLAS_H
