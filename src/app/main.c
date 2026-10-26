#include "app/app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void PrintUsage(const char *program)
{
    fprintf(stderr,
            "usage: %s [options]\n"
            "  --scale N          window scale (default: largest that fits)\n"
            "  --window MODE      fixed | resizable | fullscreen (default: fixed)\n"
            "  --vsync            cap to the display refresh rate (default: uncapped)\n"
            "  --no-overlay       hide the performance overlay (F1 toggles)\n"
            "  --autoplay         let the computer play\n"
            "  --bench SECONDS    autoplay, record every frame, print a report and exit\n"
            "  --seed N           simulation seed\n",
            program);
}

int main(int argc, char **argv)
{
    AppOptions options = { .windowMode = WINDOW_FIXED, .scale = 0, .vsync = false, .overlay = true, .autoplay = false, .benchSeconds = 0.0, .seed = 1 };

    for (int i = 1; i < argc; i++)
    {
        const char *arg = argv[i];
        const bool hasValue = (i + 1 < argc);
        if ((strcmp(arg, "--scale") == 0) && hasValue) options.scale = atoi(argv[++i]);
        else if (strcmp(arg, "--vsync") == 0) options.vsync = true;
        else if ((strcmp(arg, "--window") == 0) && hasValue)
        {
            const char *mode = argv[++i];
            if (strcmp(mode, "fixed") == 0) options.windowMode = WINDOW_FIXED;
            else if (strcmp(mode, "resizable") == 0) options.windowMode = WINDOW_RESIZABLE;
            else if (strcmp(mode, "fullscreen") == 0) options.windowMode = WINDOW_FULLSCREEN;
            else
            {
                PrintUsage(argv[0]);
                return 2;
            }
        }
        else if (strcmp(arg, "--no-overlay") == 0) options.overlay = false;
        else if (strcmp(arg, "--autoplay") == 0) options.autoplay = true;
        else if ((strcmp(arg, "--bench") == 0) && hasValue) options.benchSeconds = atof(argv[++i]);
        else if ((strcmp(arg, "--seed") == 0) && hasValue) options.seed = (uint32_t)strtoul(argv[++i], NULL, 10);
        else
        {
            PrintUsage(argv[0]);
            return 2;
        }
    }

    return AppRun(&options);
}
