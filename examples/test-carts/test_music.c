// test_music.c - cracktro-style chiptune for mus.h.
// Inspired by MakTone / class05 cracktros: arpeggiated pulse, fat TRI bass,
// ticking noise hats + snare. 4 channels, 125 BPM-ish.

#include "mus.h"
#include "vex.h"

// clang-format off

// ---- instruments -------------------------------------------------------------
// wave            duty             atk dec sus rel vol pan  ADSR
static const MusInst INSTS[] = {
    // 0: bass - TRI, subby, holds 2 rows (sus 0 = speed*2)
    {VEX_TONE_TRI,   VEX_TONE_MODE0, 0, 2, 0, 12, 85, 0},
    // 1: lead - pulse 12.5%, thin/cutting
    {VEX_TONE_PULSE, VEX_TONE_MODE2, 0, 1, 0,  8, 62, 0},
    // 2: arp  - pulse 25%, very short for chiptune arps
    {VEX_TONE_PULSE, VEX_TONE_MODE1, 0, 0, 3,  3, 60, 0},
    // 3: drums - noise, short hat / snare
    {VEX_TONE_NOISE, 0,              0, 0, 2,  4, 26, 0},
};

#define ROWS 16
#define SPD  4  // ~15 rows/sec, sustain = 8 frames (0.13s) for held notes
#define _(n, i) {(n), (i), 0}
#define O       {MUS_OFF, 0, 0}
#define R       {MUS_REST, 0, 0}

// PAT0: A minor / F / C / G — bass holds, arp cycles A-C-E, hats on beats
static const MusNote EV0[ROWS * MUS_CHANNELS] = {
    // ch0(arp) ch1(lead) ch2(bass) ch3(drums) | row chord
    _(69,3), R,      _(45,1), _(110,4), // 0 A / A2
    _(72,3), R,      R,       R,        // 1   C
    _(76,3), R,      R,       _(108,4), // 2   E  + hat
    _(69,3), R,      R,       R,        // 3
    _(72,3), R,      _(48,1), _(110,4), // 4 C / C3
    _(76,3), R,      R,       R,        // 5
    _(79,3), R,      R,       _(108,4), // 6
    _(72,3), R,      R,       R,        // 7
    _(76,3), R,      _(52,1), _(110,4), // 8 E / E3
    _(80,3), R,      R,       R,        // 9
    _(83,3), R,      R,       _(108,4), //10
    _(76,3), R,      R,       R,        //11
    _(79,3), R,      _(55,1), _(110,4), //12 G / G3
    _(83,3), R,      R,       R,        //13
    _(86,3), R,      R,       _(112,4), //14 snare-ish (higher pitch)
    _(79,3), R,      R,       R,        //15
};

// PAT1: variation — F / G / A / E with lead entering
static const MusNote EV1[ROWS * MUS_CHANNELS] = {
    _(65,3), _(77,2), _(53,1), _(110,4), // 0 F / F5 lead + arp + bass
    _(69,3), R,       R,       R,        // 1
    _(72,3), R,       R,       _(108,4), // 2
    _(65,3), R,       R,       R,        // 3
    _(67,3), _(79,2), _(55,1), _(110,4), // 4 G
    _(71,3), R,       R,       R,        // 5
    _(74,3), R,       R,       _(108,4), // 6
    _(67,3), R,       R,       R,        // 7
    _(69,3), _(81,2), _(57,1), _(110,4), // 8 A / A5
    _(72,3), R,       R,       R,        // 9
    _(76,3), R,       R,       _(108,4), //10
    _(69,3), R,       R,       R,        //11
    _(64,3), _(76,2), _(52,1), _(112,4), //12 E / E5 snare
    _(68,3), R,       R,       R,        //13
    _(71,3), R,       R,       _(108,4), //14
    _(64,3), R,       R,       R,        //15
};

#undef _
#undef O
#undef R

static const MusPat PAT0 = {ROWS, SPD, EV0};
static const MusPat PAT1 = {ROWS, SPD, EV1};
static const MusPat *const PATS[] = {&PAT0, &PAT1};
static const unsigned char ORDERS[] = {0, 0, 1, 1}; // A A B B loop
static const MusSong SONG = {4, 2, 4, 0, INSTS, PATS, ORDERS};

// clang-format on

VEX_EXPORT("boot") void boot(void) {
  mus_load(&SONG);
  mus_play();
  title("vex - test_music (cracktro)");
}

VEX_EXPORT("update") void update(void) {
  mus_tick();
  int pos = mus_pos();
  int order = pos & 0xFF;
  int row = (pos >> 8) & 0xFF;

  cls(0);
  rectb(2, 2, VEX_WIDTH - 4, VEX_HEIGHT - 4, 2);
  text("vex - test_music", 4, 4, 12);
  text(order < 2 ? "A" : "B", VEX_WIDTH - 14, 4, 11);

  rect(4, 30, (VEX_WIDTH - 24) * (row % ROWS) / ROWS, 4, 6);

  const MusNote *base = (order < 2 ? EV0 : EV1) + (row % ROWS) * MUS_CHANNELS;
  for (int ch = 0; ch < MUS_CHANNELS; ch++) {
    int c = base[ch].note != MUS_REST ? 11 : 5;
    if (base[ch].note == MUS_OFF) c = 8;
    rect(4 + ch * 12, 44, 8, 8, c);
  }

  text("mus ADSR cracktro", 4, VEX_HEIGHT - 10, 10);
}
