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

// ---- audio -----------------------------------------------------------------
// One import covers all synthesized sound: four independent voices rendered
// by the console mixer. Time is measured in frames (1/60 s) -- the console
// schedules the envelopes itself, so carts never need an audio clock.
//
//   - channels clamp to 0..3; retriggering a busy voice restarts it cleanly
//     at phase 0 (attack > 0 makes retriggering click-free)
//   - everything about a call is packed: slide target in the freq bits, ADSR
//     in the duration bits, peak level in the volume bits, channel/duty/pan/
//     waveform/note-mode in flags. Prefer the tone_* helpers over hand-
//     packing.

#define VEX_TONE_CHANNELS 4

// tone(): play a tone at `freq` Hz (clamps to 1..20000), or as a MIDI note
// number with TONE_NOTE_MODE (middle C = 60).
//
//   freq     low 16 bits: start frequency; high 16 bits: optional slide
//            target reached linearly over the sustain duration
//   duration four ADSR segment lengths in frames, each 0..255:
//            sustain | release << 8 | decay << 16 | attack << 24 -- build
//            with tone_duration()
//   volume   low byte: sustain level 0..100; high byte: optional attack-time
//            peak level, 0 meaning "use 100" -- build with tone_volume()
//   flags    channel, duty cycle, panning, waveform, note mode; see the
//            TONE_* constants and tone_flags()
VEX_IMPORT("tone") void tone(int freq, int duration, int volume, int flags);

// Waveform selection (bits 6..7). Persists on the channel until the next
// call says otherwise; the default is a pulse wave.
#define TONE_PULSE 0                 // value 0
#define TONE_NOISE (1 << 6)          // 15-bit LFSR stepped at 2*freq Hz,
                                     // so freq acts as noise color/pitch
#define TONE_TRI   (2 << 6)          // triangle; softer, good for bass

// Duty cycle for pulse waves (bits 2..3). TONE_MODE0 is the classic square.
#define TONE_MODE0 0                 // 50%
#define TONE_MODE1 (1 << 2)          // 25%
#define TONE_MODE2 (2 << 2)          // 12.5%
#define TONE_MODE3 (3 << 2)          // 75%

// Panning (bits 4..5): constant-power gains, center by default.
#define TONE_PAN_LEFT  (1 << 4)
#define TONE_PAN_RIGHT (2 << 4)

// Note mode (bit 8): interpret the frequency parameter as a MIDI note
// number instead of hertz for exact pitches (middle C = 60).
#define TONE_NOTE_MODE (1 << 8)

// The helpers below are macros so they work in static initializers (cart
// data tables); each argument is clamped and masked per byte.

// Slide from `freq` to `to` over the sustain duration (linear in Hz).
#define TONE_SLIDE(freq, to) \
  (((freq) & 0xFFFF) | (((to) & 0xFFFF) << 16))

#define TONE_DUR_BYTE(v) ((v) < 0 ? 0 : (v) > 255 ? 255 : (v))

// ADSR duration in frames:
//   sustain: held at the sustain volume
//   release: ramp back down to 0 afterwards
//   decay:   ramp from peak down to sustain
//   attack:  ramp from 0 up to peak
#define TONE_DURATION(sustain, release, decay, attack) \
  (TONE_DUR_BYTE(sustain) | (TONE_DUR_BYTE(release) << 8) | \
   (TONE_DUR_BYTE(decay) << 16) | (TONE_DUR_BYTE(attack) << 24))

#define TONE_VOL_BYTE(v) ((v) < 0 ? 0 : (v) > 100 ? 100 : (v))

// Sustain volume 0..100 with optional attack-time peak;
// peak == 0 means "use 100 during the attack".
#define TONE_VOLUME(volume, peak) \
  (TONE_VOL_BYTE(volume) | (TONE_VOL_BYTE(peak) << 8))

// Channel 0..3, duty mode, plus any TONE_* extras ORed together.
#define TONE_FLAGS(channel, mode, extra) \
  (((channel) & 3) | ((mode) & (3 << 2)) | ((extra) & ~(3 | (3 << 2))))

#endif // VEX_H
