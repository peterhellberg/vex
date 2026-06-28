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
