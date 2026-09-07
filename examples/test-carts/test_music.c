// test_music.c - cracktro-style chiptune for mus.h.
// Inspired by MakTone / class05 cracktros: arpeggiated pulse, fat TRI bass,
// ticking noise hats + snare. 4 channels, 125 BPM-ish.

#include "mus.h"
#include "vex.h"

// clang-format off

// ---- instruments -------------------------------------------------------------
// wave            duty             atk dec sus rel vol pan  ADSR — less mud
static const MusInst INSTS[] = {
    // 0: bass - TRI, now A3 not A2 (less sub mud), shorter tail
    {VEX_TONE_TRI,   VEX_TONE_MODE0, 0, 2, 0,  8, 72, 0},
    // 1: lead - pulse 12.5%, thin/cutting, a bit quieter
    {VEX_TONE_PULSE, VEX_TONE_MODE2, 0, 1, 0,  6, 58, VEX_TONE_PAN_LEFT},
    // 2: arp  - pulse 25%, very short staccato (sus 3 = 50ms)
    {VEX_TONE_PULSE, VEX_TONE_MODE1, 0, 0, 3,  3, 48, VEX_TONE_PAN_RIGHT},
    // 3: drums - noise, short tick, lower vol, centered now
    {VEX_TONE_NOISE, 0,              0, 0, 2,  3, 20, 0},
};

#define ROWS 16
#define SPD  8  // 7.5 rows/sec, sustain = 16 frames (0.27s) for held notes (hat uses sus 2)
#define _(n, i) {(n), (i), 0}
#define O       {MUS_OFF, 0, 0}
#define R       {MUS_REST, 0, 0}

// PAT0: A minor / F / C / G — bass now A3 etc (12 semitones up, less mud)
static const MusEvent EV0[ROWS * MUS_CHANNELS] = {
    // ch0(arp) ch1(lead) ch2(bass) ch3(drums) | row
    _(69,3), R,      _(57,1), _(110,4), // 0 A3 / A4 arps + hat
    _(72,3), R,      R,       R,        // 1 C
    _(76,3), R,      R,       _(108,4), // 2 E  hat
    R,       R,      R,       R,        // 3 rest — lets bass breathe
    _(72,3), R,      _(60,1), _(110,4), // 4 C4
    _(76,3), R,      R,       R,
    _(79,3), R,      R,       _(108,4),
    R,       R,      R,       R,
    _(76,3), R,      _(64,1), _(110,4), // 8 E4
    _(80,3), R,      R,       R,
    _(83,3), R,      R,       _(108,4),
    R,       R,      R,       R,
    _(79,3), R,      _(67,1), _(110,4), //12 G4
    _(83,3), R,      R,       R,
    _(86,3), R,      R,       _(112,4), //14 snare
    R,       R,      R,       R,
};

// PAT1: F / G / A / E — lead enters, sparser arp for clarity
static const MusEvent EV1[ROWS * MUS_CHANNELS] = {
    _(65,3), _(77,2), _(65,1), _(110,4), // 0 F4 / F5
    _(69,3), R,       R,       R,
    _(72,3), R,       R,       _(108,4),
    R,       R,       R,       R,
    _(67,3), _(79,2), _(67,1), _(110,4), // 4 G4
    _(71,3), R,       R,       R,
    _(74,3), R,       R,       _(108,4),
    R,       R,       R,       R,
    _(69,3), _(81,2), _(69,1), _(110,4), // 8 A4
    _(72,3), R,       R,       R,
    _(76,3), R,       R,       _(108,4),
    R,       R,       R,       R,
    _(64,3), _(76,2), _(64,1), _(112,4), //12 E4
    _(68,3), R,       R,       R,
    _(71,3), R,       R,       _(108,4),
    R,       R,       R,       R,
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

  const MusEvent *base = (order < 2 ? EV0 : EV1) + (row % ROWS) * MUS_CHANNELS;
  for (int ch = 0; ch < MUS_CHANNELS; ch++) {
    int c = base[ch].note != MUS_REST ? 11 : 5;
    if (base[ch].note == MUS_OFF)
      c = 8;
    rect(4 + ch * 12, 44, 8, 8, c);
  }

  text("mus ADSR cracktro", 4, VEX_HEIGHT - 10, 10);
}
