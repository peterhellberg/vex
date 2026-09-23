// test_music.c - cracktro chiptune for mus.h.
// Fast pulse lead, arpeggiated chords, hollow pulse bass, and noise drums. 150 BPM.

#include "mus.h"
#include "vex.h"

// clang-format off

// ---- instruments -------------------------------------------------------------
// wave            duty             atk dec sus rel vol pan pwm start end
static const MusInst INSTS[] = {
    {VEX_TONE_PULSE, VEX_TONE_MODE3, 0, 1, MUS_SUSTAIN_HOLD, 9, 88, 0, 1, 192, 192},
    {VEX_TONE_PULSE, VEX_TONE_MODE2, 0, 2, 0, 8, 62, VEX_TONE_PAN_LEFT, 1, 32, 144},
    {VEX_TONE_PULSE, VEX_TONE_MODE1, 0, 0, 4, 2, 30, VEX_TONE_PAN_RIGHT, 1, 64, 64},
    {VEX_TONE_NOISE, 0,              0, 0, 1, 4, 18, 0},
};

#define ROWS 16
#define E(n, i) {(n), (i), 0}
#define V(n, i, v) {(n), (i), (v)}
#define R {MUS_REST, 0, 0}
#define O {MUS_OFF, 0, 0}

// ch0(arp) ch1(lead) ch2(bass) ch3(drums) | row
static const MusEvent EV0[ROWS * MUS_CHANNELS] = {
    V(134,3,40), V(69,2,68),  V(45,1,80), V(45,4,32), // A minor
    R,        R,        R,           E(110,4),
    V(134,3,40), E(72,2),  V(52,1,62), E(108,4),
    R,        R,        R,           V(45,4,26),
    V(132,3,40), V(76,2,68),  V(48,1,78), V(112,4,26), // F minor
    R,        R,        R,           E(110,4),
    V(132,3,40), E(79,2),  R,           E(108,4),
    R,        O,        R,           V(45,4,26),
    V(139,3,40), V(81,2,68),  V(52,1,76), V(45,4,32), // C major
    R,        R,        R,           E(110,4),
    V(139,3,40), E(84,2),  R,           E(108,4),
    R,        R,        R,           V(45,4,26),
    V(136,3,40), V(83,2,68),  V(55,1,78), V(112,4,26), // G major
    R,        R,        R,           E(110,4),
    V(136,3,40), V(81,2,68),  R,           E(108,4),
    V(134,3,40), O,        O,           V(45,4,26),
};

static const MusEvent EV1[ROWS * MUS_CHANNELS] = {
    V(132,3,40), V(77,2,68),  V(41,1,80), V(45,4,32), // F minor
    R,        R,        R,           E(110,4),
    V(132,3,40), E(72,2),  V(48,1,62), E(108,4),
    R,        R,        R,           V(45,4,26),
    V(134,3,40), V(76,2,68),  V(45,1,78), V(112,4,26), // A minor
    R,        R,        R,           E(110,4),
    V(134,3,40), V(74,2,68),  R,           E(108,4),
    R,        O,        R,           V(45,4,26),
    V(133,3,40), V(69,2,68),  V(43,1,76), V(45,4,32), // G minor
    R,        R,        R,           E(110,4),
    V(133,3,40), E(72,2),  R,           E(108,4),
    R,        R,        R,           V(45,4,26),
    V(131,3,40), V(74,2,68),  V(40,1,78), V(112,4,26), // E minor
    R,        R,        R,           E(110,4),
    V(131,3,40), E(72,2),  R,           E(108,4),
    V(132,3,40), O,        O,           V(45,4,26),
};

static const MusEvent EV2[ROWS * MUS_CHANNELS] = {
    V(131,3,40), V(76,2,68),  V(40,1,80), V(45,4,32), // E minor
    R,        R,        R,           E(110,4),
    V(131,3,40), E(79,2),  V(47,1,62), E(108,4),
    R,        R,        R,           V(45,4,26),
    V(136,3,40), V(83,2,68),  V(45,1,78), V(112,4,26), // G major
    R,        R,        R,           E(110,4),
    V(136,3,40), V(81,2,68),  R,           E(108,4),
    R,        O,        R,           V(45,4,26),
    V(133,3,40), E(79,2),  V(43,1,76), V(45,4,32), // G minor
    R,        R,        R,           E(110,4),
    V(133,3,40), V(76,2,68),  R,           E(108,4),
    R,        R,        R,           V(45,4,26),
    V(131,3,40), V(74,2,68),  V(40,1,78), V(112,4,26), // E minor fill
    R,        R,        R,           E(110,4),
    V(131,3,40), V(76,2,68),  R,           E(108,4),
    V(131,3,40), O,        O,           V(45,4,32),
};

static const MusEvent EV3[ROWS * MUS_CHANNELS] = {
    V(134,3,40), V(81,2,68),  V(45,1,80), R,
    R,        R,        R,           R,
    R,        E(84,2),  V(52,1,62), R,
    R,        R,        R,           R,
    V(132,3,40), E(79,2),  V(48,1,78), R,
    R,        R,        R,           R,
    R,        V(76,2,68),  R,           R,
    R,        R,        R,           R,
    V(136,3,40), V(81,2,68),  V(45,1,80), V(45,4,32),
    R,        R,        R,           E(110,4),
    R,        E(84,2),  R,           E(112,4),
    R,        R,        R,           V(45,4,26),
    V(131,3,40), V(83,2,68),  V(43,1,78), V(112,4,34),
    R,        R,        R,           V(110,4,20),
    R,        V(81,2,68),  R,           V(112,4,38),
    V(136,3,40), O,        O,           V(45,4,44),
};

#undef E
#undef V
#undef R
#undef O

static const MusPat PAT0 = {ROWS, 6, EV0};
static const MusPat PAT1 = {ROWS, 6, EV1};
static const MusPat PAT2 = {ROWS, 6, EV2};
static const MusPat PAT3 = {ROWS, 6, EV3};
static const MusPat *const PATS[] = {&PAT0, &PAT1, &PAT2, &PAT3};
static const unsigned char ORDERS[] = {0, 1, 2, 1, 3, 0};
static const MusSong SONG = {4, 4, 6, 0, INSTS, PATS, ORDERS};
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
  text(pattern == 0 ? "A" : pattern == 1 ? "B" : pattern == 2 ? "C" : "D", VEX_WIDTH - 17, 6, 15);

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
  text(pattern == 0 ? "A / MAIN" : pattern == 1 ? "B / LEAD" : pattern == 2 ? "C / BRIDGE" : "D / OUTRO", 8, 110, 15);
  text("150 BPM / 4CH / PWM", 4, 126, 10);
}
