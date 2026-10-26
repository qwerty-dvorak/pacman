#include "game/maze.h"

#include <string.h>

const int8_t dirDx[5] = { 1, -1, 0, 0, 0 };
const int8_t dirDy[5] = { 0, 0, -1, 1, 0 };

// Verified tile-for-tile against assets/sprites/general_sprites.png (filled vs. empty maze).
const MazeLayout mazeLayoutClassic = { {
    "############################",
    "#............##............#",
    "#.####.#####.##.#####.####.#",
    "#o####.#####.##.#####.####o#",
    "#.####.#####.##.#####.####.#",
    "#..........................#",
    "#.####.##.########.##.####.#",
    "#.####.##.########.##.####.#",
    "#......##....##....##......#",
    "######.##### ## #####.######",
    "     #.##### ## #####.#     ",
    "     #.##          ##.#     ",
    "     #.## ###--### ##.#     ",
    "######.## #      # ##.######",
    "      .   #      #   .      ",
    "######.## #      # ##.######",
    "     #.## ######## ##.#     ",
    "     #.##          ##.#     ",
    "     #.## ######## ##.#     ",
    "######.## ######## ##.######",
    "#............##............#",
    "#.####.#####.##.#####.####.#",
    "#.####.#####.##.#####.####.#",
    "#o..##.......  .......##..o#",
    "###.##.##.########.##.##.###",
    "###.##.##.########.##.##.###",
    "#......##....##....##......#",
    "#.##########.##.##########.#",
    "#.##########.##.##########.#",
    "#..........................#",
    "############################",
} };

void MazeBuild(Maze *maze, const MazeLayout *layout)
{
    memset(maze, 0, sizeof(*maze));
    for (int y = 0; y < MAZE_ROWS; y++)
    {
        for (int x = 0; x < MAZE_COLS; x++)
        {
            const uint32_t bit = 1u << x;
            switch (layout->tiles[y][x])
            {
                case TILE_WALL:
                case TILE_DOOR: maze->walls[y] |= bit; break;
                case TILE_ENERGIZER: maze->energizers[y] |= bit; maze->pellets[y] |= bit; break;
                case TILE_DOT: maze->pellets[y] |= bit; break;
                default: break;
            }
        }
        maze->pelletsLeft += __builtin_popcount(maze->pellets[y]);

        // A row wraps when both edge tiles are open. Its tunnel is the enclosed stretch from
        // each edge inwards: open, with walls directly above and below.
        const uint32_t walls = maze->walls[y];
        if (((walls & 1u) != 0) || (((walls >> (MAZE_COLS - 1)) & 1u) != 0) || (y == 0) || (y == MAZE_ROWS - 1)) continue;
        maze->wrapRows |= 1u << y;
    }

    for (int y = 1; y < MAZE_ROWS - 1; y++)
    {
        if (((maze->wrapRows >> y) & 1u) == 0) continue;
        const uint32_t enclosed = ~maze->walls[y] & maze->walls[y - 1] & maze->walls[y + 1];
        for (int x = 0; (x < MAZE_COLS) && ((enclosed >> x) & 1u); x++) maze->tunnels[y] |= 1u << x;
        for (int x = MAZE_COLS - 1; (x >= 0) && ((enclosed >> x) & 1u); x--) maze->tunnels[y] |= 1u << x;
    }
}

bool MazeIsLandmark(int tx, int ty)
{
    if ((ty >= HOUSE_TOP_ROW) && (ty <= HOUSE_BOTTOM_ROW) && (tx >= HOUSE_LEFT_COL) && (tx <= HOUSE_RIGHT_COL)) return true;
    const bool centre = (tx == CENTER_LEFT_COL) || (tx == CENTER_LEFT_COL + 1);
    return centre && ((ty == HOUSE_EXIT_ROW) || (ty == PAC_START_ROW) || (ty == FRUIT_ROW));
}

static MazeCheck Fail(int x, int y, const char *message)
{
    MazeCheck check = { .ok = false, .x = x, .y = y };
    strncpy(check.message, message, sizeof(check.message) - 1);
    return check;
}

static bool IsOpen(char c) { return (c != TILE_WALL) && (c != TILE_DOOR); }

MazeCheck MazeValidate(const MazeLayout *layout)
{
    int pellets = 0;
    for (int y = 0; y < MAZE_ROWS; y++)
    {
        if (strlen(layout->tiles[y]) != MAZE_COLS) return Fail(-1, y, "ROW LENGTH MUST BE 28");
        for (int x = 0; x < MAZE_COLS; x++)
        {
            const char c = layout->tiles[y][x];
            const bool landmark = MazeIsLandmark(x, y);
            const bool house = (y >= HOUSE_TOP_ROW) && (y <= HOUSE_BOTTOM_ROW) && (x >= HOUSE_LEFT_COL) && (x <= HOUSE_RIGHT_COL);
            if ((c != TILE_WALL) && (c != TILE_DOOR) && (c != TILE_DOT) && (c != TILE_ENERGIZER) && (c != TILE_EMPTY)) return Fail(x, y, "UNKNOWN TILE");
            if (((y == 0) || (y == MAZE_ROWS - 1)) && (c != TILE_WALL)) return Fail(x, y, "TOP AND BOTTOM MUST BE WALL");
            if ((c == TILE_DOOR) && !landmark) return Fail(x, y, "DOOR ONLY ON THE HOUSE");
            if (house && (c != mazeLayoutClassic.tiles[y][x])) return Fail(x, y, "HOUSE MUST STAY INTACT");
            if (landmark && !house && !IsOpen(c)) return Fail(x, y, "START EXIT AND FRUIT MUST BE OPEN");
            if ((c == TILE_DOT) || (c == TILE_ENERGIZER)) pellets++;
        }
        if (IsOpen(layout->tiles[y][0]) != IsOpen(layout->tiles[y][MAZE_COLS - 1])) return Fail(0, y, "TUNNEL NEEDS BOTH ENDS OPEN");
    }
    if (pellets == 0) return Fail(-1, -1, "MAZE NEEDS PELLETS");

    // Flood fill from Pac-Man's start through open tiles, wrapping on tunnel rows.
    uint8_t reached[MAZE_ROWS][MAZE_COLS];
    uint16_t queue[MAZE_ROWS*MAZE_COLS];
    memset(reached, 0, sizeof(reached));
    int head = 0;
    int tail = 0;
    reached[PAC_START_ROW][CENTER_LEFT_COL] = 1;
    queue[tail++] = (uint16_t)(PAC_START_ROW*MAZE_COLS + CENTER_LEFT_COL);
    while (head < tail)
    {
        const int node = queue[head++];
        const int x = node%MAZE_COLS;
        const int y = node/MAZE_COLS;
        for (int d = 0; d < 4; d++)
        {
            const int nx = (x + dirDx[d] + MAZE_COLS)%MAZE_COLS;
            const int ny = y + dirDy[d];
            if ((ny < 0) || (ny >= MAZE_ROWS) || reached[ny][nx] || !IsOpen(layout->tiles[ny][nx])) continue;
            reached[ny][nx] = 1;
            queue[tail++] = (uint16_t)(ny*MAZE_COLS + nx);
        }
    }

    if (!reached[HOUSE_EXIT_ROW][CENTER_LEFT_COL]) return Fail(CENTER_LEFT_COL, HOUSE_EXIT_ROW, "HOUSE EXIT UNREACHABLE");
    for (int y = 0; y < MAZE_ROWS; y++)
    {
        for (int x = 0; x < MAZE_COLS; x++)
        {
            const char c = layout->tiles[y][x];
            if (((c == TILE_DOT) || (c == TILE_ENERGIZER)) && !reached[y][x]) return Fail(x, y, "PELLET UNREACHABLE");
        }
    }
    return (MazeCheck){ .ok = true, .x = -1, .y = -1 };
}
