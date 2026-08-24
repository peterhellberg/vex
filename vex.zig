//! Cart SDK for writing vex carts in Zig (0.17-dev).
//!
//! Import it from your cart and call the drawing/input functions:
//!
//! ```zig
//! const vex = @import("vex");
//!
//! export fn update() void {
//!     vex.cls(0);                  // clear to dark
//!     vex.text("HELLO", 4, 4, 12); // white text
//! }
//! ```
//!
//! Compilation without a build.zig is done like this:
//!
//! ```
//! zig build-exe \
//!     -target wasm32-freestanding \
//!     -O ReleaseSmall -fno-entry -rdynamic --dep vex \
//!     -femit-bin=cart.wasm -Mroot=cart.zig -Mvex=vex.zig
//! ```
//! A cart must export `update()` (called every frame at 60 fps) and may
//! export `boot()` (called once at start). It draws by calling the functions
//! below, which the console links from the `env` import module.
//!
//! Coordinates are in framebuffer pixels (`0,0` is the top-left) and every
//! `color` is a SWEETIE-16 palette index `0..15`.

/// Framebuffer width, in pixels.
pub const WIDTH = 320;
/// Framebuffer height, in pixels.
pub const HEIGHT = 180;

/// `btn()` index for the left arrow.
pub const LEFT = 0;
/// `btn()` index for the right arrow.
pub const RIGHT = 1;
/// `btn()` index for the up arrow.
pub const UP = 2;
/// `btn()` index for the down arrow.
pub const DOWN = 3;
/// `btn()` index for the Z button.
pub const Z = 4;
/// `btn()` index for the X button.
pub const X = 5;

/// `mbtn()` index for the left mouse button.
pub const MOUSE_LEFT = 0;
/// `mbtn()` index for the right mouse button.
pub const MOUSE_RIGHT = 1;
/// `mbtn()` index for the middle mouse button.
pub const MOUSE_MIDDLE = 2;

/// Clear the whole screen to `color`.
pub extern "env" fn cls(color: i32) void;
/// Set the single pixel at (`x`, `y`) to `color`.
pub extern "env" fn pset(x: i32, y: i32, color: i32) void;
/// Draw a filled `w`×`h` rectangle with its top-left corner at (`x`, `y`).
pub extern "env" fn rect(x: i32, y: i32, w: i32, h: i32, color: i32) void;
/// Draw a 1px outline of a `w`×`h` rectangle at (`x`, `y`).
pub extern "env" fn rectb(x: i32, y: i32, w: i32, h: i32, color: i32) void;
/// Draw a filled circle of radius `r` centered at (`x`, `y`).
pub extern "env" fn circ(x: i32, y: i32, r: i32, color: i32) void;
/// Draw a circle outline of radius `r` centered at (`x`, `y`).
pub extern "env" fn circb(x: i32, y: i32, r: i32, color: i32) void;
/// Draw a line from (`x0`, `y0`) to (`x1`, `y1`).
pub extern "env" fn line(x0: i32, y0: i32, x1: i32, y1: i32, color: i32) void;
/// Draw a filled triangle through the three points (any winding order).
pub extern "env" fn tri(x1: i32, y1: i32, x2: i32, y2: i32, x3: i32, y3: i32, color: i32) void;
/// Draw a triangle outline through the three points.
pub extern "env" fn trib(x1: i32, y1: i32, x2: i32, y2: i32, x3: i32, y3: i32, color: i32) void;
/// Draw a `w`×`h` bitmap of palette indices (one byte per pixel) from `data`,
/// top-left at (`x`, `y`). Pixels equal to `key` are skipped — pass a value
/// outside `0..15` (e.g. `-1`) to draw every pixel.
pub extern "env" fn blit(data: [*]const u8, x: i32, y: i32, w: i32, h: i32, key: i32) void;
/// Draw the NUL-terminated string `s` with its top-left at (`x`, `y`).
pub extern "env" fn text(s: [*:0]const u8, x: i32, y: i32, color: i32) void;
/// Set the console window title to the NUL-terminated string `s`.
pub extern "env" fn title(s: [*:0]const u8) void;
/// Return `1` while the button is held, else `0`. See `LEFT`…`X`.
pub extern "env" fn btn(button: i32) i32;
/// Return `1` if the button was just pressed this frame, else `0`.
pub extern "env" fn btnp(button: i32) i32;
/// Mouse x position, in framebuffer pixels (`0`…`WIDTH - 1`).
pub extern "env" fn mx() i32;
/// Mouse y position, in framebuffer pixels (`0`…`HEIGHT - 1`).
pub extern "env" fn my() i32;
/// Return `1` while the mouse button is held, else `0`.
/// See `MOUSE_LEFT`, `MOUSE_RIGHT`, `MOUSE_MIDDLE`.
pub extern "env" fn mbtn(button: i32) i32;
/// Override palette entry `index` (`0..15`) with a packed `0xRRGGBB` color.
pub extern "env" fn pal(index: i32, rgb: i32) void;
/// Restore the default SWEETIE-16 palette.
pub extern "env" fn palreset() void;
// Audio: four generic voices; any event is legal on any channel. All audio
// semantics below are shared by every host (C console, vex-run, vex-web):
//
//   - channels clamp to 0..3 (TONE_CHANNELS); freq clamps to 1..20000,
//     freq <= 0 silences the channel
//   - retriggering a busy channel restarts it cleanly at phase 0, no click
//   - ms > 0: decaying tone, exponential envelope to -48 dB over the
//     duration (ms caps at 5000)
//   - ms == 0: sustain at flat amplitude until the next event on the channel
//   - ms < 0: legacy flat ~100 ms blip
pub const TONE_CHANNELS = 4;

/// Play a square wave on a voice.
pub extern "env" fn tone(channel: i32, freq: i32, ms: i32) void;

/// Switch the voice to a noise source for this event -- a 16-bit LFSR
/// stepped at 2*freq Hz, so freq acts as noise color/pitch. Same ms
/// semantics as tone(); the next tone() flips the voice back to square.
pub extern "env" fn noise(channel: i32, freq: i32, ms: i32) void;

/// Per-channel linear gain multiplier, `v` clamped to 0..`VOL_MAX` with
/// `VOL_MAX` == unity (tracker-native range; the default). Applies live to
/// whatever the channel is playing.
pub const VOL_MAX = 64;
pub extern "env" fn vol(channel: i32, v: i32) void;

/// Audio clock -- sample frames produced by the output stream since console
/// start (48 kHz count; unsigned-wrap safe for ~12 hours). Use it to derive
/// note/row boundaries instead of a frame accumulator so tempo stays
/// sample-accurate regardless of display drift or frame throttling.
pub extern "env" fn apos() i32;

/// Retune the voice currently sounding on `channel` (0..3) to `freq` Hz
/// without restarting it: phase, envelope, volume and duration are kept,
/// and the wave continues from its current position at the new rate. Works
/// on all voice kinds (freq clamps to 1..20000 for square/noise, 1..96000
/// for samples). No-op if the channel is silent. This is what makes
/// arpeggio, vibrato and portamento click-free: retune instead of
/// retrigger.
pub extern "env" fn pitch(channel: i32, freq: i32) void;

/// Trigger 8-bit signed PCM on a voice. `ptr`/`len` address raw bytes in
/// linear memory (len clamps to 64 KiB and truncates at the end of memory;
/// the bytes are captured when sample() is called -- later edits do not
/// affect the playing voice). `rate` is the playback frequency in Hz
/// (clamps to 1..96000); pitch() can slide it afterwards. `loop_len` > 0
/// tail-loops the final loop_len bytes indefinitely (sustain, like tone()'s
/// ms == 0); 0 plays once and ends naturally. The event replaces whatever
/// was on the channel and obeys vol().
pub extern "env" fn sample(channel: i32, ptr: [*]const u8, len: i32,
                           rate: i32, loop_len: i32) void;

/// `true` while the button is held — shorthand for `btn(button) != 0`.
pub fn down(button: i32) bool {
    return btn(button) != 0;
}

/// `true` while the mouse button is held — shorthand for `mbtn(button) != 0`.
pub fn mdown(button: i32) bool {
    return mbtn(button) != 0;
}

/// `true` if the button was just pressed this frame — shorthand for `btnp(button) != 0`.
pub fn pressed(button: i32) bool {
    return btnp(button) != 0;
}
