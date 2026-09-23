// test_music.c - cracktro chiptune for mus.h.
// Fast pulse lead, arpeggiated chords, hollow pulse bass, and noise drums. 150 BPM.

#include "mus.h"
#include "vex.h"

// clang-format off

// ---- instruments -------------------------------------------------------------
// wave            duty             atk dec sus rel vol pan pwm start end
static const MusInst INSTS[] = {
    {VEX_TONE_PULSE, VEX_TONE_MODE1, 0, 1, MUS_SUSTAIN_HOLD, 6, 84, 0, 1, 64, 64},
    {VEX_TONE_PULSE, VEX_TONE_MODE2, 0, 0, 0, 4, 52, VEX_TONE_PAN_LEFT, 1, 16, 192},
    {VEX_TONE_PULSE, VEX_TONE_MODE2, 0, 1, 3, 3, 34, VEX_TONE_PAN_RIGHT, 1, 32, 160},
    {VEX_TONE_NOISE, 0,              0, 0, 1, 4, 18, 0},
    {VEX_TONE_PULSE, VEX_TONE_MODE1, 0, 0, 0, 3, 48, VEX_TONE_PAN_LEFT, 1, 192, 32},
    {VEX_TONE_PULSE, VEX_TONE_MODE1, 0, 0, 2, 2, 28, VEX_TONE_PAN_RIGHT, 1, 160, 48},
    {VEX_TONE_PULSE, VEX_TONE_MODE2, 0, 1, 0, 4, 72, 0, 1, 32, 32},
};

#define ROWS 16
#define E(n, i) {(n), (i), 0}
#define V(n, i, v) {(n), (i), (v)}
#define R {MUS_REST, 0, 0}
#define O {MUS_OFF, 0, 0}

// ch0(arp) ch1(lead) ch2(bass) ch3(drums) | row
static const MusEvent EV0[ROWS * MUS_CHANNELS] = {
    V(134,3,44), V(69,2,56),  V(45,1,80), V(45,4,32), // A minor
    R,        R,        R,           E(110,4),
    V(134,3,44), E(72,2),  V(52,1,62), V(110,4,14),
    R,        R,        R,           V(45,4,26),
    V(132,3,44), V(76,2,56),  V(48,1,78), V(112,4,26), // F minor
    R,        R,        R,           E(110,4),
    V(132,3,44), E(79,2),  R,           V(110,4,20),
    R,        O,        R,           V(45,4,26),
    V(139,3,44), V(81,2,56),  V(52,1,76), V(45,4,32), // C major
    R,        R,        R,           E(110,4),
    V(139,3,44), E(84,2),  R,           V(110,4,18),
    R,        R,        R,           V(45,4,26),
    V(136,3,44), V(83,2,56),  V(55,1,78), V(112,4,26), // G major
    R,        R,        R,           E(110,4),
    V(136,3,44), V(81,2,56),  R,           V(112,4,24),
    V(134,3,44), O,        O,           V(45,4,26),
};

static const MusEvent EV1[ROWS * MUS_CHANNELS] = {
    V(132,3,44), V(77,2,56),  V(41,1,80), V(45,4,32), // F minor
    R,        R,        R,           E(110,4),
    V(132,3,44), E(72,2),  V(48,1,62), V(110,4,14),
    R,        R,        R,           V(45,4,26),
    V(134,3,44), V(76,2,56),  V(45,1,78), V(112,4,26), // A minor
    R,        R,        R,           E(110,4),
    V(134,3,44), V(74,2,56),  R,           V(110,4,20),
    R,        O,        R,           V(45,4,26),
    V(133,3,44), V(69,2,56),  V(43,1,76), V(45,4,32), // G minor
    R,        R,        R,           E(110,4),
    V(133,3,44), E(72,2),  R,           V(110,4,16),
    R,        R,        R,           V(45,4,26),
    V(131,3,44), V(74,2,56),  V(40,1,78), V(112,4,26), // E minor
    R,        R,        R,           E(110,4),
    V(131,3,44), E(72,2),  R,           V(112,4,24),
    V(132,3,44), O,        O,           V(45,4,26),
};

static const MusEvent EV2[ROWS * MUS_CHANNELS] = {
    V(131,6,42), V(76,5,52),  V(40,7,80), V(45,4,32), // E minor
    R,        R,        R,           E(110,4),
    V(131,6,42), E(79,5),  V(47,7,62), V(110,4,20),
    R,        R,        R,           V(45,4,26),
    V(136,6,42), V(83,5,52),  V(45,7,78), V(112,4,26), // G major
    R,        R,        R,           E(110,4),
    V(136,6,42), V(81,5,52),  R,           V(110,4,18),
    R,        O,        R,           V(45,4,26),
    V(133,6,42), E(79,5),  V(43,7,76), V(45,4,32), // G minor
    R,        R,        R,           E(110,4),
    V(133,6,42), V(76,5,52),  R,           V(110,4,20),
    R,        R,        R,           V(45,4,26),
    V(131,6,42), V(74,5,52),  V(40,7,78), V(112,4,26), // E minor fill
    R,        R,        R,           E(110,4),
    V(131,6,42), V(76,5,52),  R,           V(112,4,24),
    V(131,6,42), O,        O,           V(45,4,32),
};

static const MusEvent EV3[ROWS * MUS_CHANNELS] = {
    V(134,6,42), V(81,5,52),  V(45,7,80), R,
    R,        R,        R,           R,
    R,        E(84,5),  V(52,7,62), R,
    R,        R,        R,           R,
    V(132,6,42), E(79,5),  V(48,7,78), R,
    R,        R,        R,           R,
    R,        V(76,5,52),  R,           R,
    R,        R,        R,           R,
    V(136,6,42), V(81,5,52),  V(45,7,80), V(45,4,32),
    R,        R,        R,           E(110,4),
    R,        E(84,5),  R,           E(112,4),
    R,        R,        R,           V(45,4,26),
    V(131,6,42), V(83,5,52),  V(43,7,78), V(112,4,34),
    R,        R,        R,           V(110,4,20),
    R,        V(81,5,52),  R,           V(112,4,38),
    V(136,6,42), O,        O,           V(45,4,44),
};

static const MusEvent EV4[ROWS * MUS_CHANNELS] = {
    V(134,3,40), V(69,2,56),  V(45,1,80), R,
    R,        R,        R,           R,
    V(134,3,40), E(72,2),  V(52,1,62), R,
    R,        R,        R,           R,
    V(132,3,40), V(76,2,56),  V(41,1,78), R,
    R,        R,        R,           R,
    V(132,3,40), E(79,2),  R,           R,
    R,        O,        R,           R,
    V(139,3,40), V(81,2,56),  V(52,1,76), V(45,4,32),
    R,        R,        R,           E(110,4),
    V(139,3,40), E(84,2),  R,           E(108,4),
    R,        R,        R,           V(45,4,26),
    V(136,3,40), V(83,2,56),  V(55,1,78), V(112,4,26),
    R,        R,        R,           E(110,4),
    V(136,3,40), V(81,2,56),  R,           E(108,4),
    V(134,3,40), O,        O,           V(45,4,32),
};

#undef E
#undef V
#undef R
#undef O

static const MusPat PAT0 = {ROWS, 6, EV0};
static const MusPat PAT1 = {ROWS, 6, EV1};
static const MusPat PAT2 = {ROWS, 6, EV2};
static const MusPat PAT3 = {ROWS, 6, EV3};
static const MusPat PAT4 = {ROWS, 6, EV4};
static const MusPat *const PATS[] = {&PAT0, &PAT1, &PAT2, &PAT3, &PAT4};
static const unsigned char ORDERS[] = {4, 0, 1, 2, 1, 0, 3, 0, 1};
static const MusSong SONG = {7, 5, 9, 0, INSTS, PATS, ORDERS};
static const char *PATTERN_NAMES[] = {"A", "B", "C", "D", "E"};
static const char *PATTERN_LABELS[] = {
    "A / MAIN", "B / LEAD", "C / BRIDGE", "D / OUTRO", "E / INTRO",
};
static unsigned char CHANNEL_MUTED[MUS_CHANNELS];
static int MOUSE_DOWN;

// clang-format on

VEX_EXPORT("boot") void boot(void) {
  pal(0, 0x140C00);
  pal(1, 0x690804);
  pal(2, 0xDE2C2C);
  pal(3, 0xFA5555);
  pal(4, 0x382400);
  pal(5, 0xA1858D);
  pal(6, 0xD0B2BA);
  pal(7, 0xFACACA);
  pal(8, 0x002000);
  pal(9, 0x405544);
  pal(10, 0x617561);
  pal(11, 0x99B295);
  pal(12, 0x0C3044);
  pal(13, 0x556D89);
  pal(14, 0x7595B6);
  pal(15, 0xDEEEFF);
  mus_load(&SONG);
  mus_play();
  title("vex - test_music (cracktro)");
}

VEX_EXPORT("update") void update(void) {
  int mouse_down_now = mbtn(0);
  if (mouse_down_now && !MOUSE_DOWN) {
    int mouse_x = mx();
    int mouse_y = my();
    for (int ch = 0; ch < MUS_CHANNELS; ch++) {
      int x = 4 + ch * 38;
      if (mouse_x >= x && mouse_x < x + 34 && mouse_y >= 60 && mouse_y < 86) {
        CHANNEL_MUTED[ch] = !CHANNEL_MUTED[ch];
        mus_mute(ch, CHANNEL_MUTED[ch]);
      }
    }
  }
  MOUSE_DOWN = mouse_down_now;

  mus_tick();
  int pos = mus_pos();
  int order = pos & 0xFF;
  int pattern = ORDERS[order];
  int row = (pos >> 8) & 0xFF;

  cls(8);
  rectb(2, 2, VEX_WIDTH - 4, VEX_HEIGHT - 4, 10);
  text("VEX // CRACKTRO", 6, 6, 15);
  rect(VEX_WIDTH - 26, 4, 22, 11, 9);
  rectb(VEX_WIDTH - 26, 4, 22, 11, 11);
  text(PATTERN_NAMES[pattern], VEX_WIDTH - 17, 6, 15);

  rect(4, 22, VEX_WIDTH - 8, 1, 10);
  text("SEQUENCE", 4, 27, 10);
  rect(4, 36, VEX_WIDTH - 8, 6, 9);
  rect(4, 36, (VEX_WIDTH - 8) * (row % ROWS) / ROWS, 6, 11);

  text("CHANNELS", 4, 50, 10);
  const char *labels[] = {"ARP", "LEAD", "BASS", "DRUM"};
  const MusEvent *base = PATS[pattern]->events + (row % ROWS) * MUS_CHANNELS;
  for (int ch = 0; ch < MUS_CHANNELS; ch++) {
    int x = 4 + ch * 38;
    int c = CHANNEL_MUTED[ch] ? 9 : base[ch].note != MUS_REST ? 11 : 9;
    if (!CHANNEL_MUTED[ch] && base[ch].note == MUS_OFF)
      c = 13;
    rect(x, 60, 34, 26, 9);
    rectb(x, 60, 34, 26, CHANNEL_MUTED[ch] ? 9 : 10);
    int label_x = x + (ch == 0 ? 5 : 1);
    text(labels[ch], label_x, 64, c);
    rect(x + 4, 78, 26, 4, c);
  }

  rect(4, 96, VEX_WIDTH - 8, 24, 9);
  rectb(4, 96, VEX_WIDTH - 8, 24, 10);
  text("SYNC LOCK", 8, 101, 11);
  text(PATTERN_LABELS[pattern], 8, 110, 15);
  text("150 BPM / 4CH / PWM", 4, 126, 10);
}
