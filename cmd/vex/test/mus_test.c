// Sequencer conformance for mus.h: drives the tracker with a stub tone()
// that records calls, pinning trigger/rest/off semantics, the sustain
// default (speed*2), per-note volume overrides with peak-at-vol, the
// end-of-song stop, and out-of-range instrument bounds checks.

#include <stdio.h>
#include <string.h>

// Redirect tone() to a recorder before mus.h (via vex.h) declares it.
#define tone mus_record_tone
#include "mus.h"
#undef tone

#define MAX_CALLS 64
static struct {
    int freq, dur, vol, flags;
} g_calls[MAX_CALLS];
static int g_ncalls = 0;

void mus_record_tone(int freq, int duration, int volume, int flags) {
    if (g_ncalls < MAX_CALLS) {
        g_calls[g_ncalls].freq = freq;
        g_calls[g_ncalls].dur = duration;
        g_calls[g_ncalls].vol = volume;
        g_calls[g_ncalls].flags = flags;
        g_ncalls++;
    }
}

static int failures = 0;

#define CHECK(desc, cond) do {                                              \
        if (!(cond)) {                                                      \
            fprintf(stderr, "FAIL: %s\n", desc);                            \
            failures++;                                                     \
        }                                                                   \
    } while (0)

static const MusInst INSTS[] = {
    {VEX_TONE_PULSE, VEX_TONE_MODE0, 0, 1, 0, 4, 80, 0},
    {VEX_TONE_NOISE, 0,              0, 0, 5, 2, 40, 0},
};

static const MusEvent EVENTS[4 * MUS_CHANNELS] = {
    // row 0: note + note-off + rests
    {69, 1, 0}, {MUS_OFF, 0, 0}, {MUS_REST, 0, 0}, {MUS_REST, 0, 0},
    // row 1: all rest — previous note rings, no new triggers
    {MUS_REST, 0, 0}, {MUS_REST, 0, 0}, {MUS_REST, 0, 0}, {MUS_REST, 0, 0},
    // row 2: per-note volume override
    {60, 2, 33}, {MUS_REST, 0, 0}, {MUS_REST, 0, 0}, {MUS_REST, 0, 0},
    // row 3: out-of-range instrument is skipped
    {62, 9, 0}, {MUS_REST, 0, 0}, {MUS_REST, 0, 0}, {MUS_REST, 0, 0},
};

static const MusPat PAT = {4, 2, EVENTS};
static const MusPat *const PATS[] = {&PAT};
static const unsigned char ORDERS[] = {0};
static const MusSong SONG = {2, 1, 1, 0xFF, INSTS, PATS, ORDERS};

static void tick_n(int n) {
    for (int i = 0; i < n; i++)
        mus_tick();
}

int main(void) {
    CHECK("MIDI 69 renders at 440 Hz", _mus_note_hz(69) == 440);
    CHECK("MIDI 60 renders near 261 Hz", _mus_note_hz(60) == 261);

    mus_load(&SONG);
    CHECK("load resets position", mus_pos() == 0);
    mus_play();

    // Row 0: note trigger + OFF silence.
    tick_n(2);
    CHECK("row 0 triggers note and off", g_ncalls == 2);
    CHECK("note freq is 440 Hz", g_calls[0].freq == 440);
    CHECK("default volume peaks at level",
          g_calls[0].vol == VEX_TONE_VOLUME(80, 80));
    CHECK("zero sustain defaults to speed*2",
          (g_calls[0].dur & 0xFF) == 2 * 2);
    CHECK("note lands on channel 0", (g_calls[0].flags & 3) == 0);
    CHECK("off silences its channel",
          g_calls[1].dur == 0 && g_calls[1].vol == 0 &&
              (g_calls[1].flags & 3) == 1);
    CHECK("position advances one row", mus_pos() == (1 << 8));

    // Row 1: rest rings, nothing new.
    tick_n(2);
    CHECK("rest row triggers nothing", g_ncalls == 2);

    // Row 2: volume override + explicit sustain.
    tick_n(2);
    CHECK("override row triggers once", g_ncalls == 3);
    CHECK("override note uses table pitch",
          g_calls[2].freq == _mus_note_hz(60));
    CHECK("override volume peaks at override",
          g_calls[2].vol == VEX_TONE_VOLUME(33, 33));
    CHECK("explicit sustain passes through",
          (g_calls[2].dur & 0xFF) == 5);

    // Row 3: bad instrument skipped, song ends with all-channel silence.
    tick_n(2);
    CHECK("end of song stops with 4 silences", g_ncalls == 3 + 4);
    tick_n(4);
    CHECK("stopped song stays silent", g_ncalls == 3 + 4);

    // Reload restarts the song from the top.
    mus_load(&SONG);
    mus_play();
    CHECK("reload resets position", mus_pos() == 0);
    tick_n(2);
    CHECK("reloaded song triggers again", g_ncalls == 3 + 4 + 2);

    if (failures) {
        fprintf(stderr, "MUS: %d failure(s)\n", failures);
        return 1;
    }
    printf("MUS: all ok\n");
    return 0;
}
