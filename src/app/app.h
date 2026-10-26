#ifndef APP_APP_H
#define APP_APP_H

#include "core/base.h"

typedef enum WindowMode {
    WINDOW_FIXED = 0,       // Exact integer-scaled arcade size; tiling WMs float fixed-size windows.
    WINDOW_RESIZABLE,       // Letterboxed integer scaling inside any size.
    WINDOW_FULLSCREEN       // Exclusive fullscreen at the monitor's resolution (page flipping).
} WindowMode;

typedef struct AppOptions {
    WindowMode windowMode;
    int scale;              // Window scale; 0 picks the largest that fits the monitor.
    bool vsync;
    bool overlay;           // Performance overlay (toggle with F1).
    bool autoplay;          // Computer-controlled Pac-Man.
    double benchSeconds;    // > 0: autoplay, measure every frame, print a report and exit.
    uint32_t seed;
} AppOptions;

int AppRun(const AppOptions *options);

#endif // APP_APP_H
