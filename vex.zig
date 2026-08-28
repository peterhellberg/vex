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

/// Number of independent mixer voices (`tone()` channels, `0..3`).
pub const TONE_CHANNELS = 4;

/// Waveform (bits 6..7); persists per channel until changed.
pub const TONE_PULSE: i32 = 0;
/// LFSR stepped at 2*freq Hz.
pub const TONE_NOISE: i32 = 1 << 6;
/// Triangle; softer, good for bass.
pub const TONE_TRI: i32 = 2 << 6;

/// Duty cycle for pulses (bits 2..3).
pub const TONE_MODE0: i32 = 0; // 50%
pub const TONE_MODE1: i32 = 1 << 2; // 25%
pub const TONE_MODE2: i32 = 2 << 2; // 12.5%
pub const TONE_MODE3: i32 = 3 << 2; // 75%

/// Panning (bits 4..5); constant-power gains, center by default.
pub const TONE_PAN_LEFT: i32 = 1 << 4;
pub const TONE_PAN_RIGHT: i32 = 2 << 4;

/// Interpret the frequency parameter as a MIDI note number.
pub const TONE_NOTE_MODE: i32 = 1 << 8;

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
/// Like `blit`, but each source palette index is remapped through the 16-byte
/// table at `map` before the palette lookup.
pub extern "env" fn blitm(data: [*]const u8, x: i32, y: i32, w: i32, h: i32, key: i32, map: [*]const u8) void;
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
/// Play a tone at `freq` Hz (clamps to 1..20000), or a MIDI note number with
/// `TONE_NOTE_MODE` (middle C = 60). Build arguments with `toneSlide`,
/// `ToneDuration`, `ToneVolume`, and `toneFlags` -- layouts:
///   freq     low 16: start Hz; high 16: slide target over the sustain
///   duration sustain | release << 8 | decay << 16 | attack << 24 (frames)
///   volume   sustain level 0..100 | peak << 8 (peak 0 = 100 during attack)
///   flags    bits 0..1 channel | 2..3 duty | 4..5 pan | 6..7 waveform |
///            bit 8 note mode
pub extern "env" fn tone(freq: i32, duration: i32, volume: i32, flags: i32) void;

// ---- input -----------------------------------------------------------------

/// `true` while the button is held
pub fn down(button: i32) bool {
    return btn(button) != 0;
}

/// `true` while the mouse button is held
pub fn mdown(button: i32) bool {
    return mbtn(button) != 0;
}

/// `true` if the button was just pressed this frame
pub fn pressed(button: i32) bool {
    return btnp(button) != 0;
}

// ---- audio -----------------------------------------------------------------

/// Slide from `freq` to `to` over the sustain duration (linear in Hz).
pub fn toneSlide(freq: i32, to: i32) i32 {
    return (freq & 0xFFFF) | ((to & 0xFFFF) << 16);
}

/// ADSR duration in frames (each segment clamps to 0..255).
pub const ToneDuration = struct {
    attack: i32 = 0,
    decay: i32 = 0,
    sustain: i32 = 0,
    release: i32 = 0,

    pub fn pack(d: ToneDuration) i32 {
        return toneByte(d.sustain) |
            (toneByte(d.release) << 8) |
            (toneByte(d.decay) << 16) |
            (toneByte(d.attack) << 24);
    }
};

/// Sustain volume 0..100 with optional attack-time peak
/// (`peak == 0` means "use 100 during the attack").
pub const ToneVolume = struct {
    level: i32,
    peak: i32 = 0,

    pub fn pack(v: ToneVolume) i32 {
        return toneByte(v.level) |
            (toneByte(v.peak) << 8);
    }
};

/// Channel 0..3, duty mode, plus any TONE_* extras ORed together.
pub fn toneFlags(channel: i32, mode: i32, extra: i32) i32 {
    return (channel & 3) | (mode & (3 << 2)) |
        (extra & ~@as(i32, 3 | (3 << 2)));
}

/// One-shot description of a note to play. Every field except `channel` and
/// `freq` has a sensible default, so simple notes are one line:
///
/// ```zig
/// try vex.Note{ .channel = 0, .freq = 262 }.play();
/// ```
pub const Note = struct {
    channel: i32,
    freq: i32,
    attack: i32 = 0,
    decay: i32 = 0,
    sustain: i32 = 12,
    release: i32 = 4,
    volume: i32 = 100,
    peak: i32 = 0,
    mode: i32 = TONE_MODE0,
    wave: i32 = TONE_PULSE,
    pan: i32 = 0,
    /// Extra `TONE_*` bits (e.g. `TONE_NOTE_MODE`), ORed into the flags.
    extra: i32 = 0,

    pub fn play(n: Note) void {
        tone(
            n.freq,
            (ToneDuration{
                .sustain = n.sustain,
                .release = n.release,
                .decay = n.decay,
                .attack = n.attack,
            }).pack(),
            (ToneVolume{
                .level = n.volume,
                .peak = n.peak,
            }).pack(),
            toneFlags(
                n.channel,
                n.mode,
                n.wave | n.pan | n.extra,
            ),
        );
    }
};

/// Silence `channel` immediately: an all-zero duration ends whatever the
/// voice is playing.
pub fn silence(channel: i32) void {
    tone(440, 0, 0, toneFlags(channel, 0, 0));
}

fn toneByte(v: i32) i32 {
    return if (v < 0) 0 else if (v > 255) 255 else v;
}
