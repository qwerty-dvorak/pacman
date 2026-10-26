#include "audio/audio.h"

#include "raylib.h"

#include <assert.h>
#include <limits.h>
#include <string.h>

// Frames per stream sub-buffer. raylib double-buffers the stream, so the queue-to-speaker
// latency is about two of these (43 ms) plus the device period; smaller sizes underrun
// on PulseAudio/PipeWire's default quantum.
#define STREAM_BUFFER_FRAMES    1024

static_assert((AUDIO_QUEUE_CAPACITY & (AUDIO_QUEUE_CAPACITY - 1)) == 0, "queue capacity must be a power of two");

// raylib callbacks carry no user pointer.
static Audio *callbackAudio = NULL;
static AudioStream stream = { 0 };

// Runs on miniaudio's mixing thread: drain commands, render, publish the music state.
static void StreamCallback(void *buffer, unsigned int frames)
{
    Audio *audio = callbackAudio;
    if (audio == NULL) return;

    unsigned int tail = atomic_load_explicit(&audio->queueTail, memory_order_relaxed);
    const unsigned int head = atomic_load_explicit(&audio->queueHead, memory_order_acquire);
    while (tail != head)
    {
        SynthApply(&audio->synth, &audio->queue[tail & (AUDIO_QUEUE_CAPACITY - 1)]);
        tail++;
    }
    atomic_store_explicit(&audio->queueTail, tail, memory_order_release);

    SynthRender(&audio->synth, (int16_t *)buffer, frames);

    const unsigned int state = (SynthMusicSequence(&audio->synth) << 1) | (SynthMusicPlaying(&audio->synth)? 1u : 0u);
    atomic_store_explicit(&audio->musicState, state, memory_order_release);
}

static bool Push(Audio *audio, const SynthCommand *command)
{
    const unsigned int head = atomic_load_explicit(&audio->queueHead, memory_order_relaxed);
    const unsigned int tail = atomic_load_explicit(&audio->queueTail, memory_order_acquire);
    if ((head - tail) >= AUDIO_QUEUE_CAPACITY) return false;

    audio->queue[head & (AUDIO_QUEUE_CAPACITY - 1)] = *command;
    atomic_store_explicit(&audio->queueHead, head + 1, memory_order_release);
    return true;
}

bool AudioInit(Audio *audio)
{
    if (callbackAudio != NULL) return false;
    memset(audio, 0, sizeof(*audio));
    atomic_init(&audio->queueHead, 0);
    atomic_init(&audio->queueTail, 0);
    atomic_init(&audio->musicState, 0);

    InitAudioDevice();
    if (!IsAudioDeviceReady()) return false;

    SetAudioStreamBufferSizeDefault(STREAM_BUFFER_FRAMES);
    stream = LoadAudioStream(SYNTH_SAMPLE_RATE, 16, 1);
    if (!IsAudioStreamValid(stream))
    {
        CloseAudioDevice();
        return false;
    }

    SynthInit(&audio->synth);
    audio->active = true;
    audio->musicGain = SYNTH_GAIN_ONE;
    audio->sfxGain = SYNTH_GAIN_ONE;

    // The pointer is published before the stream starts, so the callback never sees a
    // half-initialized Audio.
    callbackAudio = audio;
    SetAudioStreamCallback(stream, StreamCallback);
    PlayAudioStream(stream);
    return true;
}

void AudioShutdown(Audio *audio)
{
    if (!audio->active) return;

    StopAudioStream(stream);
    UnloadAudioStream(stream);
    CloseAudioDevice();
    stream = (AudioStream){ 0 };
    callbackAudio = NULL;
    audio->active = false;
}

void AudioPlaySfx(Audio *audio, SoundEffect sfx)
{
    if (!audio->active) return;

    const SynthCommand command = { .type = SYNTH_COMMAND_SFX, .id = (uint8_t)sfx };
    Push(audio, &command);
}

void AudioPlayMusic(Audio *audio, MusicTrack track, bool loop)
{
    if (!audio->active) return;

    const SynthCommand command = {
        .type = SYNTH_COMMAND_MUSIC, .id = (uint8_t)track, .param = loop, .sequence = audio->musicSequence + 1
    };
    if (!Push(audio, &command)) return;

    audio->musicSequence++;
    audio->musicRequested = (track > MUSIC_NONE) && (track < MUSIC_COUNT);
}

void AudioSetAmbience(Audio *audio, Ambience ambience, int sirenLevel)
{
    if ((unsigned int)ambience >= AMBIENCE_COUNT) ambience = AMBIENCE_NONE;
    const uint8_t level = (uint8_t)((ambience == AMBIENCE_SIREN)? CLAMP(sirenLevel, 0, SYNTH_SIREN_LEVELS - 1) : 0);
    if (LIKELY((ambience == audio->ambience) && (level == audio->sirenLevel)) || !audio->active) return;

    const SynthCommand command = { .type = SYNTH_COMMAND_AMBIENCE, .id = (uint8_t)ambience, .param = level };
    if (!Push(audio, &command)) return;

    audio->ambience = (uint8_t)ambience;
    audio->sirenLevel = level;
}

void AudioSetVolumes(Audio *audio, float music, float sfx)
{
    const uint16_t musicGain = (uint16_t)(CLAMP(music, 0.0f, 1.0f)*SYNTH_GAIN_ONE + 0.5f);
    const uint16_t sfxGain = (uint16_t)(CLAMP(sfx, 0.0f, 1.0f)*SYNTH_GAIN_ONE + 0.5f);
    if (((musicGain == audio->musicGain) && (sfxGain == audio->sfxGain)) || !audio->active) return;

    const SynthCommand command = { .type = SYNTH_COMMAND_VOLUMES, .musicGain = musicGain, .sfxGain = sfxGain };
    if (!Push(audio, &command)) return;

    audio->musicGain = musicGain;
    audio->sfxGain = sfxGain;
}

void AudioSetPaused(Audio *audio, bool paused)
{
    if ((paused == audio->paused) || !audio->active) return;

    const SynthCommand command = { .type = SYNTH_COMMAND_PAUSE, .param = paused };
    if (Push(audio, &command)) audio->paused = paused;
}

// Until the callback has applied the latest music command its published state is stale,
// so the request itself answers.
bool AudioMusicPlaying(const Audio *audio)
{
    if (!audio->active) return false;

    const unsigned int state = atomic_load_explicit(&audio->musicState, memory_order_acquire);
    if ((state >> 1) != (audio->musicSequence & (UINT_MAX >> 1))) return audio->musicRequested;
    return (state & 1u) != 0;
}
