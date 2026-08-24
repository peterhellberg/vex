// Audio test cart: plays a looping chiptune melody through tone(freq) so the
// sound implementation can be compared across hosts (C console, vex-run,
// vex-web). The melody spans a wide frequency range and note lengths, plus a
// rapid-fire section that stresses the hosts' voice mixing.
//
// The cart still sustains held notes the old way, re-triggering a flat
// legacy blip (ms < 0) every RETRIGGER frames on channel 0 -- a deliberate
// stress test of rapid retriggers. New carts would just use ms == 0 and let
// the voice sustain until the next note. A note with freq 0 is a rest (no
// tone). Frequencies are drawn on a log scale so octaves are evenly spaced.
#include "vex.h"

#define NORMAL 12  // frames per regular note (0.2s at 60fps)
#define LONG   24  // frames for a held note (0.4s)
#define SHORT   6  // frames for a quick note (0.1s = one blip)
#define RETRIGGER 6 // re-tone every 0.1s while a note is held (sustain)
#define TRACE_N 16 // notes kept in the recent-notes trace

typedef struct {
  int freq;          // Hz; 0 means rest (silent)
  int frames;        // how long the note lasts, in frames
  const char* name;  // label, e.g. "C4"; "" shows frequency only
} Note;

static const Note MELODY[] = {
  // -- arpeggio: rising and falling C major --------------------------------
  {262, NORMAL, "C4"}, {330, NORMAL, "E4"}, {392, NORMAL, "G4"},
  {523, NORMAL, "C5"}, {392, NORMAL, "G4"}, {330, NORMAL, "E4"},
  {262, LONG,   "C4"},

  // -- twinkle twinkle ------------------------------------------------------
  {262, NORMAL, "C4"}, {262, NORMAL, "C4"}, {392, NORMAL, "G4"},
  {392, NORMAL, "G4"}, {440, NORMAL, "A4"}, {440, NORMAL, "A4"},
  {392, LONG,   "G4"}, {349, NORMAL, "F4"}, {349, NORMAL, "F4"},
  {330, NORMAL, "E4"}, {330, NORMAL, "E4"}, {294, NORMAL, "D4"},
  {294, NORMAL, "D4"}, {262, LONG,   "C4"},

  // -- octaves: same pitch class across registers ---------------------------
  {131, NORMAL, "C3"}, {262, NORMAL, "C4"}, {196, NORMAL, "G3"},
  {392, NORMAL, "G4"}, {165, NORMAL, "E3"}, {330, NORMAL, "E4"},
  {131, NORMAL, "C3"}, {262, LONG,   "C4"},

  // -- sweep: log ramp up and down, low to high ------------------------------
  {130, NORMAL, ""},  {146, NORMAL, ""},  {163, NORMAL, ""},
  {183, NORMAL, ""},  {205, NORMAL, ""},  {230, NORMAL, ""},
  {257, NORMAL, ""},  {288, NORMAL, ""},  {323, NORMAL, ""},
  {361, NORMAL, ""},  {405, NORMAL, ""},  {453, NORMAL, ""},
  {508, NORMAL, ""},  {569, NORMAL, ""},  {637, NORMAL, ""},
  {714, NORMAL, ""},  {800, NORMAL, ""},  {896, NORMAL, ""},
  {1003, NORMAL, ""}, {1124, NORMAL, ""}, {1259, NORMAL, ""},
  {1410, NORMAL, ""}, {1579, NORMAL, ""}, {1769, NORMAL, ""},
  {1981, NORMAL, ""},
  {1769, NORMAL, ""}, {1579, NORMAL, ""}, {1410, NORMAL, ""},
  {1259, NORMAL, ""}, {1124, NORMAL, ""}, {1003, NORMAL, ""},
  {896, NORMAL, ""},  {800, NORMAL, ""},  {714, NORMAL, ""},
  {637, NORMAL, ""},  {569, NORMAL, ""},  {508, NORMAL, ""},
  {453, NORMAL, ""},  {405, NORMAL, ""},  {361, NORMAL, ""},
  {323, NORMAL, ""},  {288, NORMAL, ""},  {257, NORMAL, ""},
  {230, NORMAL, ""},  {205, NORMAL, ""},  {183, NORMAL, ""},
  {163, NORMAL, ""},  {146, NORMAL, ""},

  // -- rapid: quick alternating notes (stresses sound pooling) --------------
  {440, SHORT, "A4"}, {659, SHORT, "E5"}, {440, SHORT, "A4"},
  {659, SHORT, "E5"}, {440, SHORT, "A4"}, {659, SHORT, "E5"},
  {440, SHORT, "A4"}, {659, SHORT, "E5"}, {440, SHORT, "A4"},
  {659, SHORT, "E5"}, {440, SHORT, "A4"}, {659, SHORT, "E5"},
  {440, SHORT, "A4"}, {659, SHORT, "E5"}, {440, SHORT, "A4"},
  {659, SHORT, "E5"}, {440, SHORT, "A4"}, {659, SHORT, "E5"},
  {440, SHORT, "A4"}, {659, SHORT, "E5"}, {440, SHORT, "A4"},
  {659, SHORT, "E5"}, {440, SHORT, "A4"}, {659, SHORT, "E5"},

  // -- range: extreme frequencies plus a rest --------------------------------
  {55,   SHORT, "A1"},
  {1975, SHORT, "B6"},
  {0,    LONG,  "REST"},
};

typedef struct {
  int start;
  const char* name;
} Section;

static const Section SECTIONS[] = {
  {0,   "arpeggio"},
  {7,   "twinkle"},
  {21,  "octaves"},
  {29,  "sweep"},
  {77,  "rapid"},
  {101, "range"},
};

#define NUM_SECTIONS (int)(sizeof(SECTIONS) / sizeof(SECTIONS[0]))
#define NUM_NOTES    (int)(sizeof(MELODY) / sizeof(MELODY[0]))

static int idx;          // current note index
static int frames_left;  // frames remaining in the current note
static int retrigger;    // frames until the next sustain tone
static int flash;        // frames left showing the note flash
static long tones;       // total tone() calls
static int trace[TRACE_N]; // frequency factors of recent notes
static int trace_i;

// ---- fixed-point log2 (8 fractional bits), no libm -------------------------

static int flog2(int x) {
  if (x < 1) x = 1;
  int r = 0;
  while (x >= 2) {
    x >>= 1;
    r += 256;
  }
  int f = 0;
  for (int i = 0; i < 8; i++) {
    x *= x;
    f <<= 1;
    if (x >= 2) {
      f |= 1;
      x >>= 1;
    }
  }
  return r + f;
}

// Map a frequency to a 0..100 pitch level on a log scale, so the sweep and
// melody look evenly spaced instead of squashed at the low end.
static int freq_factor(int freq) {
  int lo = flog2(64);
  int hi = flog2(2048);
  int v = flog2(freq);
  if (v <= lo) return 0;
  if (v >= hi) return 100;
  return (v - lo) * 100 / (hi - lo);
}

// ---- tiny string helpers (no libc) -----------------------------------------

static void str_cat(char* dst, const char* src) {
  while (*dst) dst++;
  while (*src) *dst++ = *src++;
  *dst = '\0';
}

static void itoa(int v, char* out) {
  char tmp[12];
  int n = 0;
  if (v == 0) tmp[n++] = '0';
  while (v > 0) {
    tmp[n++] = (char)('0' + v % 10);
    v /= 10;
  }
  for (int i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
  out[n] = '\0';
}

static const char* section_name(int i) {
  const char* name = "";
  for (int s = 0; s < NUM_SECTIONS; s++)
    if (i >= SECTIONS[s].start) name = SECTIONS[s].name;
  return name;
}

static void build_section_label(char* out, int i) {
  char tmp[12];
  out[0] = '\0';
  str_cat(out, section_name(i));
  str_cat(out, " ");
  itoa(i + 1, tmp);
  str_cat(out, tmp);
  str_cat(out, "/");
  itoa(NUM_NOTES, tmp);
  str_cat(out, tmp);
}

static void build_note_label(char* out, const Note* n) {
  char tmp[12];
  out[0] = '\0';
  if (n->name[0]) {
    str_cat(out, n->name);
    str_cat(out, " ");
  }
  itoa(n->freq, tmp);
  str_cat(out, tmp);
  str_cat(out, "Hz");
}

static void build_tone_label(char* out) {
  char tmp[12];
  out[0] = '\0';
  str_cat(out, "tones ");
  itoa((int)tones, tmp);
  str_cat(out, tmp);
}

// ---- drawing helpers --------------------------------------------------------

static void trace_push(int factor) {
  trace[trace_i] = factor;
  trace_i = (trace_i + 1) % TRACE_N;
}

static void draw_trace(void) {
  int x0 = 8;
  int w = (VEX_WIDTH - 16 - 24) / TRACE_N;
  int base = VEX_HEIGHT - 16;
  for (int i = 0; i < TRACE_N; i++) {
    int f = trace[(trace_i + i) % TRACE_N];
    int h = f * 24 / 100;
    rect(x0 + i * w, base - h, w - 1, h, 8);
  }
}

static void draw_meter(int factor) {
  int x = VEX_WIDTH - 24;
  int top = 52;
  int base = VEX_HEIGHT - 40;
  rectb(x, top, 20, base - top, 8);
  int h = 4 + factor * (base - top - 8) / 100;
  rect(x + 1, base - h, 18, h, factor < 30 ? 10 : factor < 60 ? 9 : 3);
}

// ---- cart entry points --------------------------------------------------------

VEX_EXPORT("boot") void boot(void) {
  idx = 0;
  frames_left = 0;
  retrigger = 0;
  flash = 0;
  tones = 0;
  trace_i = 0;
  for (int i = 0; i < TRACE_N; i++) trace[i] = 0;
  title("vex - test_audio");
}

VEX_EXPORT("update") void update(void) {
  // Advance to the next note when the current one has run its frames.
  if (frames_left <= 0) {
    frames_left = MELODY[idx].frames;
    retrigger = 0;
    trace_push(freq_factor(MELODY[idx].freq));
    idx++;
    if (idx >= NUM_NOTES) idx = 0;
  }

  const Note* n = &MELODY[idx];

  // Sustain: re-tone while the note is held; rests stay silent.
  if (retrigger <= 0) {
    retrigger = RETRIGGER;
    if (n->freq > 0) {
      tone(0, n->freq, -1);
      tones++;
      flash = RETRIGGER;
    }
  }
  retrigger--;
  frames_left--;
  if (flash > 0) flash--;

  // ---- draw ----
  cls(0);
  rectb(2, 2, VEX_WIDTH - 4, VEX_HEIGHT - 4, 2);

  text("vex - test_audio", 4, 4, 12);

  char s[40];
  build_section_label(s, idx);
  text(s, 4, 14, 10);

  if (flash > 0) rect(4, 24, 200, 14, 1);
  build_note_label(s, n);
  text(s, 6, 26, 7);

  int elapsed = n->frames - frames_left;
  rect(4, 42, (VEX_WIDTH - 24) * elapsed / n->frames, 4, 6);

  draw_meter(freq_factor(n->freq));
  draw_trace();

  build_tone_label(s);
  text(s, 4, VEX_HEIGHT - 10, 11);
}
