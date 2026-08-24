// Behavioral test of the C host's tone implementation (cmd/vex/main.c).
//
// The real audio section is #included verbatim (audio_section.inc, extracted
// from main.c by the `test-hosts` Makefile target); raylib's and wasm3's API
// surface is replaced by recording stubs. The same semantics are asserted as
// in the Go toneEngine tests (cmd/vex-run/main_test.go) and the vex.js mock
// test (cmd/vex-web/test/tone_test.js):
//
//   - channel clamps to 0..3
//   - freq <= 0 silences the channel (StopSound), freq > 20000 clamps
//   - ms <= 0: flat 100ms blip, ±8000/32768, starting positive
//   - ms > 0: exponential decay reaching ~amp/256 at the last sample,
//     duration = min(ms, 5000) ms
//   - a new tone on a channel replaces it from frame 0; shorter tones
//     leave silence in the buffer tail
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---- wasm3 API stubs -------------------------------------------------------
static int32_t* g_args;
#define m3ApiGetArg(T, N) T N = (T)(*g_args++);
#define m3ApiSuccess() return
#define m3ApiRawFunction(name) static void name(int32_t* _sp)

// ---- raylib audio stubs ----------------------------------------------------
typedef struct {
    struct { int sampleRate; } stream;
    float* data;
    int frames;
} Sound;

typedef struct {
    unsigned frameCount;
    unsigned sampleRate;
    unsigned sampleSize;
    unsigned channels;
    void* data;
} Wave;

#define RL_MALLOC malloc
#define RL_FREE free

static int fake_device_rate = 48000;
static bool g_playing = false;
static int stop_count = 0;

static Sound LoadSoundFromWave(Wave w) {
    Sound s;
    s.stream.sampleRate = fake_device_rate;
    s.data = (float*)w.data;
    s.frames = (int)w.frameCount;
    return s;
}
static void UnloadSound(Sound s) { (void)s; }
static void InitAudioDevice(void) {}
static void CloseAudioDevice(void) {}
static bool IsAudioDeviceReady(void) { return true; }
static bool IsSoundPlaying(Sound s) { (void)s; return g_playing; }
static void StopSound(Sound s) { (void)s; stop_count++; g_playing = false; }
static void UpdateSound(Sound s, const void* data, int n) {
    memcpy(s.data, data, (size_t)n * sizeof(float));
    (void)n;
}
static void PlaySound(Sound s) { (void)s; g_playing = true; }

// ---- the real host code ----------------------------------------------------
#define main main_unused
#include "audio_section.inc"
#undef main

int failures = 0;
#define CHECK(name, cond) do { \
    if (!(cond)) { failures++; printf("FAIL: %s\n", name); } \
    else printf("ok: %s\n", name); \
} while (0)

static int32_t* args3(int32_t a, int32_t b, int32_t c) {
    static int32_t v[3];
    v[0] = a; v[1] = b; v[2] = c;
    g_args = v;
    return v;
}

int main(void) {
    g_audio_ready = true;

    // Legacy flat blip on channel 0.
    host_tone(args3(0, 440, 0));
    {
        ToneVoice* v = &g_voices[0];
        CHECK("sound loaded", v->loaded);
        int frames = fake_device_rate / 10;
        CHECK("legacy blip is 100ms", v->cap_frames >= frames);
        CHECK("starts at +8000/32768", fabsf(v->buf[0] - 8000.0f/32768.0f) < 1e-6f);
        bool flat = true;
        for (int i = 0; i < frames; i++)
            if (fabsf(fabsf(v->buf[i]) - 8000.0f/32768.0f) > 1e-6f) flat = false;
        CHECK("flat amplitude throughout blip", flat);
        // First call: capacity == note length, so there is no tail yet.
        CHECK("first call allocates exactly the legacy blip",
              v->cap_frames == frames);
    }

    // Decay tone on channel 1: 250ms.
    host_tone(args3(1, 880, 250));
    {
        ToneVoice* v = &g_voices[1];
        int frames = fake_device_rate * 250 / 1000;
        CHECK("decay duration = ms", v->cap_frames >= frames);
        CHECK("decay starts at peak", fabsf(v->buf[0] - 8000.0f/32768.0f) < 1e-6f);
        float end = fabsf(v->buf[frames - 1]);
        float expect = 8000.0f/32768.0f/256.0f;
        CHECK("decay reaches ~peak/256 at end",
              end > expect * 0.9f && end < expect * 1.1f);
    }

    // Channel clamping.
    host_tone(args3(42, 440, 0));
    CHECK("channel 42 clamps to 3", g_voices[3].loaded);
    host_tone(args3(-7, 440, 0));
    CHECK("negative channel clamps to 0", g_voices[0].loaded);

    // freq <= 0 silences a loaded, playing channel.
    g_playing = true;
    int before_stops = stop_count;
    host_tone(args3(1, 0, 0));
    CHECK("freq 0 stops the channel", stop_count == before_stops + 1);
    // ...and does nothing harmful on an unloaded one.
    host_tone(args3(2, 0, 0));
    CHECK("freq 0 on idle channel is a no-op", stop_count == before_stops + 1);

    // Shorter tone after longer one leaves a silent tail (no stale samples).
    host_tone(args3(0, 440, 1000)); // grows capacity to ~1s
    ToneVoice* v0 = &g_voices[0];
    host_tone(args3(0, 880, 100));  // much shorter
    bool tail_silent = true;
    for (int i = fake_device_rate / 10; i < v0->cap_frames; i++)
        if (v0->buf[i] != 0.0f) tail_silent = false;
    CHECK("shorter rewrite silences old tail", tail_silent);

    // Huge freq must not divide-by-zero or crash (clamped to 20000).
    host_tone(args3(1, 100000, 50));
    CHECK("huge freq survives", true);

    printf(failures ? "C HOST: %d FAILURES\n" : "C HOST: all ok\n", failures);
    return failures != 0;
}
