//! vex.zig - cart SDK for writing vex carts in Zig (0.17-dev).
//!
//! Import it from your cart and call the drawing/input functions:
//!
//!   const vex = @import("vex");
//!
//!   export fn update() void {
//!       vex.cls(0);                  // clear to dark
//!       vex.text("HELLO", 4, 4, 12); // white text
//!   }
//!
//! A cart must export update() (called every frame at 60fps) and may export
//! boot() (called once at start). It draws by calling the functions below,
//! which the console links from the "env" import module.

// Screen is 320x180. Colors are SWEETIE-16 palette indices 0..15.
pub const WIDTH = 320;
pub const HEIGHT = 180;

// Buttons, as passed to btn().
pub const LEFT = 0;
pub const RIGHT = 1;
pub const UP = 2;
pub const DOWN = 3;
pub const A = 4;
pub const B = 5;

// Mouse buttons, as passed to mbtn().
pub const MOUSE_LEFT = 0;
pub const MOUSE_RIGHT = 1;
pub const MOUSE_MIDDLE = 2;

// Host API: functions the console provides via the "env" import module.
pub extern "env" fn cls(color: i32) void; // clear screen
pub extern "env" fn pset(x: i32, y: i32, color: i32) void; // set one pixel
pub extern "env" fn rect(x: i32, y: i32, w: i32, h: i32, color: i32) void; // filled rect
pub extern "env" fn rectb(x: i32, y: i32, w: i32, h: i32, color: i32) void; // rect outline
pub extern "env" fn circ(x: i32, y: i32, r: i32, color: i32) void; // filled circle
pub extern "env" fn circb(x: i32, y: i32, r: i32, color: i32) void; // circle outline
pub extern "env" fn ring(x: i32, y: i32, inner: i32, outer: i32, color: i32) void; // filled ring
pub extern "env" fn line(x0: i32, y0: i32, x1: i32, y1: i32, color: i32) void;
pub extern "env" fn tri(x1: i32, y1: i32, x2: i32, y2: i32, x3: i32, y3: i32, color: i32) void; // filled triangle
pub extern "env" fn trib(x1: i32, y1: i32, x2: i32, y2: i32, x3: i32, y3: i32, color: i32) void; // triangle outline
pub extern "env" fn text(s: [*:0]const u8, x: i32, y: i32, color: i32) void;
pub extern "env" fn title(s: [*:0]const u8) void; // set window title
pub extern "env" fn btn(button: i32) i32; // 1 if held, else 0
pub extern "env" fn mx() i32; // mouse x in screen pixels
pub extern "env" fn my() i32; // mouse y in screen pixels
pub extern "env" fn mbtn(button: i32) i32; // 1 if mouse button held
pub extern "env" fn pal(index: i32, rgb: i32) void; // override palette entry (0xRRGGBB)
pub extern "env" fn palreset() void; // restore default palette

// Convenience wrappers around the raw 0/1 input functions.
pub fn down(button: i32) bool {
    return btn(button) != 0;
}

pub fn mdown(button: i32) bool {
    return mbtn(button) != 0;
}
