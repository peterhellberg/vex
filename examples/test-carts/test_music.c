// test_music.c - music library test cart.
// Plays a short bassline with octave pulses, looping forever.
// Exercises mus.h so the music engine can be compared across hosts.
//
// Requires mus.h with:
//   - triggering gated on _mus_tick == 0 (no per-frame retrigger)
//   - notes played as Hz via _mus_note_hz() (no VEX_TONE_NOTE_MODE)
#include "mus.h"
#include "vex.h"

// clang-format off

// ---- instruments -------------------------------------------------------------
// wave            duty             atk dec sus rel vol pan
static const MusInst INSTS[] = {
    // 1: bass - triangle, deep
    {VEX_TONE_TRI,   VEX_TONE_MODE0, 0, 2, 85, 20, 95, 0},
    // 2: pulse - same line one/two octaves up, quiet bounce
    {VEX_TONE_TRI,   VEX_TONE_MODE0, 0, 2, 50, 15, 45, 0},
    // 3: hat - filtered LFSR tick, short and bright, panned left
    {VEX_TONE_NOISE, 0,              1, 3, 22, 10, 45, VEX_TONE_PAN_LEFT},
};

// ---- pattern -----------------------------------------------------------------
// A minor walk: A A E G / F F C D.  Speed 8 => ~7.5 rows/sec.
#define ROWS 8
#define SPD  8
#define _(n, i) {(n), (i), 0, 0}
#define O       {MUS_OFF, 0, 0, 0}
#define R       {MUS_REST, 0, 0, 0}

// Events: rows * MUS_CHANNELS;

static const MusNote EV0[ROWS * MUS_CHANNELS] = {
    //  ch0  ch1  bass          oct-pulse     hat
    R,   R,   _(45,1), R,   _(69,2), R, _(84,3), R,
    R,   R,   R,       R,   R,       R, R,       R,
    R,   R,   _(45,1), R,   R,       R, R,       R,
    R,   R,   R,       R,   _(57,2), R, R,       R,
    R,   R,   _(52,1), R,   _(76,2), R, _(84,3), R,
    R,   R,   R,       R,   R,       R, R,       R,
    R,   R,   _(43,1), R,   R,       R, R,       R,
    R,   R,   _(50,1), R,   _(62,2), R, R,       R,
};

#undef _
#undef O
#undef R

// ---- song --------------------------------------------------------------------
static const MusPat PAT0 = {ROWS, SPD, EV0};
static const MusPat *const PATS[] = {&PAT0};

static const unsigned char ORDERS[] = {0};

static const MusSong SONG = {
    3,      // num_insts
    1,      // num_pats
    1,      // num_orders
    0,      // loop to order 0
    INSTS, PATS, ORDERS,
};

// clang-format on

// -- entry points
// --------------------------------------------------------------

VEX_EXPORT("boot") void boot(void) {
  mus_load(&SONG);
  mus_play();
  title("vex - test_music");
}

VEX_EXPORT("update") void update(void) {
  mus_tick();

  int pos = mus_pos();
  int row = (pos >> 8) & 0xFF;

  cls(0);
  rectb(2, 2, VEX_WIDTH - 4, VEX_HEIGHT - 4, 2);

  text("vex - test_music", 4, 4, 12);

  // progress bar within the pattern
  rect(4, 30, (VEX_WIDTH - 24) * (row % ROWS) / ROWS, 4, 6);

  // channel activity dots (ch2 and ch3 should light up)
  for (int ch = 0; ch < MUS_CHANNELS; ch++) {
    const MusNote *ev = &EV0[(row % ROWS) * MUS_CHANNELS + ch];
    int color = ev->note != MUS_REST ? 11 : 5;
    rect(4 + ch * 12, 44, 8, 8, color);
  }

  text("mus", 4, VEX_HEIGHT - 10, 11);
}
