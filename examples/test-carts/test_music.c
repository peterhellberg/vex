// test_music.c - music library test cart.
// Plays a simple chiptune: melody, bass, and percussion, looping forever.
// Exercises mus.h so the music engine can be compared across hosts.
#include "vex.h"
#include "mus.h"

// -- instruments (1-indexed in note events) -----------------------------------

static const MusInst INSTS[] = {
    // 0: square lead (pulse, 50% duty, snappy envelope)
    {VEX_TONE_PULSE, VEX_TONE_MODE0, 1, 3, 80, 6, 90, 0},
    // 1: triangle bass
    {VEX_TONE_TRI, VEX_TONE_MODE0, 2, 4, 85, 8, 85, 0},
    // 2: kick (low noise)
    {VEX_TONE_NOISE, 0, 0, 3, 20, 8, 100, 0},
    // 3: hi-hat (high noise)
    {VEX_TONE_NOISE, 0, 0, 2, 10, 5, 60, 0},
};

// -- pattern 0: 16 rows, speed 6 (10 rows/sec) ------------------------------
//
// Melody: C4 E4 G4 C5 | G4 E4 C4 -- | repeated
// Bass:   C3 --- G2 --- | C3 --- G2 --- |
// Perc:   K - H - K - H - | K - H - K - H - |

#define ROWS 16
#define SPD  6
#define _(n, i) {n, i, 0, 0}
#define R       {0, 0, 0, 0}

static const MusNote EVENTS[ROWS * MUS_CHANNELS] = {
    //  row: melody       bass        kick         hat
    /*  0 */ _(60, 1), _(48, 1), _(36, 2), R,
    /*  1 */ _(64, 1), R,        R,         R,
    /*  2 */ _(67, 1), R,        R,         _(78, 3),
    /*  3 */ _(72, 1), R,        R,         R,
    /*  4 */ _(67, 1), _(43, 1), _(36, 2), R,
    /*  5 */ _(64, 1), R,        R,         R,
    /*  6 */ _(60, 1), R,        R,         _(78, 3),
    /*  7 */ R,        R,        R,         R,
    /*  8 */ _(60, 1), _(48, 1), _(36, 2), R,
    /*  9 */ _(64, 1), R,        R,         R,
    /* 10 */ _(67, 1), R,        R,         _(78, 3),
    /* 11 */ _(72, 1), R,        R,         R,
    /* 12 */ _(67, 1), _(43, 1), _(36, 2), R,
    /* 13 */ _(64, 1), R,        R,         R,
    /* 14 */ _(60, 1), R,        R,         _(78, 3),
    /* 15 */ R,        R,        R,         R,
};

#undef _
#undef R

static const MusPat PAT0 = {ROWS, SPD, EVENTS};
static const MusPat *const PATS[] = {&PAT0};
static const unsigned char ORDERS[] = {0};

static const MusSong SONG = {
    4,    // num_insts
    1,    // num_pats
    1,    // num_orders
    0,    // loop to order 0
    INSTS, PATS, ORDERS,
};

// -- tiny helpers (no libc) --------------------------------------------------

static void str_cat(char *d, const char *s) {
    while (*d) d++;
    while (*s) *d++ = *s++;
    *d = '\0';
}

static void itoa(int v, char *out) {
    char tmp[12];
    int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v > 0) { tmp[n++] = (char)('0' + v % 10); v /= 10; }
    for (int i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
    out[n] = '\0';
}

// -- entry points -------------------------------------------------------------

VEX_EXPORT("boot") void boot(void) {
    mus_load(&SONG);
    mus_play();
    title("vex - test_music");
}

VEX_EXPORT("update") void update(void) {
    mus_tick();

    int pos = mus_pos();
    int ord = pos & 0xFF;
    int row = (pos >> 8) & 0xFF;

    cls(0);
    rectb(2, 2, VEX_WIDTH - 4, VEX_HEIGHT - 4, 2);

    text("vex - test_music", 4, 4, 12);

    // position display: "0:05"
    char s[16];
    s[0] = '\0';
    char tmp[8];
    itoa(ord, tmp);
    str_cat(s, tmp);
    str_cat(s, ":");
    itoa(row, tmp);
    str_cat(s, tmp);
    text(s, 4, 14, 10);

    // progress bar
    rect(4, 30, (VEX_WIDTH - 24) * row / ROWS, 4, 6);

    // channel activity dots
    for (int ch = 0; ch < MUS_CHANNELS; ch++) {
        const MusNote *ev = &EVENTS[row * MUS_CHANNELS + ch];
        int color = ev->note != 0 ? 11 : 5;
        rect(4 + ch * 12, 44, 8, 8, color);
    }

    text("mus", 4, VEX_HEIGHT - 10, 11);
}
