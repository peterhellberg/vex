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
    {VEX_TONE_PULSE, VEX_TONE_MODE0, 0, 0, 7, 4, 60, 0},
    {VEX_TONE_PULSE, VEX_TONE_MODE0, 0, 0, MUS_SUSTAIN_HOLD, 3, 50, 0},
    {VEX_TONE_PULSE, VEX_TONE_MODE0, 0, 0, 0, 1, 0, 0},
};

static const MusEvent EVENTS[4 * MUS_CHANNELS] = {
    {69, 1, 0}, {67, 1, 0}, {MUS_REST, 0, 0}, {MUS_REST, 0, 0},
    {MUS_REST, 0, 0}, {MUS_OFF, 0, 0}, {MUS_REST, 0, 0}, {MUS_REST, 0, 0},
    {129, 3, 33}, {60, 2, 0}, {MUS_REST, 0, 0}, {MUS_REST, 0, 0},
    {62, 9, 0}, {MUS_OFF, 0, 0}, {MUS_REST, 0, 0}, {MUS_REST, 0, 0},
};

static const MusPat PAT = {4, 2, EVENTS};
static const MusPat *const PATS[] = {&PAT};
static const unsigned char ORDERS[] = {0};
static const MusSong SONG = {5, 1, 1, 0xFF, INSTS, PATS, ORDERS};

static const MusEvent ARP_EVENTS[2 * MUS_CHANNELS] = {
    {129, 3, 0}, {MUS_REST, 0, 0}, {MUS_REST, 0, 0}, {MUS_REST, 0, 0},
    {MUS_REST, 0, 0}, {MUS_REST, 0, 0}, {MUS_REST, 0, 0}, {MUS_REST, 0, 0},
};
static const MusPat ARP_PAT = {2, 7, ARP_EVENTS};
static const MusPat *const ARP_PATS[] = {&ARP_PAT};
static const MusSong ARP_SONG = {5, 1, 1, 0xFF, INSTS, ARP_PATS, ORDERS};

static const MusEvent HOLD_EVENTS[2 * MUS_CHANNELS] = {
    {60, 4, 0}, {60, 3, 0}, {MUS_REST, 0, 0}, {MUS_REST, 0, 0},
    {MUS_OFF, 0, 0}, {60, 5, 0}, {MUS_REST, 0, 0}, {MUS_REST, 0, 0},
};
static const MusPat HOLD_PAT = {2, 1, HOLD_EVENTS};
static const MusPat *const HOLD_PATS[] = {&HOLD_PAT};
static const MusSong HOLD_SONG = {5, 1, 1, 0xFF, INSTS, HOLD_PATS, ORDERS};

static void tick_n(int n) {
    for (int i = 0; i < n; i++)
        mus_tick();
}

int main(void) {
    CHECK("minor chord root", _mus_chord_note(129, 0) == 48);
    CHECK("major chord root", _mus_chord_note(136, 0) == 55);

    mus_load(&SONG);
    CHECK("load resets position", mus_pos() == 0);
    mus_play();
    g_ncalls = 0;

    tick_n(2);
    CHECK("row 0 triggers both notes", g_ncalls == 2);
    CHECK("MIDI note passes to host note mode",
          g_calls[0].freq == 69 && (g_calls[0].flags & VEX_TONE_NOTE_MODE));
    CHECK("default volume peaks at level",
          g_calls[0].vol == VEX_TONE_VOLUME(80, 80));
    CHECK("zero sustain defaults to speed*2",
          (g_calls[0].dur & 0xFF) == 2 * 2);
    CHECK("note lands on channel 0", (g_calls[0].flags & 3) == 0);
    CHECK("second note lands on channel 1", (g_calls[1].flags & 3) == 1);
    CHECK("position advances one row", mus_pos() == (1 << 8));

    tick_n(2);
    CHECK("OFF releases the current voice",
          g_ncalls == 3 &&
              g_calls[2].dur == VEX_TONE_DURATION(0, 0, 0, 4) &&
              (g_calls[2].flags & VEX_TONE_RELEASE));

    tick_n(2);
    CHECK("chord and noise trigger", g_ncalls == 5);
    CHECK("chord starts at its root",
          g_calls[3].freq == 48 && (g_calls[3].flags & VEX_TONE_NOTE_MODE));
    CHECK("override volume peaks at override",
          g_calls[3].vol == VEX_TONE_VOLUME(33, 33));
    CHECK("explicit sustain passes through", (g_calls[3].dur & 0xFF) == 7);
    CHECK("noise note uses noise mode", g_calls[4].flags & VEX_TONE_NOISE);

    tick_n(1);
    CHECK("bad instrument skipped and noise OFF hard-cuts",
          g_ncalls == 6 && g_calls[5].dur == 0 && g_calls[5].vol == 0 &&
              !(g_calls[5].flags & VEX_TONE_RELEASE));
    tick_n(1);
    CHECK("end of song stops with 4 silences", g_ncalls == 10);
    tick_n(4);
    CHECK("stopped song stays silent", g_ncalls == 10);

    mus_load(&SONG);
    mus_play();
    g_ncalls = 0;
    CHECK("reload resets position", mus_pos() == 0);
    tick_n(2);
    CHECK("reloaded song triggers again", g_ncalls == 2);

    mus_load(&ARP_SONG);
    mus_play();
    g_ncalls = 0;
    tick_n(14);
    CHECK("chord arpeggio triggers three notes then rests", g_ncalls == 7);
    CHECK("arpeggio starts at root", g_calls[0].freq == 48);
    CHECK("arpeggio reaches third", g_calls[1].freq == 51);
    CHECK("arpeggio reaches fifth", g_calls[2].freq == 55);
    CHECK("arpeggio preserves explicit sustain",
          (g_calls[0].dur & 0xFF) == 7 && (g_calls[1].dur & 0xFF) == 7 &&
              (g_calls[2].dur & 0xFF) == 7);

    mus_load(&HOLD_SONG);
    mus_play();
    g_ncalls = 0;
    tick_n(1);
    CHECK("hold sustain sets hold mode",
          g_ncalls == 2 && (g_calls[0].flags & VEX_TONE_HOLD) &&
              (g_calls[0].dur & 0xFF) == MUS_SUSTAIN_HOLD);
    tick_n(1);
    CHECK("OFF releases held voice", g_ncalls == 8 &&
          (g_calls[2].flags & VEX_TONE_RELEASE));
    CHECK("zero-volume instrument hard-cuts",
          g_calls[3].dur == 0 && g_calls[3].vol == 0 &&
              (g_calls[3].flags & 3) == 1);

    if (failures) {
        fprintf(stderr, "MUS: %d failure(s)\n", failures);
        return 1;
    }
    printf("MUS: all ok\n");
    return 0;
}
