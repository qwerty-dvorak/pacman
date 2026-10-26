#ifndef AUDIO_SYNTH_H
#define AUDIO_SYNTH_H

#include "core/base.h"

// Namco WSG style synthesizer: 32-step 4-bit wavetables read by 32-bit phase accumulators,
// 4-bit voice volumes, no filtering. Every sound (music, ambience, effects) is a programmed
// sequence of pitch/volume sweeps; nothing is loaded. Pure C: no device, OS or allocation.
//
// Threading: one owner. In the game that is the audio callback, which applies queued
// commands (SynthApply) and then renders (SynthRender); neither allocates nor locks.

#define SYNTH_SAMPLE_RATE   48000
#define SYNTH_GAIN_ONE      256         // Q8 unity gain for SynthCommand.musicGain/sfxGain
#define SYNTH_SIREN_LEVELS  5

typedef enum SoundEffect {
    SFX_WAKA = 0,       // alternates the two halves of the eating sound on every call
    SFX_FRUIT,
    SFX_GHOST_EATEN,
    SFX_DEATH,
    SFX_EXTRA_LIFE,
    SFX_MENU_MOVE,      // menu sounds keep playing while paused
    SFX_MENU_SELECT,
    SFX_COUNT
} SoundEffect;

typedef enum MusicTrack {
    MUSIC_NONE = 0,
    MUSIC_INTRO,        // start jingle, 3.8 s
    MUSIC_MENU,         // calm loop, 16 s
    MUSIC_COUNT
} MusicTrack;

typedef enum Ambience {
    AMBIENCE_NONE = 0,
    AMBIENCE_SIREN,     // pitch and rate rise with the siren level (0..4)
    AMBIENCE_FRIGHT,
    AMBIENCE_EYES,
    AMBIENCE_COUNT
} Ambience;

typedef enum SynthCommandType {
    SYNTH_COMMAND_SFX = 0,      // id: SoundEffect
    SYNTH_COMMAND_MUSIC,        // id: MusicTrack (MUSIC_NONE stops), param: loop, sequence
    SYNTH_COMMAND_AMBIENCE,     // id: Ambience, param: siren level
    SYNTH_COMMAND_VOLUMES,      // musicGain, sfxGain
    SYNTH_COMMAND_PAUSE         // param: paused
} SynthCommandType;

typedef struct SynthCommand {
    uint8_t type;               // SynthCommandType
    uint8_t id;
    uint8_t param;
    uint8_t reserved;
    uint16_t musicGain;         // Q8, 0..SYNTH_GAIN_ONE
    uint16_t sfxGain;           // Q8, 0..SYNTH_GAIN_ONE; ambience follows it
    uint32_t sequence;          // music request number, echoed by SynthMusicSequence
} SynthCommand;

// Effect segments and music notes are static tables private to synth.c.
typedef struct SynthSegment SynthSegment;
typedef struct SynthNote SynthNote;

// Internal state, exposed only so a Synth can be embedded by value.
typedef struct SynthVoice {
    uint32_t phase;             // top 5 bits index the 32-step wavetable
    uint32_t inc;
    int32_t incDelta;           // per-sample pitch sweep
    int32_t level;              // 4-bit volume in Q16
    int32_t levelDelta;         // per-sample volume sweep
    uint32_t remaining;         // samples left in the current segment or note stage
    const int8_t *wave;
    const SynthSegment *segment;    // effect program cursor; NULL when idle
    const SynthSegment *segmentEnd;
    const SynthSegment *loop;       // restart point at the end; NULL for one-shots
    uint8_t priority;
} SynthVoice;

typedef struct SynthChannel {
    const SynthNote *note;      // next note; NULL once the part has ended
    uint32_t releaseSamples;    // silent gap still owed after the sounding note
} SynthChannel;

enum { SYNTH_VOICE_MELODY = 0, SYNTH_VOICE_BASS, SYNTH_VOICE_AMBIENCE, SYNTH_VOICE_WAKA,
       SYNTH_VOICE_EFFECT, SYNTH_VOICE_MENU, SYNTH_VOICE_COUNT };

typedef struct Synth {
    SynthVoice voices[SYNTH_VOICE_COUNT];
    SynthChannel channels[2];   // melody and bass parts driving the two music voices
    uint8_t track;              // MusicTrack being played, MUSIC_NONE when finished
    bool loop;
    bool paused;
    bool wakaSecond;
    uint8_t ambience;
    uint8_t sirenLevel;
    uint16_t musicGain;
    uint16_t sfxGain;
    uint32_t musicSequence;
} Synth;

void SynthInit(Synth *synth);
void SynthApply(Synth *synth, const SynthCommand *command);

// Mixes frames of 48 kHz mono into out, overwriting it. Allocation- and lock-free.
void SynthRender(Synth *synth, int16_t *out, uint32_t frames);

// True while a music track is sounding (a looping track never ends on its own).
bool SynthMusicPlaying(const Synth *synth);
// Sequence number of the last applied music command.
uint32_t SynthMusicSequence(const Synth *synth);

#endif // AUDIO_SYNTH_H
