#define _POSIX_C_SOURCE 200809L

#include "app/storage.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define PATH_SIZE           512
#define FILE_BUFFER_SIZE    4096

//----------------------------------------------------------------------------------
// Paths and files
//----------------------------------------------------------------------------------
static bool MakeDirectories(const char *path)
{
    char buffer[PATH_SIZE];
    snprintf(buffer, sizeof(buffer), "%s", path);
    for (char *p = buffer + 1; *p != '\0'; p++)
    {
        if (*p != '/') continue;
        *p = '\0';
        if ((mkdir(buffer, 0755) != 0) && (errno != EEXIST)) return false;
        *p = '/';
    }
    return (mkdir(buffer, 0755) == 0) || (errno == EEXIST);
}

// Builds <data dir>/<relative>, creating the directories leading to it.
static bool DataPath(const char *relative, char *out, size_t outSize)
{
    const char *xdg = getenv("XDG_DATA_HOME");
    const char *home = getenv("HOME");
    char base[PATH_SIZE];
    if ((xdg != NULL) && (xdg[0] == '/')) snprintf(base, sizeof(base), "%s/pacman", xdg);
    else if (home != NULL) snprintf(base, sizeof(base), "%s/.local/share/pacman", home);
    else return false;

    const int written = snprintf(out, outSize, "%s/%s", base, relative);
    if ((written < 0) || ((size_t)written >= outSize)) return false;

    char directory[PATH_SIZE];
    snprintf(directory, sizeof(directory), "%s", out);
    char *slash = strrchr(directory, '/');
    if (slash != NULL) *slash = '\0';
    return MakeDirectories(directory);
}

static bool ReadText(const char *relative, char *out, size_t outSize)
{
    char path[PATH_SIZE];
    if (!DataPath(relative, path, sizeof(path))) return false;

    FILE *file = fopen(path, "rb");
    if (file == NULL) return false;
    const size_t length = fread(out, 1, outSize - 1, file);
    out[length] = '\0';
    fclose(file);
    return true;
}

static bool WriteText(const char *relative, const char *text, size_t length)
{
    char path[PATH_SIZE];
    char temp[PATH_SIZE + 8];
    if (!DataPath(relative, path, sizeof(path))) return false;
    snprintf(temp, sizeof(temp), "%s.tmp", path);

    FILE *file = fopen(temp, "wb");
    if (file == NULL) return false;
    const bool written = (fwrite(text, 1, length, file) == length);
    const bool closed = (fclose(file) == 0);
    if (!written || !closed)
    {
        remove(temp);
        return false;
    }
    return rename(temp, path) == 0;
}

//----------------------------------------------------------------------------------
// Settings
//----------------------------------------------------------------------------------
void SettingsDefault(Settings *settings)
{
    *settings = (Settings){
        .musicVolume = 6, .sfxVolume = 8, .fpsLimit = 0, .overlay = false,
        .lives = 3, .pack = PACK_CHOICE_ARCADE, .startLevel = 1
    };
}

void SettingsLoad(Settings *settings)
{
    SettingsDefault(settings);
    char text[FILE_BUFFER_SIZE];
    if (!ReadText("settings.cfg", text, sizeof(text))) return;

    char *save = NULL;
    for (char *line = strtok_r(text, "\n", &save); line != NULL; line = strtok_r(NULL, "\n", &save))
    {
        char key[32];
        int value = 0;
        if (sscanf(line, "%31[a-z_] = %d", key, &value) != 2) continue;

        if (strcmp(key, "music_volume") == 0) settings->musicVolume = CLAMP(value, 0, 10);
        else if (strcmp(key, "sfx_volume") == 0) settings->sfxVolume = CLAMP(value, 0, 10);
        else if (strcmp(key, "fps_limit") == 0) settings->fpsLimit = CLAMP(value, FPS_LIMIT_VSYNC, 1000);
        else if (strcmp(key, "overlay") == 0) settings->overlay = (value != 0);
        else if (strcmp(key, "lives") == 0) settings->lives = CLAMP(value, 1, 5);
        else if (strcmp(key, "pack") == 0) settings->pack = CLAMP(value, 0, PACK_CHOICE_COUNT - 1);
        else if (strcmp(key, "start_level") == 0) settings->startLevel = CLAMP(value, 1, MAX_START_LEVEL);
    }
}

bool SettingsSave(const Settings *settings)
{
    char text[512];
    const int length = snprintf(text, sizeof(text),
        "music_volume = %d\nsfx_volume = %d\nfps_limit = %d\noverlay = %d\nlives = %d\npack = %d\nstart_level = %d\n",
        settings->musicVolume, settings->sfxVolume, settings->fpsLimit, settings->overlay? 1 : 0,
        settings->lives, settings->pack, settings->startLevel);
    return (length > 0) && WriteText("settings.cfg", text, (size_t)length);
}

//----------------------------------------------------------------------------------
// High scores
//----------------------------------------------------------------------------------
static void ScoresSeed(ScoreTable *table)
{
    static const ScoreEntry seed[] = {
        { "BLI", 10000, 4 }, { "PIN", 8000, 3 }, { "INK", 6000, 3 }, { "CLY", 5000, 2 }, { "PAC", 4000, 2 },
        { "MSP", 3000, 2 }, { "SUE", 2500, 1 }, { "FUN", 2000, 1 }, { "KEY", 1500, 1 }, { "BEL", 1000, 1 },
    };
    memcpy(table->entries, seed, sizeof(seed));
    table->count = (int)ARRAY_COUNT(seed);
}

void ScoresLoad(ScoreTable *table)
{
    memset(table, 0, sizeof(*table));
    char text[FILE_BUFFER_SIZE];
    if (!ReadText("scores.txt", text, sizeof(text)))
    {
        ScoresSeed(table);
        return;
    }

    char *save = NULL;
    for (char *line = strtok_r(text, "\n", &save); (line != NULL) && (table->count < SCORE_ENTRIES); line = strtok_r(NULL, "\n", &save))
    {
        ScoreEntry entry = { 0 };
        unsigned int score = 0;
        int level = 0;
        if (sscanf(line, "%3[A-Z0-9 .!-] %u %d", entry.name, &score, &level) != 3) continue;
        entry.score = score;
        entry.level = level;
        const int rank = ScoresRank(table, entry.score);
        if (rank >= 0) ScoresInsert(table, rank, entry.name, entry.score, entry.level);
    }
}

bool ScoresSave(const ScoreTable *table)
{
    char text[SCORE_ENTRIES*32];
    size_t length = 0;
    for (int i = 0; i < table->count; i++)
    {
        const ScoreEntry *e = &table->entries[i];
        length += (size_t)snprintf(text + length, sizeof(text) - length, "%s %u %d\n", e->name, e->score, e->level);
    }
    return WriteText("scores.txt", text, length);
}

int ScoresRank(const ScoreTable *table, uint32_t score)
{
    if (score == 0) return -1;
    for (int i = 0; i < table->count; i++)
    {
        if (score > table->entries[i].score) return i;
    }
    return (table->count < SCORE_ENTRIES)? table->count : -1;
}

void ScoresInsert(ScoreTable *table, int rank, const char *name, uint32_t score, int32_t level)
{
    if ((rank < 0) || (rank >= SCORE_ENTRIES)) return;
    const int last = MIN(table->count, SCORE_ENTRIES - 1);
    memmove(&table->entries[rank + 1], &table->entries[rank], (size_t)(last - rank)*sizeof(ScoreEntry));

    ScoreEntry *e = &table->entries[rank];
    snprintf(e->name, sizeof(e->name), "%s", name);
    e->score = score;
    e->level = level;
    if (table->count < SCORE_ENTRIES) table->count++;
}

uint32_t ScoresBest(const ScoreTable *table)
{
    return (table->count > 0)? table->entries[0].score : 0;
}

//----------------------------------------------------------------------------------
// Maze slots
//----------------------------------------------------------------------------------
static void SlotPath(int slot, char *out, size_t outSize)
{
    snprintf(out, outSize, "levels/slot_%02d.maze", slot);
}

bool MazeSlotLoad(int slot, MazeLayout *layout, char *name, size_t nameSize, uint32_t *wallColor)
{
    char relative[64];
    char text[FILE_BUFFER_SIZE];
    SlotPath(slot, relative, sizeof(relative));
    if (!ReadText(relative, text, sizeof(text))) return false;
    return MazeFileParse(text, layout, name, nameSize, wallColor);
}

bool MazeSlotSave(int slot, const MazeLayout *layout, const char *name, uint32_t wallColor)
{
    char relative[64];
    char text[FILE_BUFFER_SIZE];
    SlotPath(slot, relative, sizeof(relative));
    const size_t length = MazeFileFormat(layout, name, wallColor, text, sizeof(text));
    return (length > 0) && WriteText(relative, text, length);
}

bool MazeSlotExists(int slot)
{
    char relative[64];
    char path[PATH_SIZE];
    struct stat info;
    SlotPath(slot, relative, sizeof(relative));
    return DataPath(relative, path, sizeof(path)) && (stat(path, &info) == 0);
}

int CustomPackLoad(LevelPack *pack)
{
    LevelPackInitEmpty(pack, "CUSTOM", 1);
    for (int slot = 1; slot <= MAZE_SLOTS; slot++)
    {
        MazeLayout layout;
        char name[MAZE_NAME_SIZE] = { 0 };
        uint32_t color = RGBA(33, 33, 255, 255);
        if (!MazeSlotLoad(slot, &layout, name, sizeof(name), &color)) continue;
        if (!MazeValidate(&layout).ok) continue;
        LevelPackAdd(pack, &layout, name, color, false);
    }
    return pack->mazeCount;
}
