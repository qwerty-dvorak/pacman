#include "game/level_pack.h"

#include <stdio.h>
#include <string.h>

// Extra mazes for the tour pack. Every layout keeps the shared landmarks (house, exit,
// start, fruit spot), is mirror symmetric, has no dead ends and passes MazeValidate.

// Blossom: 318 pellets, tunnels on rows [8, 20].
static const MazeLayout mazeBlossom = { {
    "############################",
    "#..........................#",
    "#.####.#####.##.#####.####.#",
    "#o####.#####.##.#####.####o#",
    "#.####.#####.##.#####.####.#",
    "#..........................#",
    "#.####.##.##.##.##.##.####.#",
    "#.####.##.##.##.##.##.####.#",
    "..........##.##.##..........",
    "#.#######.##.##.##.#######.#",
    "#.#######.##.##.##.#######.#",
    "#........          ........#",
    "#.####.## ###--### ##.####.#",
    "#.####.## #      # ##.####.#",
    "#.####... #      # ...####.#",
    "#.####.## #      # ##.####.#",
    "#.####.## ######## ##.####.#",
    "#........          ........#",
    "#.####.##.########.##.####.#",
    "#.####.##.########.##.####.#",
    "..........########..........",
    "#.####.##.########.##.####.#",
    "#.####.##.########.##.####.#",
    "#............  ............#",
    "#o####.#####.##.#####.####o#",
    "#.####.#####.##.#####.####.#",
    "#.........##.##.##.........#",
    "#.####.##.##.##.##.##.####.#",
    "#.####.##.##.##.##.##.####.#",
    "#..........................#",
    "############################",
} };

// Glacier: 304 pellets, tunnels on rows [11, 17].
static const MazeLayout mazeGlacier = { {
    "############################",
    "#..........................#",
    "#o#######.##.##.##.#######o#",
    "#.#######.##.##.##.#######.#",
    "#............##............#",
    "#.##.####.########.####.##.#",
    "#.##.####.########.####.##.#",
    "#.##.####.########.####.##.#",
    "#.##....................##.#",
    "#.##.####.##.##.##.####.##.#",
    "#.##.####.##.##.##.####.##.#",
    ".........          .........",
    "####.#### ###--### ####.####",
    "####.#### #      # ####.####",
    "#....#### #      # ####....#",
    "#.##.#### #      # ####.##.#",
    "#.##.#### ######## ####.##.#",
    ".........          .........",
    "#.##.####.##.##.##.####.##.#",
    "#.##.####.##.##.##.####.##.#",
    "#....####....##....####....#",
    "#.##.####.########.####.##.#",
    "#.##.####.########.####.##.#",
    "#............  ............#",
    "#.#######.########.#######.#",
    "#o#######.########.#######o#",
    "#.#######.########.#######.#",
    "#....####....##....####....#",
    "#.##.####.##.##.##.####.##.#",
    "#..........................#",
    "############################",
} };

// Amber: 352 pellets, tunnels on rows [5, 26].
static const MazeLayout mazeAmber = { {
    "############################",
    "#..........................#",
    "#.####.##.########.##.####.#",
    "#o####.##.########.##.####o#",
    "#.####.##.########.##.####.#",
    "....##.......##.......##....",
    "#.#.##.##.##.##.##.##.##.#.#",
    "#.#.##.##.##.##.##.##.##.#.#",
    "#......##..........##......#",
    "#.####.##.##.##.##.##.####.#",
    "#.####.##.##.##.##.##.####.#",
    "#...##...          ...##...#",
    "#.#.##.## ###--### ##.##.#.#",
    "#.#.##.## #      # ##.##.#.#",
    "#........ #      # ........#",
    "#.#.##### #      # #####.#.#",
    "#.#.##### ######## #####.#.#",
    "#........          ........#",
    "#.#.##.##.##.##.##.##.##.#.#",
    "#.#.##.##.##.##.##.##.##.#.#",
    "#...##................##...#",
    "#.#.##.##.########.##.##.#.#",
    "#.#.##.##.########.##.##.#.#",
    "#......##....  ....##......#",
    "#.####.##.##.##.##.##.####.#",
    "#.####.##.##.##.##.##.####.#",
    ".......##..........##.......",
    "#o#.##.##.##.##.##.##.##.#o#",
    "#.#.##.##.##.##.##.##.##.#.#",
    "#..........................#",
    "############################",
} };

// Garden: 306 pellets, tunnels on rows [7, 20].
static const MazeLayout mazeGarden = { {
    "############################",
    "#..........................#",
    "#o#######.##.##.##.#######o#",
    "#.#######.##.##.##.#######.#",
    "#.....###..........###.....#",
    "#.###.###.##.##.##.###.###.#",
    "#.###.###.##.##.##.###.###.#",
    "..###........##........###..",
    "#.###.###.########.###.###.#",
    "#.###.###.########.###.###.#",
    "#.###.###.########.###.###.#",
    "#........          ........#",
    "#.###.### ###--### ###.###.#",
    "#.###.### #      # ###.###.#",
    "#.###.... #      # ....###.#",
    "#.###.### #      # ###.###.#",
    "#.###.### ######## ###.###.#",
    "#........          ........#",
    "#.###.###.##.##.##.###.###.#",
    "#.###.###.##.##.##.###.###.#",
    "..###........##........###..",
    "#####.###.##.##.##.###.#####",
    "#####.###.##.##.##.###.#####",
    "#............  ............#",
    "#.###.################.###.#",
    "#.###.################.###.#",
    "#.....###....##....###.....#",
    "#o#######.##.##.##.#######o#",
    "#.#######.##.##.##.#######.#",
    "#..........................#",
    "############################",
} };

typedef struct TourEntry {
    const MazeLayout *layout;
    const char *name;
    uint32_t wallColor;
} TourEntry;

static const TourEntry tourMazes[] = {
    { &mazeBlossom, "BLOSSOM", RGBA(255, 135, 170, 255) },
    { &mazeGlacier, "GLACIER", RGBA(71, 183, 255, 255) },
    { &mazeAmber, "AMBER", RGBA(255, 183, 81, 255) },
    { &mazeGarden, "GARDEN", RGBA(0, 200, 120, 255) },
};

#define CLASSIC_WALL_COLOR  RGBA(33, 33, 255, 255)
#define TOUR_LEVELS_PER_MAZE 2

void LevelPackInitEmpty(LevelPack *pack, const char *name, int levelsPerMaze)
{
    memset(pack, 0, sizeof(*pack));
    snprintf(pack->name, sizeof(pack->name), "%s", name);
    pack->levelsPerMaze = MAX(levelsPerMaze, 1);
}

bool LevelPackAdd(LevelPack *pack, const MazeLayout *layout, const char *name, uint32_t wallColor, bool classicArt)
{
    if (pack->mazeCount >= PACK_MAX_MAZES) return false;

    PackMaze *entry = &pack->mazes[pack->mazeCount++];
    entry->layout = *layout;
    MazeBuild(&entry->maze, layout);
    entry->wallColor = wallColor;
    entry->classicArt = classicArt;
    snprintf(entry->name, sizeof(entry->name), "%s", name);
    return true;
}

void LevelPackInitBuiltin(LevelPack *pack, BuiltinPack which)
{
    if (which == BUILTIN_TOUR)
    {
        LevelPackInitEmpty(pack, "MAZE TOUR", TOUR_LEVELS_PER_MAZE);
        LevelPackAdd(pack, &mazeLayoutClassic, "CLASSIC", CLASSIC_WALL_COLOR, true);
        for (size_t i = 0; i < ARRAY_COUNT(tourMazes); i++)
        {
            LevelPackAdd(pack, tourMazes[i].layout, tourMazes[i].name, tourMazes[i].wallColor, false);
        }
        return;
    }

    LevelPackInitEmpty(pack, "ARCADE", 1);
    LevelPackAdd(pack, &mazeLayoutClassic, "CLASSIC", CLASSIC_WALL_COLOR, true);
}

int LevelPackMazeIndex(const LevelPack *pack, int level)
{
    if (pack->mazeCount <= 0) return 0;
    return ((MAX(level, 1) - 1)/pack->levelsPerMaze)%pack->mazeCount;
}

//----------------------------------------------------------------------------------
// Maze files
//----------------------------------------------------------------------------------
// Copies the next line (without '\r' or '\n') into line; returns the rest of the text.
static const char *NextLine(const char *text, char *line, size_t lineSize, size_t *length)
{
    size_t n = 0;
    while ((*text != '\0') && (*text != '\n'))
    {
        if ((*text != '\r') && (n + 1 < lineSize)) line[n++] = *text;
        text++;
    }
    line[n] = '\0';
    *length = n;
    return (*text == '\n')? text + 1 : text;
}

bool MazeFileParse(const char *text, MazeLayout *layout, char *name, size_t nameSize, uint32_t *wallColor)
{
    char line[128];
    int row = 0;
    while ((*text != '\0') && (row < MAZE_ROWS))
    {
        size_t length = 0;
        text = NextLine(text, line, sizeof(line), &length);

        int r = 0;
        int g = 0;
        int b = 0;
        if (strncmp(line, "name ", 5) == 0) snprintf(name, nameSize, "%s", line + 5);
        else if (sscanf(line, "color %d %d %d", &r, &g, &b) == 3) *wallColor = RGBA(CLAMP(r, 0, 255), CLAMP(g, 0, 255), CLAMP(b, 0, 255), 255);
        else if (length == MAZE_COLS) memcpy(layout->tiles[row++], line, MAZE_COLS + 1);
        else if (length != 0) return false;
    }
    return row == MAZE_ROWS;
}

size_t MazeFileFormat(const MazeLayout *layout, const char *name, uint32_t wallColor, char *out, size_t outSize)
{
    int written = snprintf(out, outSize, "name %s\ncolor %u %u %u\n", name, wallColor & 0xFFu, (wallColor >> 8) & 0xFFu, (wallColor >> 16) & 0xFFu);
    if ((written < 0) || ((size_t)written >= outSize)) return 0;

    size_t length = (size_t)written;
    for (int y = 0; y < MAZE_ROWS; y++)
    {
        if (length + MAZE_COLS + 2 > outSize) return 0;
        memcpy(out + length, layout->tiles[y], MAZE_COLS);
        length += MAZE_COLS;
        out[length++] = '\n';
    }
    out[length] = '\0';
    return length;
}
