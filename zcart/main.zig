// Example vex cart, written in Zig (0.17-dev) and compiled to wasm32.
//
// Move the player square with the arrow keys; press A (Z) to change its fill
// color. A ball bounces around the screen. The console API is imported from
// the reusable vex.zig SDK.

const vex = @import("vex");

const W = vex.WIDTH;
const H = vex.HEIGHT;
const P = 18;
const R = 8; // ball radius

// All mutable cart state lives on a single struct instance.
const State = struct {
    px: i32 = (W - P) / 2, // player position
    py: i32 = (H - P) / 2,
    bx: i32 = 40, // ball position
    by: i32 = 60,
    vx: i32 = 1, // ball velocity
    vy: i32 = 1,
    t: i32 = 0, // frame counter, drives the palette pulse
};

var state: State = .{};

export fn boot() void {
    vex.title("vex - Zig cart");
    state.px = (W - P) / 2;
    state.py = (H - P) / 2;
}

export fn update() void {
    const s = &state;

    const down = vex.down;

    // Move the player, clamped to the screen.
    if (down(vex.LEFT) and s.px > 0) s.px -= 1;
    if (down(vex.RIGHT) and s.px < W - P) s.px += 1;
    if (down(vex.UP) and s.py > 0) s.py -= 1;
    if (down(vex.DOWN) and s.py < H - P) s.py += 1;

    // Bounce the ball off the walls.
    s.bx += s.vx;
    s.by += s.vy;

    if (s.bx < R or s.bx > W - R) s.vx = -s.vx;
    if (s.by < R or s.by > H - R) s.vy = -s.vy;

    // Pulse palette index 10 (the ball's color) to show live palette changes.
    s.t += 1;
    const phase = @mod(s.t, 120);
    const k = if (phase < 60) phase else 120 - phase; // triangle wave 0..60..0
    const blue = 39 + k * 3;
    vex.pal(10, (255 << 16) | (236 << 8) | blue); // 0xRRGGBB

    draw(s);
}

inline fn draw(s: *State) void {
    vex.cls(0); // dark background

    // Subtle guide line, behind everything: player center -> bottom center.
    vex.line(s.px + P / 2, s.py + P / 2, W / 2, H - 1, 15);

    vex.text("VEX ZIG", 6, 6, 12); // white
    vex.text("ARROWS + Z", 6, 18, 13); // muted blue-grey

    vex.circb(60, 44, 9, 13); // outlined moon

    // Player: filled square (red while A held, otherwise green) with a border.
    const fill: i32 = if (vex.down(vex.A)) 2 else 5;

    vex.rect(s.px, s.py, P, P, fill);
    vex.rectb(s.px, s.py, P, P, 12); // white border

    const cfill: i32 = if (vex.mdown(vex.MOUSE_LEFT)) 2 else 12;

    vex.line(0, H, vex.mx(), vex.my(), cfill);

    // Mountain range across the width: alternating filled and outlined peaks.
    vex.tri(0, H - 1, 48, H - 60, 96, H - 1, 14);
    vex.trib(72, H - 1, 132, H - 84, 192, H - 1, 12);
    vex.tri(150, H - 1, 210, H - 52, 270, H - 1, 14);
    vex.trib(248, H - 1, 296, H - 72, 319, H - 1, 12);

    vex.circ(s.bx, s.by, R, 10); // ball (palette index 10, pulsed above)
    vex.ring(s.bx, s.by, R + 3, R + 5, 11); // cyan ring orbiting the ball

    // Mouse cursor: white dot, red while the left button is held.
    vex.circ(vex.mx(), vex.my(), 3, cfill);
}
