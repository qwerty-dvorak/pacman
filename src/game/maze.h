#ifndef GAME_MAZE_H
#define GAME_MAZE_H

#include "core/base.h"

// Maze space is the 224x248 playfield in arcade pixels. Tiles are 8x8 and an actor's
// tile is (x >> 3, y >> 3); the centre of tile t lies at pixel t*8 + 4.
#define MAZE_COLS           28
#define MAZE_ROWS           31
#define TILE_SIZE           8
#define TILE_CENTER         4
#define MAZE_WIDTH          (MAZE_COLS*TILE_SIZE)
#define MAZE_HEIGHT         (MAZE_ROWS*TILE_SIZE)

// Rows whose edge columns are open continue one virtual tile past each edge (tiles -1 and
// 28) and wrap, giving a 30-tile ring. x stays in [TUNNEL_MIN_X, TUNNEL_MAX_X).
#define TUNNEL_MIN_X        (-TILE_SIZE)
#define TUNNEL_MAX_X        (MAZE_WIDTH + TILE_SIZE)
#define TUNNEL_PERIOD       (TUNNEL_MAX_X - TUNNEL_MIN_X)

// Layout characters.
#define TILE_WALL           '#'
#define TILE_DOOR           '-'
#define TILE_DOT            '.'
#define TILE_ENERGIZER      'o'
#define TILE_EMPTY          ' '

// Fixed landmarks shared by every maze: the simulation scripts the ghost house, the
// start position and the fruit spot, so editors and validation keep them in place.
#define HOUSE_TOP_ROW       12
#define HOUSE_BOTTOM_ROW    16
#define HOUSE_LEFT_COL      10
#define HOUSE_RIGHT_COL     17
#define HOUSE_EXIT_ROW      11
#define PAC_START_ROW       23
#define FRUIT_ROW           17
#define CENTER_LEFT_COL     13      // The landmarks span columns 13 and 14.

typedef enum Dir {
    DIR_RIGHT = 0,          // Order matches the sprite sheet columns.
    DIR_LEFT,
    DIR_UP,
    DIR_DOWN,
    DIR_NONE
} Dir;

extern const int8_t dirDx[5];
extern const int8_t dirDy[5];

FORCE_INLINE Dir DirOpposite(Dir dir) { return (dir == DIR_NONE)? DIR_NONE : (Dir)(dir ^ 1); }

// Editable text form of a maze: one NUL-terminated row string per maze row.
typedef struct MazeLayout {
    char tiles[MAZE_ROWS][MAZE_COLS + 1];
} MazeLayout;

extern const MazeLayout mazeLayoutClassic;

// Runtime form: one bit per tile, bit x of row y.
typedef struct Maze {
    uint32_t walls[MAZE_ROWS];          // Blocks Pac-Man and roaming ghosts (includes the house door).
    uint32_t energizers[MAZE_ROWS];     // Static: pellet tiles holding an energizer.
    uint32_t pellets[MAZE_ROWS];        // Remaining dots and energizers.
    uint32_t tunnels[MAZE_ROWS];        // Tunnel tiles where ghosts slow down.
    uint32_t wrapRows;                  // Bit y: row y wraps around the screen edges.
    int32_t pelletsLeft;
} Maze;

void MazeBuild(Maze *maze, const MazeLayout *layout);

// True for the tiles validation and editors must leave untouched (house, its exit,
// Pac-Man's start and the fruit spot).
bool MazeIsLandmark(int tx, int ty);

typedef struct MazeCheck {
    bool ok;
    int x;                  // Offending tile, -1 when not tile specific.
    int y;
    char message[40];       // Upper case, arcade font characters only.
} MazeCheck;

// Verifies a layout is playable: closed top/bottom border, paired tunnel openings,
// intact landmarks, at least one pellet and every pellet reachable from the start.
MazeCheck MazeValidate(const MazeLayout *layout);

// Wraps tile coordinates arithmetically: (-1 >> 3) == -1 with GCC/Clang.
FORCE_INLINE int TileOf(int pixel) { return pixel >> 3; }
FORCE_INLINE int TileCenterPixel(int tile) { return tile*TILE_SIZE + TILE_CENTER; }

FORCE_INLINE bool MazeIsWall(const Maze *maze, int tx, int ty)
{
    if (UNLIKELY((unsigned)tx >= MAZE_COLS)) return ((maze->wrapRows >> ty) & 1u) == 0;
    return (maze->walls[ty] >> tx) & 1u;
}

FORCE_INLINE bool MazeIsTunnel(const Maze *maze, int tx, int ty)
{
    if (UNLIKELY((unsigned)tx >= MAZE_COLS)) return true;
    return (maze->tunnels[ty] >> tx) & 1u;
}

FORCE_INLINE bool MazeHasPellet(const Maze *maze, int tx, int ty)
{
    if (UNLIKELY((unsigned)tx >= MAZE_COLS)) return false;
    return (maze->pellets[ty] >> tx) & 1u;
}

FORCE_INLINE bool MazeIsEnergizer(const Maze *maze, int tx, int ty)
{
    return (maze->energizers[ty] >> tx) & 1u;
}

FORCE_INLINE void MazeClearPellet(Maze *maze, int tx, int ty)
{
    maze->pellets[ty] &= ~(1u << tx);
    maze->pelletsLeft--;
}

FORCE_INLINE int WrapTunnelX(int x)
{
    if (x < TUNNEL_MIN_X) return x + TUNNEL_PERIOD;
    if (x >= TUNNEL_MAX_X) return x - TUNNEL_PERIOD;
    return x;
}

#endif // GAME_MAZE_H
