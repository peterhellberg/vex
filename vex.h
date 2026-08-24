// vex.h - cart SDK. Include this in a cart and compile to wasm32:
//
//   zig cc --target=wasm32-freestanding -nostdlib -Os \
//          -Wl,--no-entry -I. -o cart.wasm main.c
//
// A cart must export update() (called every frame at 60fps) and may export
// boot() (called once at start). It draws by calling the functions below.
#ifndef VEX_H
#define VEX_H

#define VEX_IMPORT(name) __attribute__((import_module("env"), import_name(name)))
#define VEX_EXPORT(name) __attribute__((export_name(name)))

// Screen is 320x180. Colors are SWEETIE-16 palette indices 0..15.
#define VEX_WIDTH  320
#define VEX_HEIGHT 180

// Buttons, as passed to btn().
#define VEX_LEFT  0
#define VEX_RIGHT 1
#define VEX_UP    2
#define VEX_DOWN  3
#define VEX_Z     4
#define VEX_X     5

// Mouse buttons, as passed to mbtn().
#define VEX_MOUSE_LEFT   0
#define VEX_MOUSE_RIGHT  1
#define VEX_MOUSE_MIDDLE 2

VEX_IMPORT("cls")   void cls(int color);                              // clear screen
VEX_IMPORT("pset")  void pset(int x, int y, int color);              // set one pixel
VEX_IMPORT("rect")  void rect(int x, int y, int w, int h, int color);  // filled rect
VEX_IMPORT("rectb") void rectb(int x, int y, int w, int h, int color); // rect outline
VEX_IMPORT("circ")  void circ(int x, int y, int r, int color);       // filled circle
VEX_IMPORT("circb") void circb(int x, int y, int r, int color);      // circle outline
VEX_IMPORT("line")  void line(int x0, int y0, int x1, int y1, int color);
VEX_IMPORT("tri")   void tri(int x1, int y1, int x2, int y2, int x3, int y3, int color);  // filled triangle
VEX_IMPORT("trib")  void trib(int x1, int y1, int x2, int y2, int x3, int y3, int color); // triangle outline
VEX_IMPORT("blit")  void blit(const void* data, int x, int y, int w, int h, int key); // w*h palette-index bitmap; pixels == key are skipped
VEX_IMPORT("text")  void text(const char* s, int x, int y, int color);
VEX_IMPORT("title") void title(const char* s);                       // set window title
VEX_IMPORT("btn")   int  btn(int button);                            // 1 if held, else 0
VEX_IMPORT("btnp")  int  btnp(int button);                           // 1 if just pressed this frame

VEX_IMPORT("mx")   int mx(void);             // mouse x in screen pixels
VEX_IMPORT("my")   int my(void);             // mouse y in screen pixels
VEX_IMPORT("mbtn") int mbtn(int button);     // 1 if mouse button held

VEX_IMPORT("pal")      void pal(int index, int rgb); // override palette entry (0xRRGGBB)
VEX_IMPORT("palreset") void palreset(void);          // restore default palette

// Audio: four generic voices; any event is legal on any channel. All audio
// semantics below are shared by every host (C console, vex-run, vex-web):
//
//   - channels clamp to 0..3 (VEX_TONE_CHANNELS); freq clamps to 1..20000,
//     freq <= 0 silences the channel
//   - retriggering a busy channel restarts it cleanly at phase 0, no click
//   - ms > 0: decaying tone, exponential envelope to -48 dB over the
//     duration (ms caps at 5000)
//   - ms == 0: sustain at flat amplitude until the next event on the channel
//   - ms < 0: legacy flat ~100 ms blip
#define VEX_TONE_CHANNELS 4

// tone(): play a square wave on a voice.
VEX_IMPORT("tone") void tone(int channel, int freq, int ms);

// noise(): switch the voice to a noise source for this event -- a 16-bit
// LFSR stepped at 2*freq Hz, so freq acts as noise color/pitch. Same ms
// semantics as tone(); the next tone() flips the voice back to square.
VEX_IMPORT("noise") void noise(int channel, int freq, int ms);

// vol(): per-channel linear gain multiplier, v clamped to 0..64 with 64 ==
// unity (tracker-native range; the default). Applies live to whatever the
// channel is playing.
#define VEX_VOL_MAX 64
VEX_IMPORT("vol") void vol(int channel, int v);

// apos(): audio clock -- sample frames produced by the output stream since
// console start (48 kHz count; unsigned-wrap safe for ~12 hours). Use it to
// derive note/row boundaries instead of a frame accumulator so tempo stays
// sample-accurate regardless of display drift or frame throttling.
VEX_IMPORT("apos") int apos(void);

// pitch(): retune the voice currently sounding on `channel` (0..3) to `freq`
// Hz without restarting it -- phase, envelope, volume and duration are kept,
// and the wave continues from its current position at the new rate. Works on
// all voice kinds: square/noise half-period divisor and sample playback rate
// alike (freq clamps to 1..20000 for square/noise, 1..96000 for samples).
// No-op if the channel is silent. This is what makes arpeggio, vibrato and
// portamento click-free: retune instead of retrigger.
VEX_IMPORT("pitch") void pitch(int channel, int freq);

// sample(): trigger 8-bit signed PCM on a voice. `ptr`/`len` address raw
// bytes in linear memory (len clamps to 64 KiB and truncates at the end of
// memory; the bytes are captured when sample() is called -- later edits do
// not affect the playing voice). `rate` is the playback frequency in Hz
// (clamps to 1..96000); pitch() can slide it afterwards. `loop_len` > 0
// tail-loops the final loop_len bytes indefinitely (sustain, like
// tone()'s ms == 0); 0 plays once and ends naturally. The event replaces
// whatever was on the channel and obeys vol().
VEX_IMPORT("sample") void sample(int channel, const void* ptr, int len,
                                 int rate, int loop_len);

#endif // VEX_H
