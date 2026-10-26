#include "audio/synth.h"

#include <string.h>

#define SAMPLES_PER_MS      (SYNTH_SAMPLE_RATE/1000)
#define RENDER_CHUNK        256
#define NO_LOOP             0xFF

// Mixer scale: a voice at volume 15 on a +-15 wave contributes 15*240 before the Q8 bus
// gain; the shift maps that to 7200, so four loud voices fit int16 and six clip only at
// full volume, where the final clamp catches them.
#define LEVEL_SHIFT         12
#define MIX_SHIFT           7

typedef enum SynthWave {
    WAVE_SINE = 0,
    WAVE_TRIANGLE,
    WAVE_SQUARE,
    WAVE_PULSE,
    WAVE_SAW,
    WAVE_ORGAN,
    WAVE_COUNT
} SynthWave;

struct SynthSegment {
    uint8_t wave;
    uint8_t volume;             // 4-bit volume at the start of the sweep
    uint8_t volumeEnd;
    uint16_t ms;
    uint16_t startHz;
    uint16_t endHz;
};

struct SynthNote {
    uint8_t note;               // MIDI note number, 0 = rest
    uint8_t ticks;
};

typedef struct SynthProgram {
    const SynthSegment *segments;
    uint8_t count;
    uint8_t loopIndex;          // NO_LOOP for one-shots
    uint8_t voice;
    uint8_t priority;           // a busy voice only yields to equal or higher priority
} SynthProgram;

typedef struct SynthTrack {
    const SynthNote *parts[2];  // melody, bass; both parts span the same number of ticks
    uint16_t counts[2];
    uint8_t waves[2];
    uint8_t volumes[2];         // 4-bit attack volume
    uint8_t floors[2];          // the per-tick decay stops here
    uint32_t samplesPerTick;
    uint32_t releaseSamples;    // gap after every sounded note so repeats re-articulate
} SynthTrack;

// 32-step 4-bit wavetables stored as 2*v - 15 (odd values -15..15): the same 16 levels,
// centred on zero so voices starting and stopping do not step the DC level. The 25% pulse
// is shifted to 23/-7 for the same reason.
static const int8_t waves[WAVE_COUNT][32] = {
    [WAVE_SINE] = { 1, 3, 5, 9, 11, 13, 13, 15, 15, 15, 13, 13, 11, 9, 5, 3, 1, -3, -5, -9, -11, -13, -13, -15, -15, -15, -13, -13, -11, -9, -5, -3 },
    [WAVE_TRIANGLE] = { -15, -13, -11, -9, -7, -5, -3, -1, 1, 3, 5, 7, 9, 11, 13, 15, 15, 13, 11, 9, 7, 5, 3, 1, -1, -3, -5, -7, -9, -11, -13, -15 },
    [WAVE_SQUARE] = { 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, -15, -15, -15, -15, -15, -15, -15, -15, -15, -15, -15, -15, -15, -15, -15, -15 },
    [WAVE_PULSE] = { 23, 23, 23, 23, 23, 23, 23, 23, -7, -7, -7, -7, -7, -7, -7, -7, -7, -7, -7, -7, -7, -7, -7, -7, -7, -7, -7, -7, -7, -7, -7, -7 },
    [WAVE_SAW] = { -15, -15, -13, -13, -11, -11, -9, -9, -7, -7, -5, -5, -3, -3, -1, -1, 1, 1, 3, 3, 5, 5, 7, 7, 9, 9, 11, 11, 13, 13, 15, 15 },
    [WAVE_ORGAN] = { 1, 5, 9, 11, 13, 15, 15, 13, 11, 9, 7, 5, 3, 1, 1, 1, 1, -1, -1, -1, -3, -5, -7, -9, -11, -13, -15, -15, -13, -11, -9, -5 },
};

// Phase increments of MIDI notes 120..131; lower octaves are exact right shifts.
static const uint32_t topOctaveInc[12] = {
    749115498u, 793660223u, 840853716u, 890853480u, 943826385u, 999949222u,
    1059409297u, 1122405052u, 1189146729u, 1259857073u, 1334772074u, 1414141751u
};

#define SEG(wave, volume, volumeEnd, ms, startHz, endHz)    { (wave), (volume), (volumeEnd), (ms), (startHz), (endHz) }
#define REST(ms)                                            SEG(WAVE_SINE, 0, 0, (ms), 0, 0)
#define TONE(wave, volume, ms, hz)                          SEG((wave), (volume), (volume), (ms), (hz), (hz))

//----------------------------------------------------------------------------------
// Effects
//----------------------------------------------------------------------------------

// The eating sound is two halves, a falling and a rising sweep, alternated per dot.
static const SynthSegment wakaDown[] = {
    SEG(WAVE_SQUARE, 9, 6, 80, 560, 230),
    SEG(WAVE_SQUARE, 6, 0, 10, 230, 210),
};
static const SynthSegment wakaUp[] = {
    SEG(WAVE_SQUARE, 9, 6, 80, 230, 560),
    SEG(WAVE_SQUARE, 6, 0, 10, 560, 580),
};
static const SynthSegment fruit[] = {
    SEG(WAVE_TRIANGLE, 13, 13, 90, 400, 1400),
    SEG(WAVE_TRIANGLE, 13, 0, 170, 1400, 500),
};
static const SynthSegment ghostEaten[] = {
    SEG(WAVE_SAW, 12, 10, 380, 200, 1900),
    SEG(WAVE_SAW, 10, 0, 40, 1900, 2000),
};

// Nine up-down chirps sliding downward, then two low "bwop" drops: 1.4 s.
#define DEATH_CHIRP(hi, lo) SEG(WAVE_TRIANGLE, 12, 12, 25, (lo), (hi)), SEG(WAVE_TRIANGLE, 12, 10, 90, (hi), (lo))
static const SynthSegment death[] = {
    DEATH_CHIRP(1000, 620), DEATH_CHIRP(930, 580), DEATH_CHIRP(860, 540),
    DEATH_CHIRP(800, 500), DEATH_CHIRP(740, 460), DEATH_CHIRP(680, 420),
    DEATH_CHIRP(620, 380), DEATH_CHIRP(560, 340), DEATH_CHIRP(500, 300),
    REST(60),
    SEG(WAVE_SQUARE, 12, 3, 120, 420, 110),
    REST(40),
    SEG(WAVE_SQUARE, 12, 0, 120, 420, 110),
};

#define LIFE_BEEP SEG(WAVE_SQUARE, 10, 8, 60, 1046, 1046), REST(60)
static const SynthSegment extraLife[] = {
    LIFE_BEEP, LIFE_BEEP, LIFE_BEEP, LIFE_BEEP, LIFE_BEEP, LIFE_BEEP, LIFE_BEEP, LIFE_BEEP,
};
static const SynthSegment menuMove[] = {
    SEG(WAVE_PULSE, 8, 0, 35, 880, 1100),
};
static const SynthSegment menuSelect[] = {
    SEG(WAVE_SQUARE, 9, 7, 50, 523, 523),
    SEG(WAVE_SQUARE, 9, 7, 50, 784, 784),
    SEG(WAVE_SQUARE, 9, 0, 140, 1046, 1046),
};

#define PROGRAM(segments, loopIndex, voice, priority) { (segments), (uint8_t)ARRAY_COUNT(segments), (loopIndex), (voice), (priority) }

// SFX_WAKA's entry is the falling half; the rising half is wakaUpProgram.
static const SynthProgram sfxPrograms[SFX_COUNT] = {
    [SFX_WAKA] = PROGRAM(wakaDown, NO_LOOP, SYNTH_VOICE_WAKA, 0),
    [SFX_FRUIT] = PROGRAM(fruit, NO_LOOP, SYNTH_VOICE_EFFECT, 1),
    [SFX_GHOST_EATEN] = PROGRAM(ghostEaten, NO_LOOP, SYNTH_VOICE_EFFECT, 2),
    [SFX_DEATH] = PROGRAM(death, NO_LOOP, SYNTH_VOICE_EFFECT, 3),
    [SFX_EXTRA_LIFE] = PROGRAM(extraLife, NO_LOOP, SYNTH_VOICE_EFFECT, 2),
    [SFX_MENU_MOVE] = PROGRAM(menuMove, NO_LOOP, SYNTH_VOICE_MENU, 0),
    [SFX_MENU_SELECT] = PROGRAM(menuSelect, NO_LOOP, SYNTH_VOICE_MENU, 0),
};
static const SynthProgram wakaUpProgram = PROGRAM(wakaUp, NO_LOOP, SYNTH_VOICE_WAKA, 0);

//----------------------------------------------------------------------------------
// Ambience (looping)
//----------------------------------------------------------------------------------

// The siren rises and falls continuously; each level is higher and slightly faster.
#define SIREN(lo, hi, halfMs) SEG(WAVE_SINE, 8, 8, (halfMs), (lo), (hi)), SEG(WAVE_SINE, 8, 8, (halfMs), (hi), (lo))
static const SynthSegment siren0[] = { SIREN(380, 760, 220) };
static const SynthSegment siren1[] = { SIREN(430, 840, 205) };
static const SynthSegment siren2[] = { SIREN(480, 920, 190) };
static const SynthSegment siren3[] = { SIREN(540, 1000, 175) };
static const SynthSegment siren4[] = { SIREN(600, 1100, 160) };
static const SynthSegment fright[] = {
    SEG(WAVE_TRIANGLE, 9, 7, 110, 220, 560),
};
static const SynthSegment eyes[] = {
    SEG(WAVE_SINE, 8, 8, 70, 1800, 650),
};

static const SynthProgram sirenPrograms[SYNTH_SIREN_LEVELS] = {
    PROGRAM(siren0, 0, SYNTH_VOICE_AMBIENCE, 0),
    PROGRAM(siren1, 0, SYNTH_VOICE_AMBIENCE, 0),
    PROGRAM(siren2, 0, SYNTH_VOICE_AMBIENCE, 0),
    PROGRAM(siren3, 0, SYNTH_VOICE_AMBIENCE, 0),
    PROGRAM(siren4, 0, SYNTH_VOICE_AMBIENCE, 0),
};
static const SynthProgram frightProgram = PROGRAM(fright, 0, SYNTH_VOICE_AMBIENCE, 0);
static const SynthProgram eyesProgram = PROGRAM(eyes, 0, SYNTH_VOICE_AMBIENCE, 0);

//----------------------------------------------------------------------------------
// Music
//----------------------------------------------------------------------------------

enum {
    E2 = 40, F_SHARP2 = 42, G_SHARP2 = 44, B2 = 47, C3 = 48, C_SHARP3 = 49, D_SHARP3 = 51, E3 = 52,
    F_SHARP3 = 54, G_SHARP3 = 56, A_SHARP3 = 58, B3 = 59, C4 = 60, A_SHARP4 = 70, B4 = 71, C5 = 72,
    C_SHARP5 = 73, D_SHARP5 = 75, E5 = 76, F5 = 77, F_SHARP5 = 78, G5 = 79, G_SHARP5 = 80, A5 = 81,
    A_SHARP5 = 82, B5 = 83, C6 = 84, C_SHARP6 = 85, D_SHARP6 = 87
};

// Start jingle, one tick = a 32nd note. Two arpeggio phrases a semitone apart, the first
// again, then the chromatic climb to the tonic: 66 ticks.
static const SynthNote introMelody[] = {
    { B4, 2 }, { B5, 2 }, { F_SHARP5, 2 }, { D_SHARP5, 2 }, { B5, 1 }, { F_SHARP5, 3 }, { D_SHARP5, 4 },
    { C5, 2 }, { C6, 2 }, { G5, 2 }, { E5, 2 }, { C6, 1 }, { G5, 3 }, { E5, 4 },
    { B4, 2 }, { B5, 2 }, { F_SHARP5, 2 }, { D_SHARP5, 2 }, { B5, 1 }, { F_SHARP5, 3 }, { D_SHARP5, 4 },
    { D_SHARP5, 1 }, { E5, 1 }, { F5, 2 }, { F5, 1 }, { F_SHARP5, 1 }, { G5, 2 }, { G5, 1 }, { G_SHARP5, 1 },
    { A5, 2 }, { B5, 6 },
};
static const SynthNote introBass[] = {
    { B2, 6 }, { B3, 2 }, { B2, 6 }, { B3, 2 },
    { C3, 6 }, { C4, 2 }, { C3, 6 }, { C4, 2 },
    { B2, 6 }, { B3, 2 }, { B2, 6 }, { B3, 2 },
    { F_SHARP3, 4 }, { G_SHARP3, 4 }, { A_SHARP3, 4 }, { B3, 6 },
};

// Menu loop, one tick = a 16th note at 120 bpm: eight bars of I-vi-IV-V in B major built
// from the jingle's arpeggio, over a root-fifth-octave walking bass. 128 ticks = 16 s.
static const SynthNote menuMelody[] = {
    { B4, 4 }, { D_SHARP5, 2 }, { F_SHARP5, 2 }, { B5, 4 }, { A_SHARP5, 2 }, { F_SHARP5, 2 },
    { G_SHARP5, 6 }, { F_SHARP5, 2 }, { D_SHARP5, 4 }, { B4, 4 },
    { E5, 4 }, { G_SHARP5, 2 }, { B5, 2 }, { G_SHARP5, 4 }, { E5, 2 }, { F_SHARP5, 2 },
    { F_SHARP5, 8 }, { A_SHARP4, 4 }, { C_SHARP5, 4 },
    { B4, 4 }, { D_SHARP5, 2 }, { F_SHARP5, 2 }, { B5, 4 }, { C_SHARP6, 2 }, { D_SHARP6, 2 },
    { B5, 6 }, { G_SHARP5, 2 }, { F_SHARP5, 4 }, { D_SHARP5, 4 },
    { E5, 2 }, { F_SHARP5, 2 }, { G_SHARP5, 4 }, { F_SHARP5, 2 }, { E5, 2 }, { C_SHARP5, 4 },
    { D_SHARP5, 4 }, { C_SHARP5, 4 }, { B4, 8 },
};
static const SynthNote menuBass[] = {
    { B2, 4 }, { F_SHARP3, 4 }, { B3, 4 }, { F_SHARP3, 4 },
    { G_SHARP2, 4 }, { D_SHARP3, 4 }, { G_SHARP3, 4 }, { D_SHARP3, 4 },
    { E2, 4 }, { B2, 4 }, { E3, 4 }, { B2, 4 },
    { F_SHARP2, 4 }, { C_SHARP3, 4 }, { F_SHARP3, 4 }, { C_SHARP3, 4 },
    { B2, 4 }, { F_SHARP3, 4 }, { B3, 4 }, { F_SHARP3, 4 },
    { G_SHARP2, 4 }, { D_SHARP3, 4 }, { G_SHARP3, 4 }, { D_SHARP3, 4 },
    { E2, 4 }, { B2, 4 }, { E3, 4 }, { B2, 4 },
    { F_SHARP2, 4 }, { C_SHARP3, 4 }, { B2, 8 },
};

static const SynthTrack tracks[MUSIC_COUNT] = {
    [MUSIC_INTRO] = {
        .parts = { introMelody, introBass },
        .counts = { ARRAY_COUNT(introMelody), ARRAY_COUNT(introBass) },
        .waves = { WAVE_ORGAN, WAVE_TRIANGLE },
        .volumes = { 13, 13 },
        .floors = { 8, 7 },
        .samplesPerTick = 57*SAMPLES_PER_MS,
        .releaseSamples = 10*SAMPLES_PER_MS,
    },
    // Quieter and softer than the jingle: it sits under menu navigation sounds.
    [MUSIC_MENU] = {
        .parts = { menuMelody, menuBass },
        .counts = { ARRAY_COUNT(menuMelody), ARRAY_COUNT(menuBass) },
        .waves = { WAVE_SINE, WAVE_TRIANGLE },
        .volumes = { 8, 8 },
        .floors = { 3, 3 },
        .samplesPerTick = 125*SAMPLES_PER_MS,
        .releaseSamples = 20*SAMPLES_PER_MS,
    },
};

//----------------------------------------------------------------------------------
// Voices
//----------------------------------------------------------------------------------

static uint32_t HzToInc(uint32_t hz)
{
    return (uint32_t)(((uint64_t)hz << 32)/SYNTH_SAMPLE_RATE);
}

static uint32_t NoteToInc(uint32_t note)
{
    return topOctaveInc[note%12] >> (10 - note/12);
}

// Sweeps are linear per sample and truncated toward zero, so they never overshoot their
// end values (a fade to 0 cannot wrap negative).
static void VoiceStartSegment(SynthVoice *voice, const SynthSegment *segment)
{
    const uint32_t samples = MAX((uint32_t)segment->ms*SAMPLES_PER_MS, 1u);
    const uint32_t startInc = HzToInc(segment->startHz);
    const uint32_t endInc = HzToInc(segment->endHz);
    voice->wave = waves[segment->wave];
    voice->inc = startInc;
    voice->incDelta = (int32_t)(((int64_t)endInc - (int64_t)startInc)/(int64_t)samples);
    voice->level = (int32_t)segment->volume << 16;
    voice->levelDelta = (((int32_t)segment->volumeEnd - (int32_t)segment->volume)*65536)/(int32_t)samples;
    voice->remaining = samples;
}

// Phase is left running: restarting a busy voice then continues its waveform instead of
// jumping, which keeps rapid retriggers (waka every dot) free of extra clicks.
static void VoiceStartProgram(SynthVoice *voice, const SynthProgram *program)
{
    voice->segment = program->segments;
    voice->segmentEnd = program->segments + program->count;
    voice->loop = (program->loopIndex == NO_LOOP)? NULL : program->segments + program->loopIndex;
    voice->priority = program->priority;
    VoiceStartSegment(voice, voice->segment);
}

static void VoiceStop(SynthVoice *voice)
{
    voice->segment = NULL;
    voice->remaining = 0;
    voice->level = 0;
    voice->levelDelta = 0;
}

static bool AdvanceEffect(SynthVoice *voice)
{
    if (voice->segment == NULL) return false;

    voice->segment++;
    if (voice->segment == voice->segmentEnd)
    {
        if (voice->loop == NULL)
        {
            VoiceStop(voice);
            return false;
        }
        voice->segment = voice->loop;
    }
    VoiceStartSegment(voice, voice->segment);
    return true;
}

// Music notes are a sounding stage that decays one 4-bit step per tick down to the track's
// floor, followed by a silent release gap; rests are one silent stage.
static bool AdvanceMusic(Synth *synth, int part)
{
    SynthChannel *channel = &synth->channels[part];
    SynthVoice *voice = &synth->voices[SYNTH_VOICE_MELODY + part];
    if (channel->note == NULL) return false;

    if (channel->releaseSamples > 0)
    {
        voice->level = 0;
        voice->levelDelta = 0;
        voice->remaining = channel->releaseSamples;
        channel->releaseSamples = 0;
        return true;
    }

    const SynthTrack *track = &tracks[synth->track];
    const SynthNote *end = track->parts[part] + track->counts[part];
    if (channel->note == end)
    {
        if (!synth->loop)
        {
            channel->note = NULL;
            voice->level = 0;
            voice->levelDelta = 0;
            return false;
        }
        channel->note = track->parts[part];
    }

    const SynthNote note = *channel->note++;
    const uint32_t samples = (uint32_t)note.ticks*track->samplesPerTick;
    voice->incDelta = 0;
    if (note.note == 0)
    {
        voice->level = 0;
        voice->levelDelta = 0;
        voice->remaining = samples;
        return true;
    }

    const uint32_t release = MIN(track->releaseSamples, samples/2);
    const uint32_t sounding = samples - release;
    const int32_t volume = track->volumes[part];
    const int32_t volumeEnd = MAX(volume - (int32_t)note.ticks, (int32_t)track->floors[part]);
    voice->wave = waves[track->waves[part]];
    voice->inc = NoteToInc(note.note);
    voice->level = volume << 16;
    voice->levelDelta = ((volumeEnd - volume)*65536)/(int32_t)sounding;
    voice->remaining = sounding;
    channel->releaseSamples = release;
    return true;
}

static bool AdvanceVoice(Synth *synth, int index)
{
    if (index <= SYNTH_VOICE_BASS) return AdvanceMusic(synth, index);
    return AdvanceEffect(&synth->voices[index]);
}

static void StopMusic(Synth *synth)
{
    for (int part = 0; part < 2; part++)
    {
        synth->channels[part].note = NULL;
        synth->channels[part].releaseSamples = 0;
        VoiceStop(&synth->voices[SYNTH_VOICE_MELODY + part]);
    }
    synth->track = MUSIC_NONE;
}

static void RenderVoice(Synth *synth, int index, int32_t *mix, uint32_t frames)
{
    SynthVoice *voice = &synth->voices[index];
    uint32_t done = 0;
    while (done < frames)
    {
        if ((voice->remaining == 0) && !AdvanceVoice(synth, index)) return;

        const uint32_t run = MIN(voice->remaining, frames - done);
        uint32_t phase = voice->phase;
        uint32_t inc = voice->inc;
        int32_t level = voice->level;
        const int32_t incDelta = voice->incDelta;
        const int32_t levelDelta = voice->levelDelta;
        const int8_t *wave = voice->wave;

        // Rests only need the clock to run; the waveform phase is irrelevant in silence.
        if ((level != 0) || (levelDelta != 0))
        {
            int32_t *dst = mix + done;
            for (uint32_t i = 0; i < run; i++)
            {
                dst[i] += (int32_t)wave[phase >> 27]*(level >> LEVEL_SHIFT);
                phase += inc;
                inc += (uint32_t)incDelta;
                level += levelDelta;
            }
        }
        else
        {
            inc += (uint32_t)incDelta*run;
        }

        voice->phase = phase;
        voice->inc = inc;
        voice->level = level;
        voice->remaining -= run;
        done += run;
    }
}

//----------------------------------------------------------------------------------
// Public API
//----------------------------------------------------------------------------------

void SynthInit(Synth *synth)
{
    memset(synth, 0, sizeof(*synth));
    for (int i = 0; i < SYNTH_VOICE_COUNT; i++) synth->voices[i].wave = waves[WAVE_SINE];
    synth->musicGain = SYNTH_GAIN_ONE;
    synth->sfxGain = SYNTH_GAIN_ONE;
}

static void PlaySfx(Synth *synth, SoundEffect sfx)
{
    if (sfx >= SFX_COUNT) return;

    const SynthProgram *program = &sfxPrograms[sfx];
    const bool menuSound = (program->voice == SYNTH_VOICE_MENU);
    if (synth->paused && !menuSound) return;

    if (sfx == SFX_WAKA)
    {
        if (synth->wakaSecond) program = &wakaUpProgram;
        synth->wakaSecond = !synth->wakaSecond;
    }
    else if (sfx == SFX_DEATH)
    {
        VoiceStop(&synth->voices[SYNTH_VOICE_WAKA]);
        synth->wakaSecond = false;
    }

    SynthVoice *voice = &synth->voices[program->voice];
    if ((voice->segment != NULL) && (program->priority < voice->priority)) return;
    VoiceStartProgram(voice, program);
}

static void SetAmbience(Synth *synth, Ambience ambience, int sirenLevel)
{
    const int level = (ambience == AMBIENCE_SIREN)? CLAMP(sirenLevel, 0, SYNTH_SIREN_LEVELS - 1) : 0;
    if ((ambience == synth->ambience) && (level == synth->sirenLevel)) return;

    synth->ambience = (uint8_t)ambience;
    synth->sirenLevel = (uint8_t)level;
    SynthVoice *voice = &synth->voices[SYNTH_VOICE_AMBIENCE];
    switch (ambience)
    {
        case AMBIENCE_SIREN: VoiceStartProgram(voice, &sirenPrograms[level]); break;
        case AMBIENCE_FRIGHT: VoiceStartProgram(voice, &frightProgram); break;
        case AMBIENCE_EYES: VoiceStartProgram(voice, &eyesProgram); break;
        default:
        {
            synth->ambience = AMBIENCE_NONE;
            VoiceStop(voice);
        } break;
    }
}

static void PlayMusic(Synth *synth, MusicTrack track, bool loop)
{
    StopMusic(synth);
    if ((track <= MUSIC_NONE) || (track >= MUSIC_COUNT)) return;

    synth->track = (uint8_t)track;
    synth->loop = loop;
    for (int part = 0; part < 2; part++) synth->channels[part].note = tracks[track].parts[part];
}

void SynthApply(Synth *synth, const SynthCommand *command)
{
    switch (command->type)
    {
        case SYNTH_COMMAND_SFX: PlaySfx(synth, (SoundEffect)command->id); break;
        case SYNTH_COMMAND_MUSIC:
        {
            synth->musicSequence = command->sequence;
            PlayMusic(synth, (MusicTrack)command->id, command->param != 0);
        } break;
        case SYNTH_COMMAND_AMBIENCE: SetAmbience(synth, (Ambience)command->id, command->param); break;
        case SYNTH_COMMAND_VOLUMES:
        {
            synth->musicGain = MIN(command->musicGain, SYNTH_GAIN_ONE);
            synth->sfxGain = MIN(command->sfxGain, SYNTH_GAIN_ONE);
        } break;
        case SYNTH_COMMAND_PAUSE: synth->paused = (command->param != 0); break;
        default: break;
    }
}

void SynthRender(Synth *synth, int16_t *out, uint32_t frames)
{
    int32_t music[RENDER_CHUNK];
    int32_t sfx[RENDER_CHUNK];
    const int32_t musicGain = synth->musicGain;
    const int32_t sfxGain = synth->sfxGain;

    for (uint32_t offset = 0; offset < frames; offset += RENDER_CHUNK)
    {
        const uint32_t count = MIN(frames - offset, (uint32_t)RENDER_CHUNK);
        memset(music, 0, count*sizeof(int32_t));
        memset(sfx, 0, count*sizeof(int32_t));

        RenderVoice(synth, SYNTH_VOICE_MELODY, music, count);
        RenderVoice(synth, SYNTH_VOICE_BASS, music, count);
        // Paused game sounds hold their position and resume where they stopped.
        if (!synth->paused)
        {
            RenderVoice(synth, SYNTH_VOICE_AMBIENCE, sfx, count);
            RenderVoice(synth, SYNTH_VOICE_WAKA, sfx, count);
            RenderVoice(synth, SYNTH_VOICE_EFFECT, sfx, count);
        }
        RenderVoice(synth, SYNTH_VOICE_MENU, sfx, count);

        int16_t *dst = out + offset;
        for (uint32_t i = 0; i < count; i++)
        {
            const int32_t sample = (music[i]*musicGain + sfx[i]*sfxGain) >> MIX_SHIFT;
            dst[i] = (int16_t)CLAMP(sample, INT16_MIN, INT16_MAX);
        }
    }
}

bool SynthMusicPlaying(const Synth *synth)
{
    return (synth->channels[0].note != NULL) || (synth->channels[1].note != NULL);
}

uint32_t SynthMusicSequence(const Synth *synth)
{
    return synth->musicSequence;
}
