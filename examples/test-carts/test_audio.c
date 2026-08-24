// Audio test cart: exercises tone() so the sound implementation can be
// compared across hosts (C console, vex-run, vex-web). Each step triggers
// up to three voices at once and holds for a few frames, walking through
// melodies, polyphony, duty cycles, noise percussion, slides, note mode,
// panning, rapid retriggers, and extreme frequencies.
#include "vex.h"

#define TRACE_N 16 // steps kept in the recent-steps trace

typedef struct {
  int freq;   // Hz, or MIDI note with TONE_NOTE_MODE, or slide-packed
  int dur;    // packed via tone_duration
  int vol;    // packed via tone_volume
  int flags;  // built via tone_flags
} Voice;

typedef struct {
  const char* name;
  int frames;      // how long this step lasts, in frames
  Voice voice[3];  // simultaneous voices (freq 0 entries are skipped)
} Step;

#define V(ch, f, d, v, x) {f, d, v, TONE_FLAGS(ch, 0, x)}
#define NOTE_LEN(dur) (TONE_DURATION(dur, 6, 3, 1)) // musical default envelope

static const Step STEPS[] = {
  // -- arpeggio: rising and falling C major on a square lead ----------------
  {"arpeggio", 14, {V(0, 262, NOTE_LEN(12), 100, TONE_MODE0)}},
  {"arpeggio", 14, {V(0, 330, NOTE_LEN(12), 100, TONE_MODE0)}},
  {"arpeggio", 14, {V(0, 392, NOTE_LEN(12), 100, TONE_MODE0)}},
  {"arpeggio", 14, {V(0, 523, NOTE_LEN(12), 100, TONE_MODE0)}},
  {"arpeggio", 20, {V(0, 392, NOTE_LEN(18), 100, TONE_MODE0)}},

  // -- twinkle: classic melody ----------------------------------------------
  {"twinkle", 14, {V(0, 262, NOTE_LEN(12), 100, TONE_MODE0)}},
  {"twinkle", 14, {V(0, 262, NOTE_LEN(12), 100, TONE_MODE0)}},
  {"twinkle", 14, {V(0, 392, NOTE_LEN(12), 100, TONE_MODE0)}},
  {"twinkle", 14, {V(0, 440, NOTE_LEN(12), 90, TONE_MODE0)}},
  {"twinkle", 22, {V(0, 392, NOTE_LEN(20), 100, TONE_MODE0)}},

  // -- polyphony: triangle bass under a square lead --------------------------
  {"polyphony", 40,
    {V(0, 330, NOTE_LEN(38), 80, TONE_MODE0),
     V(1, 131, TONE_DURATION(36, 8, 6, 2), 90, TONE_TRI)}},
  {"polyphony", 40,
    {V(0, 392, NOTE_LEN(38), 80, TONE_MODE0),
     V(1, 165, TONE_DURATION(36, 8, 6, 2), 90, TONE_TRI)}},

  // -- duty: same pitch at each pulse width ----------------------------------
  {"duty", 16, {V(0, 440, NOTE_LEN(14), 100, TONE_MODE0)}},
  {"duty", 16, {V(0, 440, NOTE_LEN(14), 100, TONE_MODE1)}},
  {"duty", 16, {V(0, 440, NOTE_LEN(14), 100, TONE_MODE2)}},
  {"duty", 16, {V(0, 440, NOTE_LEN(14), 100, TONE_MODE3)}},

  // -- noise: kick and hats ---------------------------------------------------
  {"noise", 10, {V(2, 120, TONE_DURATION(2, 10, 0, 0), 100, TONE_NOISE)}},
  {"noise", 6,  {V(2, 6000, TONE_DURATION(1, 5, 0, 0), 60, TONE_NOISE)}},
  {"noise", 6,  {V(2, 6000, TONE_DURATION(1, 5, 0, 0), 60, TONE_NOISE)}},
  {"noise", 10, {V(2, 120, TONE_DURATION(2, 10, 0, 0), 100, TONE_NOISE)}},
  {"noise", 6,  {V(2, 6000, TONE_DURATION(1, 5, 0, 0), 60, TONE_NOISE)}},

  // -- slide: linear frequency glides -----------------------------------------
  {"slide", 30, {V(0, TONE_SLIDE(200, 800), TONE_DURATION(28, 8, 0, 0), 90, TONE_MODE0)}},
  {"slide", 30, {V(0, TONE_SLIDE(800, 200), TONE_DURATION(28, 8, 0, 0), 90, TONE_MODE0)}},

  // -- notes: MIDI note numbers (C4 E4 G4 C5) ----------------------------------
  {"notes", 14, {V(0, 60, NOTE_LEN(12), 100, TONE_NOTE_MODE)}},
  {"notes", 14, {V(0, 64, NOTE_LEN(12), 100, TONE_NOTE_MODE)}},
  {"notes", 14, {V(0, 67, NOTE_LEN(12), 100, TONE_NOTE_MODE)}},
  {"notes", 20, {V(0, 72, NOTE_LEN(18), 100, TONE_NOTE_MODE)}},

  // -- pan: left, center, right -------------------------------------------------
  {"pan", 14, {V(0, 440, NOTE_LEN(12), 100, TONE_PAN_LEFT)}},
  {"pan", 14, {V(0, 494, NOTE_LEN(12), 100, 0)}},
  {"pan", 14, {V(0, 554, NOTE_LEN(12), 100, TONE_PAN_RIGHT)}},

  // -- rapid: quick alternating notes (stresses retriggering) ------------------
  {"rapid", 6, {V(0, 440, NOTE_LEN(6), 100, TONE_MODE0)}},
  {"rapid", 6, {V(0, 659, NOTE_LEN(6), 100, TONE_MODE0)}},
  {"rapid", 6, {V(0, 440, NOTE_LEN(6), 100, TONE_MODE0)}},
  {"rapid", 6, {V(0, 659, NOTE_LEN(6), 100, TONE_MODE0)}},
  {"rapid", 6, {V(0, 440, NOTE_LEN(6), 100, TONE_MODE0)}},
  {"rapid", 6, {V(0, 659, NOTE_LEN(6), 100, TONE_MODE0)}},

  // -- range: extremes plus an explicit kill ------------------------------------
  {"range", 10, {V(0, 55, NOTE_LEN(9), 100, TONE_MODE0)}},
  {"range", 10, {V(0, 1975, NOTE_LEN(9), 100, TONE_MODE0)}},
  {"range", 14, {}},                 // rest: nothing triggered
  {"range", 10, {V(0, 262, 0, 0, 0), // kill idiom: zero duration
                 V(1, 131, TONE_DURATION(8, 4, 0, 0), 80, TONE_TRI)}},
};

#define NUM_STEPS (int)(sizeof(STEPS) / sizeof(STEPS[0]))
#define NUM_VOICES (int)(sizeof(((Step*)0)->voice) / sizeof(Voice))

static int idx;          // current step index
static int frames_left;  // frames remaining in the current step
static int trace[TRACE_N]; // recent step indices for the strip chart
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

// Map a frequency to a 0..100 pitch level on a log scale, so meters look
// evenly spaced instead of squashed at the low end.
static int freq_factor(int freq) {
  if ((unsigned)freq > 65535 || freq <= 0) return 50;
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

// ---- drawing helpers ---------------------------------------------------------

static void draw_trace(void) {
  int x0 = 8;
  int w = (VEX_WIDTH - 16 - 24) / TRACE_N;
  int base = VEX_HEIGHT - 16;
  for (int i = 0; i < TRACE_N; i++) {
    int s = trace[(trace_i + i) % TRACE_N];
    int h = s ? 24 : 0;
    rect(x0 + i * w, base - h, w - 1, h, s % 15 + 1);
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

// ---- cart entry points -------------------------------------------------------

VEX_EXPORT("boot") void boot(void) {
  idx = 0;
  frames_left = 0;
  trace_i = 0;
  for (int i = 0; i < TRACE_N; i++) trace[i] = 0;
  title("vex - test_audio");
}

VEX_EXPORT("update") void update(void) {
  // Advance to the next step when the current one has run its frames.
  if (frames_left <= 0) {
    idx = (idx + 1) % NUM_STEPS;
    frames_left = STEPS[idx].frames;
    trace[trace_i] = idx;
    trace_i = (trace_i + 1) % TRACE_N;

    // Trigger this step's voices. freq == 0 entries are rests.
    for (int v = 0; v < NUM_VOICES; v++) {
      const Voice* vc = &STEPS[idx].voice[v];
      if (vc->freq == 0 && vc->dur == 0 && vc->vol == 0 && vc->flags == 0)
        continue;
      tone(vc->freq, vc->dur, vc->vol, vc->flags);
    }
  }
  frames_left--;

  const Step* st = &STEPS[idx];

  // ---- draw ----
  cls(0);
  rectb(2, 2, VEX_WIDTH - 4, VEX_HEIGHT - 4, 2);

  text("vex - test_audio", 4, 4, 12);

  char s[48];
  s[0] = '\0';
  str_cat(s, st->name);
  str_cat(s, " ");
  char tmp[12];
  itoa(idx + 1, tmp);
  str_cat(s, tmp);
  str_cat(s, "/");
  itoa(NUM_STEPS, tmp);
  str_cat(s, tmp);
  text(s, 4, 14, 10);

  int elapsed = st->frames - frames_left;
  rect(4, 42, (VEX_WIDTH - 24) * elapsed / st->frames, 4, 6);

  int meter = 0;
  for (int v = 0; v < NUM_VOICES; v++)
    if (st->voice[v].freq != 0) meter = freq_factor(st->voice[v].freq & 0xFFFF);
  draw_meter(meter);
  draw_trace();

  text("tone()", 4, VEX_HEIGHT - 10, 11);
}
