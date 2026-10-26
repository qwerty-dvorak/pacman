#ifndef APP_STORAGE_H
#define APP_STORAGE_H

#include "core/base.h"
#include "game/level_pack.h"

// Persistent data under $XDG_DATA_HOME/pacman (default ~/.local/share/pacman): settings,
// the high score table and the editor's maze slots. Every write goes to a temporary file
// that is renamed over the old one, so a crash never leaves a truncated file.

#define FPS_LIMIT_VSYNC     (-1)

typedef enum PackChoice {
    PACK_CHOICE_ARCADE = 0,
    PACK_CHOICE_TOUR,
    PACK_CHOICE_CUSTOM,
    PACK_CHOICE_COUNT
} PackChoice;

typedef struct Settings {
    int musicVolume;        // 0..10
    int sfxVolume;          // 0..10
    int fpsLimit;           // 0 = unlimited, FPS_LIMIT_VSYNC, or a frame rate.
    bool overlay;
    int lives;              // 1..5
    int pack;               // PackChoice
    int startLevel;         // 1..MAX_START_LEVEL
} Settings;

#define MAX_START_LEVEL     21

#define SCORE_ENTRIES       10
#define SCORE_NAME_LENGTH   3

typedef struct ScoreEntry {
    char name[SCORE_NAME_LENGTH + 1];
    uint32_t score;
    int32_t level;
} ScoreEntry;

typedef struct ScoreTable {
    ScoreEntry entries[SCORE_ENTRIES];
    int count;
} ScoreTable;

#define MAZE_SLOTS          24

void SettingsDefault(Settings *settings);
void SettingsLoad(Settings *settings);          // Defaults for anything missing or invalid.
bool SettingsSave(const Settings *settings);

void ScoresLoad(ScoreTable *table);             // Seeded defaults when no file exists.
bool ScoresSave(const ScoreTable *table);
int ScoresRank(const ScoreTable *table, uint32_t score);   // Insert position, or -1 if it does not place.
void ScoresInsert(ScoreTable *table, int rank, const char *name, uint32_t score, int32_t level);
uint32_t ScoresBest(const ScoreTable *table);

// Editor slots are 1-based. Load fails if the slot file is missing or malformed.
bool MazeSlotLoad(int slot, MazeLayout *layout, char *name, size_t nameSize, uint32_t *wallColor);
bool MazeSlotSave(int slot, const MazeLayout *layout, const char *name, uint32_t wallColor);
bool MazeSlotExists(int slot);

// Fills pack with every valid saved slot, in slot order. Returns the number of mazes.
int CustomPackLoad(LevelPack *pack);

#endif // APP_STORAGE_H
