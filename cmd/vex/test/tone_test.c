// Behavioral test of the C host's audio implementation (cmd/vex/main.c).
//
// The real audio section is #included verbatim (audio_section.inc, extracted
// from main.c by the `test-hosts` Makefile target); raylib's and wasm3's API
// surface is replaced by recording stubs. The streaming mixer's callback is
// driven manually with deterministic frame counters, asserting the same
// semantics as the Go toneEngine tests (cmd/vex-run/main_test.go) and the
// vex.js mock test (cmd/vex-web/test/tone_test.js):
//
//   - channel clamps to 0..3
//   - freq <= 0 silences; freq > 20000 clamps
//   - ms < 0: flat 100ms blip at ±8000/32768, starting positive
//   - ms == 0: sustain -- constant amplitude past 1s until replaced
//   - ms > 0: decay reaching ~amp/256 at the last sample
//   - vol scales linearly and live; clamps to 0..10
//   - noise produces non-zero output with a zero-crossing rate distinct
//     from the square voice at the same frequency
//   - apos advances monotonically at exactly frames-produced rate
#include <assert.h>
#include <math.h>
#include <stdatomic.h>
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
#define m3ApiReturnType(T) T* raw_return = (T*)(_sp++);
#define m3ApiReturn(V) (*raw_return = (V))

// Fake linear memory + runtime handle for sample() byte copies.
typedef struct M3Runtime* IM3Runtime;
static IM3Runtime g_fake_runtime;
#define runtime g_fake_runtime
static uint8_t fake_mem[128 * 1024];
static void* _mem = fake_mem;
static unsigned m3_GetMemorySize(IM3Runtime rt) {
    (void)rt;
    return sizeof(fake_mem);
}

// ---- raylib audio stubs ----------------------------------------------------
typedef struct {
    struct { int sampleRate; } stream;
} Sound;

typedef struct {
    unsigned frameCount;
    unsigned sampleRate;
    unsigned sampleSize;
    unsigned channels;
    void* data;
} Wave;

typedef struct {
    int sampleRate;
} AudioStream;

static int fake_device_rate = 48000;
static bool g_playing = false;
static void* g_callback = NULL;
static bool g_stream_playing = false;

typedef void (*AudioCallback)(void* buffer, unsigned int frames);

static Sound LoadSoundFromWave(Wave w) {
    Sound s;
    s.stream.sampleRate = fake_device_rate;
    return s;
}
static void UnloadSound(Sound s) { (void)s; }
static void InitAudioDevice(void) {}
static void CloseAudioDevice(void) {}
static bool IsAudioDeviceReady(void) { return true; }

static AudioStream LoadAudioStream(unsigned rate, unsigned size, unsigned ch) {
    AudioStream s = { .sampleRate = (int)rate };
    return s;
}
static void UnloadAudioStream(AudioStream s) { (void)s; }
static void StopAudioStream(AudioStream s) { g_stream_playing = false; }
static void PlayAudioStream(AudioStream s) { g_stream_playing = true; }
static void SetAudioStreamCallback(AudioStream s, AudioCallback cb) { g_callback = cb; }

// ---- the real host code ----------------------------------------------------
#define main main_unused
#include "audio_section.inc"
#undef main

int failures = 0;
#define CHECK(name, cond) do { \
    if (!(cond)) { failures++; printf("FAIL: %s\n", name); } \
    else printf("ok: %s\n", name); \
} while (0)

static int32_t* args5(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e) {
    static int32_t v[5];
    v[0] = a; v[1] = b; v[2] = c; v[3] = d; v[4] = e;
    g_args = v;
    return v;
}

static int32_t* args(int32_t a, int32_t b, int32_t c) {
    static int32_t v[3];
    v[0] = a; v[1] = b; v[2] = c;
    g_args = v;
    return v;
}

// Drive the mixer callback for `frames` frames in device-sized chunks,
// returning one channel's post-mix sample per frame.
static float* rendered;
static int run_mixer(int frames) {
    rendered = realloc(rendered, sizeof(float) * frames);
    static float buf[8192];
    int produced = 0;
    while (produced < frames) {
        unsigned chunk = frames - produced > 4096 ? 4096 : (unsigned)(frames - produced);
        memset(buf, 0, sizeof(buf));
        mix_callback(buf, chunk);
        for (unsigned i = 0; i < chunk && produced < frames; i++)
            rendered[produced++] = buf[i * 2];
    }
    return produced;
}

int main(void) {
    g_audio_ready = true;

    // Sustained tone holds flat amplitude past 1 second...
    host_tone(args(0, 440, 0));
    CHECK("stream opened on first event", g_stream_ready);
    int n = run_mixer(fake_device_rate * 3 / 2); // 1.5s
    CHECK("apos counts produced frames", atomic_load(&g_apos) == (uint64_t)n);
    float expect = 8000.0f / 32768.0f;
    bool held = true;
    for (int i = fake_device_rate; i < n; i += 97) // probe past 1s
        if (fabsf(fabsf(rendered[i]) - expect) > expect * 1e-4f) held = false;
    CHECK("sustain holds flat amplitude past 1s", held);
    CHECK("sustain starts positive at phase 0", fabsf(rendered[0] - expect) < expect * 1e-4f);

    // ...until a zero-freq event silences it.
    host_tone(args(0, 0, 0));
    int n2 = run_mixer(64);
    bool silent = true;
    for (int i = 0; i < n2; i++) if (rendered[i] != 0.0f) silent = false;
    CHECK("freq 0 silences sustained voice", silent);

    // Volume scales linearly and live.
    host_vol(args(1, 32, 0));
    host_tone(args(1, 440, 0)); // sustain on ch1
    run_mixer(64);
    CHECK("vol(32) halves amplitude",
          fabsf(fabsf(rendered[63]) - expect * 0.5f) < expect * 1e-4f);
    host_vol(args(1, 99, 0)); // clamp to unity
    run_mixer(64);
    CHECK("vol clamps high to unity",
          fabsf(fabsf(rendered[63]) - expect) < expect * 1e-4f);

    // Legacy blip ends after ~100ms; decay reaches ~peak/256 at its end.
    host_tone(args(1, 0, 0)); // retire the volume-test voice first
    host_tone(args(2, 1000, -1));
    int blip = run_mixer(fake_device_rate / 5); // 200ms window
    CHECK("legacy blip starts at peak", fabsf(rendered[0] - expect) < expect * 1e-4f);
    bool blip_tail_silent = true;
    for (int i = fake_device_rate / 10 + 64; i < blip; i++)
        if (rendered[i] != 0.0f) blip_tail_silent = false;
    CHECK("legacy blip ends after 100ms", blip_tail_silent);

    host_tone(args(2, 1000, 250));
    run_mixer(fake_device_rate * 250 / 1000);
    float end_amp = fabsf(rendered[fake_device_rate * 250 / 1000 - 1]);
    CHECK("decay reaches ~peak/256 at end",
          end_amp > expect / 256 * 0.9f && end_amp < expect / 256 * 1.05f);

    // Noise differs from square at same frequency (zero-crossing structure).
    int probes = fake_device_rate / 50; // 20ms each
    host_tone(args(3, 1000, 0));
    run_mixer(probes);
    int sq_flips = 0;
    for (int i = 1; i < probes; i++)
        if ((rendered[i - 1] > 0) != (rendered[i] > 0)) sq_flips++;
    host_noise(args(3, 1000, 0));
    run_mixer(probes);
    int nz_flips = 0;
    bool nonzero = false;
    for (int i = 1; i < probes; i++) {
        if ((rendered[i - 1] > 0) != (rendered[i] > 0)) nz_flips++;
        if (rendered[i] != 0.0f) nonzero = true;
    }
    CHECK("noise output nonzero", nonzero);
    CHECK("noise zero-crossings differ from square",
          nz_flips != sq_flips && nz_flips > 0);
    host_noise(args(3, 0, 0)); // retire the sustained noise voice
    run_mixer(16);

    // apos monotonicity across many callback invocations.
    uint64_t last = atomic_load(&g_apos);
    bool mono = true;
    for (int i = 0; i < 50; i++) {
        run_mixer(1234);
        uint64_t now = atomic_load(&g_apos);
        if (now <= last) mono = false;
        last = now;
    }
    CHECK("apos advances monotonically", mono);

    // pitch(): retune a sustained square without restarting it.
    host_tone(args(0, 250, 0)); // half = 96 samples at 48k
    run_mixer(200);
    host_pitch(args(0, 500, 0)); // half -> 48
    int toggles = 0;
    int ptoggles = fake_device_rate * 480 / 48000; // 480 frames
    run_mixer(ptoggles);
    for (int i = 1; i < ptoggles; i++)
        if ((rendered[i - 1] > 0) != (rendered[i] > 0)) toggles++;
    // ~10 polarity toggles expected for 500 Hz over 480 frames (one of
    // slack either way for where in the old cycle the retune landed).
    CHECK("pitch retunes cadence mid-note", toggles >= 9 && toggles <= 11);

    // pitch on a silent channel must be a harmless no-op.
    host_tone(args(0, 0, 0));
    run_mixer(16);
    atomic_store(&g_new_half[0], 12345);
    run_mixer(16);
    bool still_silent = true;
    for (int i = 0; i < 16; i++)
        if (rendered[i] != 0.0f) still_silent = false;
    CHECK("pitch on silent channel stays silent", still_silent);

    // sample(): one-shot quiet ramp, exact first value, natural end.
    memset(fake_mem, 0, sizeof(fake_mem));
    for (int i = 0; i < 96; i++) ((int8_t*)fake_mem)[i] = (int8_t)(i - 24);
    host_sample(args5(1, 0, 96, fake_device_rate, 0));
    n2 = run_mixer(fake_device_rate / 20); // 50ms window
    // First rendered frame: PCM[0] = -24 * 256 = -6144 (linear region).
    CHECK("sample starts at first PCM byte", fabsf(rendered[0] * 32768.0f + 6144.0f) < 1.0f);
    bool ended = true;
    // 96 bytes @ 48000 Hz = 96 frames; probe past the end.
    for (int i = fake_device_rate / 500 + 96 + 8; i < n2; i++)
        if (rendered[i] != 0.0f) ended = false;
    CHECK("one-shot sample ends after len/rate", ended);

    // Tail loop sustains until silenced.
    host_sample(args5(1, 0, 96, fake_device_rate, 48));
    run_mixer(fake_device_rate / 5);
    bool loop_live = false;
    for (int i = 96 + 16; i < n2; i++) // past the first pass of 96 bytes
        if (rendered[i] != 0.0f) loop_live = true;
    CHECK("tail loop keeps sounding past one pass", loop_live);
    host_tone(args(1, 0, 0));
    n2 = run_mixer(64);
    bool loop_gone = true;
    for (int i = 0; i < n2; i++)
        if (rendered[i] != 0.0f) loop_gone = false;
    CHECK("silencing stops looped sample", loop_gone);

    // Caps: rate clamps high, oversized len truncates at TONE_SAMPLE_MAX.
    memset(fake_mem, 0x7F, sizeof(fake_mem));
    host_sample(args5(2, 0, 999999, 999999, 0));
    n2 = run_mixer(64);
    CHECK("clamped sample sounds", rendered[0] > 0.0f || rendered[63] != 0.0f);

    // Virtual clock: with no live stream draining the mixer, the per-frame
    // tick advances apos by 800 samples/frame (48 kHz / 60 fps); with a
    // live stream it must be a no-op.
    g_stream_ready = false;
    atomic_store(&g_apos, 0);
    for (int i = 0; i < 60; i++) vex_audio_frame_tick();
    CHECK("headless virtual clock: 800 samples/frame",
          atomic_load(&g_apos) == 60 * 800);

    g_stream_ready = true;
    atomic_store(&g_apos, 100000);
    for (int i = 0; i < 60; i++) vex_audio_frame_tick();
    CHECK("virtual clock inert while stream is live",
          atomic_load(&g_apos) == 100000);

    printf(failures ? "C HOST: %d FAILURES\n" : "C HOST: all ok\n", failures);
    free(rendered);
    return failures != 0;
}
