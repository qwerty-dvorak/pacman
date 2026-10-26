#ifndef AUDIO_AUDIO_H
#define AUDIO_AUDIO_H

#include "audio/synth.h"

#include <stdalign.h>
#include <stdatomic.h>

// Sound output through raylib's audio device (miniaudio) and a 48 kHz mono 16-bit stream
// whose callback runs the synthesizer. All functions are for the main thread only.
//
// Audio is a concrete struct so the app can embed it without allocating; it is 64-byte
// aligned, so heap copies need aligned_alloc. Only one Audio can be initialized at a time
// (raylib's stream callback is a plain function, bound to a single static instance).
// A zeroed or failed-to-initialize Audio turns every call into a no-op.
//
// The main thread never touches the Synth after AudioInit: commands travel through a
// single-producer/single-consumer ring the callback drains before rendering. A full ring
// drops sound effects; state changes (ambience, volumes, pause) are retried on the next
// call because their cached "last sent" values only update once queued.

#define AUDIO_QUEUE_CAPACITY    128     // power of two

typedef struct Audio {
    Synth synth;                                    // owned by the audio callback

    alignas(CACHE_LINE_SIZE) atomic_uint queueHead; // written by the main thread
    alignas(CACHE_LINE_SIZE) atomic_uint queueTail; // written by the callback
    alignas(CACHE_LINE_SIZE) atomic_uint musicState;    // callback: sequence << 1 | playing
    SynthCommand queue[AUDIO_QUEUE_CAPACITY];

    // Main thread only.
    alignas(CACHE_LINE_SIZE) bool active;
    bool paused;
    bool musicRequested;            // last music command started a track
    uint8_t ambience;
    uint8_t sirenLevel;
    uint16_t musicGain;
    uint16_t sfxGain;
    uint32_t musicSequence;
} Audio;

bool AudioInit(Audio *audio);           // false when no device: other calls are then no-ops
void AudioShutdown(Audio *audio);

void AudioPlaySfx(Audio *audio, SoundEffect sfx);
void AudioPlayMusic(Audio *audio, MusicTrack track, bool loop);     // MUSIC_NONE stops
// Cheap when unchanged, so it may be called every frame; sirenLevel 0..4 (siren only).
void AudioSetAmbience(Audio *audio, Ambience ambience, int sirenLevel);
void AudioSetVolumes(Audio *audio, float music, float sfx);         // 0..1 each
void AudioSetPaused(Audio *audio, bool paused);  // holds game sounds; music and menu sfx play
// True from AudioPlayMusic until the track ends or is stopped (a looping track never ends).
bool AudioMusicPlaying(const Audio *audio);

#endif // AUDIO_AUDIO_H
