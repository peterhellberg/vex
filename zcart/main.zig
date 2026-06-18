// Example vex cart, written in Zig (0.17-dev) and compiled to wasm32.
//
// Move the player square with the arrow keys; press A (Z) to change its fill
// color. A ball bounces around the screen. Same console API as the C cart,
// imported from the "env" module the host links.

const W: i32 = 128;
const H: i32 = 128;
const PLAYER: i32 = 12;
const R: i32 = 6; // ball radius

// Host API: functions the console provides via the "env" import module.
extern "env" fn cls(color: i32) void;
extern "env" fn rect(x: i32, y: i32, w: i32, h: i32, color: i32) void;
extern "env" fn rectb(x: i32, y: i32, w: i32, h: i32, color: i32) void;
extern "env" fn circ(x: i32, y: i32, r: i32, color: i32) void;
extern "env" fn line(x0: i32, y0: i32, x1: i32, y1: i32, color: i32) void;
extern "env" fn text(s: [*:0]const u8, x: i32, y: i32, color: i32) void;
extern "env" fn btn(button: i32) i32;

// Buttons.
const LEFT: i32 = 0;
const RIGHT: i32 = 1;
const UP: i32 = 2;
const DOWN: i32 = 3;
const A: i32 = 4;

var px: i32 = (W - PLAYER) / 2;
var py: i32 = (H - PLAYER) / 2;
var bx: i32 = 24;
var by: i32 = 80;
var vx: i32 = 1;
var vy: i32 = 1;

fn down(button: i32) bool {
    return btn(button) != 0;
}

export fn boot() void {
    px = (W - PLAYER) / 2;
    py = (H - PLAYER) / 2;
}

export fn update() void {
    // Move the player, clamped to the screen.
    if (down(LEFT) and px > 0) px -= 1;
    if (down(RIGHT) and px < W - PLAYER) px += 1;
    if (down(UP) and py > 0) py -= 1;
    if (down(DOWN) and py < H - PLAYER) py += 1;

    // Bounce the ball off the walls.
    bx += vx;
    by += vy;
    if (bx < R or bx > W - R) vx = -vx;
    if (by < R or by > H - R) vy = -vy;

    cls(1); // dark blue background
    text("VEX ZIG", 4, 4, 7);
    text("ARROWS + Z", 4, 14, 13);

    circ(bx, by, R, 10); // yellow ball

    // Player: filled square (red while A held, otherwise green) with a border.
    const fill: i32 = if (down(A)) 8 else 11;
    rect(px, py, PLAYER, PLAYER, fill);
    rectb(px, py, PLAYER, PLAYER, 7);

    line(0, H - 1, W - 1, H - 1, 3); // ground
}
