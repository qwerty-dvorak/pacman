#ifndef CORE_BASE_H
#define CORE_BASE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ARRAY_COUNT(a)      (sizeof(a)/sizeof((a)[0]))
#define MIN(a, b)           (((a) < (b))? (a) : (b))
#define MAX(a, b)           (((a) > (b))? (a) : (b))
#define CLAMP(v, lo, hi)    MIN(MAX((v), (lo)), (hi))

#define LIKELY(x)           __builtin_expect(!!(x), 1)
#define UNLIKELY(x)         __builtin_expect(!!(x), 0)
#define FORCE_INLINE        static inline __attribute__((always_inline))

#define CACHE_LINE_SIZE     64

// Packed RGBA8 colour, byte order r, g, b, a in memory (little endian).
#define RGBA(r, g, b, a)    ((uint32_t)(r) | ((uint32_t)(g) << 8) | ((uint32_t)(b) << 16) | ((uint32_t)(a) << 24))

#endif // CORE_BASE_H
