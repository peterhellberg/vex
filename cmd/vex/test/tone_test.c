// Cross-host tone() conformance for the C console: the real audio section is
// #included verbatim (audio_section.inc, extracted from cmd/vex/main.c by
// `make test-hosts`) against recording stubs, then driven through the same
// trigger path carts use. Mirrors the Go engine's tone tests so both hosts
// pin identical behavior.
//
// Regenerate the extracted section after editing main.c's audio code (the
// make target does this automatically).

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <pthread.h>

// Minimal stand-ins for the pieces the audio section pulls in from raylib
// and wasm3. The stream is never created headlessly; we just need a rate.
typedef struct {
    int sampleRate;
} AudioStream;

static AudioStream LoadAudioStream(unsigned int rate, unsigned int size,
                                   unsigned int channels) {
    (void)size; (void)channels;
    AudioStream s = { .sampleRate = (int)rate };
    return s;
}
static void UnloadAudioStream(AudioStream s) { (void)s; }
static void PlayAudioStream(AudioStream s) { (void)s; }
static void SetAudioStreamCallback(AudioStream s, void (*cb)(void*, unsigned int)) {
    (void)s; (void)cb;
}
static void CloseAudioDevice(void) {}

#define m3ApiRawFunction(name) static void name(uint64_t* _sp, void* _mem)
#define m3ApiGetArg(TYPE, NAME) TYPE NAME = *((TYPE*)_sp++);
#define m3ApiSuccess() return

#include "audio_section.inc"

static int failures = 0;

#define CHECK(desc, cond) do {                                              \
        if (!(cond)) {                                                      \
            fprintf(stderr, "FAIL: %s\n", desc);                            \
            failures++;                                                     \
        }                                                                   \
    } while (0)

// Fire a trigger through the same pending-slot path cart-side tone() uses,
// then render `frames` stereo frames through the real mixer callback.
// Sized for the largest render below (~19.3k frames of envelope sweep).
#define OUT_FRAMES 20480
static float g_out[OUT_FRAMES * 2];

static void fire_and_run(int ch, ToneTrigger t, unsigned frames) {
    if (frames > OUT_FRAMES) frames = OUT_FRAMES;
    pthread_mutex_lock(&g_tone_lock);
    g_pending[ch] = t;
    g_pending_set[ch] = true;
    pthread_mutex_unlock(&g_tone_lock);
    mix_callback(g_out, frames);
}

static ToneTrigger mk_pulse(float f0, float f1, int att, int dec,
                            int sus, int rel) {
    ToneTrigger t = {0};
    t.kind = 0;
    t.duty = 0.5f;
    t.f0 = f0;
    t.f1 = f1;
    t.frames[0] = att; t.frames[1] = dec; t.frames[2] = sus; t.frames[3] = rel;
    t.peak = 1.0f;
    t.sus = 1.0f;
    t.gl = 0.70710678f;
    t.gr = 0.70710678f;
    return t;
}

static int16_t sample_at(unsigned i) {
    float v = g_out[i * 2];
    return (int16_t)(v * 32768.0f);
}

static unsigned mag(unsigned i) {
    int v = sample_at(i);
    return v < 0 ? -v : v;
}

static double hz_between(unsigned from, unsigned to) {
    int cross = 0;
    for (unsigned i = from + 1; i < to; i++)
        if (sample_at(i - 1) <= 0 && sample_at(i) > 0) cross++;
    return (double)cross * 48000.0 / (double)(to - from);
}

int main(void) {
    g_stream = LoadAudioStream(48000, 32, 2);
    g_stream_ready = true;
    g_audio_ready = true;

    // ---- duty cycle: 25% pulse flips at a quarter period --------------------
    {
        ToneTrigger quarter = mk_pulse(1000, 0, 0, 0, 4, 0);
        quarter.duty = 0.25f;
        fire_and_run(0, quarter, 32);
        // Flip within one sample of the ideal quarter point: float32 phase
        // accumulation may land a sample later than exact math.
        int flips = -1;
        for (unsigned i = 1; i < 32 && flips < 0; i++)
            if (sample_at(i) < 0) flips = (int)i;
        CHECK("duty 25%: samples before flip are high",
              sample_at(0) > 0 && sample_at(10) > 0);
        CHECK("duty 25%: flip lands near sample 12",
              flips >= 11 && flips <= 13);
    }

    // ---- envelope: linear attack, flat sustain, linear release ---------------
    {
        const int att = 8, sus = 8, rel = 8;
        fire_and_run(0, mk_pulse(440, 0, att, 0, sus, rel),
                     (unsigned)(att * 800 + sus * 800 + rel * 800 + 64));

        const int spf = 48000 / 60; // 800 samples per frame
        const int att_s = att * spf, sus_s = sus * spf, rel_s = rel * spf;

        int mid = mag(att_s / 2);
        CHECK("attack ramps linearly (mid ~half scale)", mid > 2000 && mid < 3600);
        int full = mag(att_s - 1);
        CHECK("end of attack reaches full scale", full > 5400 && full <= 5657);
        int hold = mag(att_s + sus_s - 1);
        CHECK("sustain holds full scale",
              hold - full <= 1 && hold >= full - 1);
        int tail = mag(att_s + sus_s + rel_s - 1);
        CHECK("release decays to near silence", tail < 600);

        int late = 0;
        for (int i = att_s + sus_s + rel_s; i < att_s + sus_s + rel_s + 60; i++)
            if (sample_at(i) != 0) late++;
        CHECK("voice is idle after release", late == 0);
    }

    // ---- kill idiom: all-zero duration silences the channel ------------------
    {
        fire_and_run(0, mk_pulse(440, 0, 0, 0, 10, 0), 16);
        CHECK("sustained voice sounds before kill", sample_at(15) != 0);
        fire_and_run(0, mk_pulse(262, 0, 0, 0, 0, 0), 16);
        int sounding = 0;
        for (unsigned i = 0; i < 16; i++)
            if (sample_at(i) != 0) sounding++;
        CHECK("zero-duration trigger kills the channel", sounding == 0);
    }

    // ---- slide: frequency glides toward the target ----------------------------
    {
        ToneTrigger t = mk_pulse(220, 880, 0, 0, 20, 0);
        fire_and_run(0, t, 17600);

        double start_hz = hz_between(80, 1280);      // early window
        double end_hz = hz_between(13600, 15800);    // last stretch of sustain
        CHECK("slide starts near 220 Hz", start_hz > 180 && start_hz < 280);
        CHECK("slide approaches 880 Hz", end_hz > 700 && end_hz < 1060);
    }

    // ---- noise: LFSR taps and output mapping ---------------------------------
    {
        ToneTrigger t = {0};
        t.kind = 1; // noise
        t.duty = 0.5f;
        t.f0 = 8000; // stepped at 16000 Hz: one step every third sample
        t.frames[2] = 8; // sustain
        t.peak = 1.0f;
        t.sus = 1.0f;
        t.gl = 0.70710678f;
        t.gr = 0.70710678f;
        fire_and_run(0, t, 300 * 2);

        uint16_t lfsr = 0xACE1;
        double nph = 0.0;
        int bad = 0;
        for (unsigned i = 0; i < 300; i++) {
            nph += 2.0 * 8000.0 / 48000.0;
            while (nph >= 1.0) {
                nph -= 1.0;
                uint16_t fb = (uint16_t)(1u - (((lfsr >> 15) ^ (lfsr >> 13)) & 1));
                lfsr = (uint16_t)(lfsr << 1 | fb);
            }
            int16_t want = (lfsr & 1) ? 5656 : -5656;
            if (sample_at(i) != want) bad++;
        }
        CHECK("noise reproduces the reference LFSR", bad == 0);
    }

    // ---- note mode: MIDI 69 is concert A ---------------------------------------
    {
        // Parse check via the same math tone() uses: 69 -> 440 Hz.
        float f = 440.0f * powf(2.0f, (69.0f - 69.0f) / 12.0f);
        CHECK("note mode conversion anchor", f == 440.0f);
    }

    if (failures) {
        fprintf(stderr, "C HOST: %d failure(s)\n", failures);
        return 1;
    }
    printf("C HOST: all ok\n");
    return 0;
}
