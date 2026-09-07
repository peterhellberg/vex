// test_zmusic.zig - Beethoven Für Elise, grimy Berlin club remix.
// Down an octave, hollow 75% pulse + sub, hats shuffled, 1-bar hypnotic loop.
//
// Uses per-note vol overrides for kick/hat accents, and OFF gates on the
// bass channel so release tails land on the downbeat.

const vex = @import("vex");
const mus = @import("mus");

const INSTS = [_]mus.Inst{
    // 0: kick/bass - pulse 75% hollow, very low, short punchy, drives soft-clip
    .{
        .wave = vex.TONE_PULSE,
        .duty = vex.TONE_MODE3,
        .attack = 0,
        .decay = 1,
        .sustain = 4,
        .release = 10,
        .volume = 92,
        .pan = 0,
    },
    // 1: Für Elise lead - pulse 12.5% but down 12 semitones (E4 not E5), thin/dark
    .{
        .wave = vex.TONE_PULSE,
        .duty = vex.TONE_MODE2,
        .attack = 0,
        .decay = 2,
        .sustain = 0,
        .release = 7,
        .volume = 52,
        .pan = vex.TONE_PAN_LEFT,
    },
    // 2: sub - TRI 2 octaves below lead, just to rattle the chest.
    //    SUSTAIN_HOLD: rings until an explicit OFF, so it stays under phrases.
    .{
        .wave = vex.TONE_TRI,
        .duty = vex.TONE_MODE0,
        .attack = 0,
        .decay = 1,
        .sustain = mus.SUSTAIN_HOLD,
        .release = 14,
        .volume = 48,
        .pan = 0,
    },
    // 3: hats - noise, shuffled, lower 9k + ghost 8k, quieter
    .{
        .wave = vex.TONE_NOISE,
        .duty = 0,
        .attack = 0,
        .decay = 0,
        .sustain = 1,
        .release = 3,
        .volume = 18,
        .pan = 0,
    },
};

const ROWS: i32 = 32; // 8 bars × 4 beats, hypnotic 1-bar loop repeated
const SPD: u8 = 7; // 8.5 rows/sec → 128 BPM (4 rows=beat)

const R = mus.Event{ .note = mus.REST, .inst = 0, .vol = 0 };
const O = mus.Event{ .note = mus.OFF, .inst = 0, .vol = 0 };

fn N(n: u8, i: u8) mus.Event {
    return .{ .note = n, .inst = i, .vol = 0 };
}

/// Note with velocity override (1..100). 0 = use instrument volume.
fn Nv(n: u8, i: u8, v: u8) mus.Event {
    return .{ .note = n, .inst = i, .vol = v };
}

// Velocity conventions:
//   kick:  100 on the downbeat of each bar, 78 on pumps → soft-clip pumping
//   hats:  20 downbeat, 12 offbeat/ghosts, 14 mid, 26 on the snare hit
const K1: u8 = 100;
const K2: u8 = 78;
const H1: u8 = 20;
const H2: u8 = 12;
const H3: u8 = 14;
const HSNARE: u8 = 26;

// PAT0: A minor E4 D#4 etc — ch1 harmony a third above lead (grimy thickening),
// bass C1-A1 every beat with accented downbeats, hats shuffled with velocity.
const EV0 = [_]mus.Event{
    // row: ch0(lead) ch1(harm) ch2(bass) ch3(hat) | bar
    N(64, 2), N(68, 2), Nv(33, 1, K1), Nv(110, 4, H1), N(63, 2), N(67, 2), Nv(33, 1, K2), Nv(108, 4, H2), N(64, 2), N(68, 2), Nv(45, 1, K2), Nv(96, 4, H3), N(63, 2), N(67, 2), R, R, // 0-3  E4+G#4 / C1 A1
    N(64, 2), N(68, 2), Nv(33, 1, K1), Nv(110, 4, H1), N(59, 2), N(62, 2), Nv(33, 1, K2), Nv(96, 4, H2), N(62, 2), N(65, 2), Nv(40, 1, K2), Nv(108, 4, H3), N(60, 2), N(64, 2), R, R, // 4-7  B3+D4 / etc
    N(57, 2), N(60, 2), Nv(45, 1, K1), Nv(110, 4, H1), R, R, Nv(33, 3, K2), R, R, R, Nv(33, 1, K2), Nv(108, 4, H3), R, R, O, R, // 8-11 A3+C4 + sub A1, gated into next bar
    N(48, 2), N(52, 2), Nv(36, 1, K1), Nv(110, 4, H1), N(52, 2), N(56, 2), Nv(36, 1, K2), Nv(108, 4, H2), N(57, 2), N(60, 2), Nv(48, 1, K2), Nv(96, 4, H3), R, R, R, R, // 12-15 C3+E3 / A3+C4
    N(59, 2), N(62, 2), Nv(40, 1, K1), Nv(110, 4, H1), N(52, 2), N(55, 2), Nv(40, 1, K2), Nv(96, 4, H2), N(56, 2), N(59, 2), Nv(44, 1, K2), Nv(108, 4, H3), N(59, 2), N(62, 2), R, R, // 16-19 E3+G#3 / etc
    N(59, 2), N(62, 2), Nv(47, 1, K1), Nv(110, 4, H1), N(60, 2), N(64, 2), Nv(47, 1, K2), Nv(108, 4, H2), N(64, 2), N(67, 2), Nv(48, 1, K2), Nv(96, 4, H3), R, R, R, R, // 20-23 B3+D4 / C4+E4
    N(63, 2), N(66, 2), Nv(33, 1, K1), Nv(110, 4, H1), N(64, 2), N(68, 2), Nv(33, 1, K2), Nv(108, 4, H2), N(64, 2), N(68, 2), Nv(45, 1, K2), Nv(96, 4, H3), N(59, 2), N(62, 2), R, R, // 24-27 again
    N(62, 2), N(65, 2), Nv(40, 1, K1), Nv(110, 4, H1), N(60, 2), N(64, 2), Nv(40, 1, K2), Nv(108, 4, H2), N(57, 2), N(60, 2), Nv(45, 1, K2), Nv(112, 4, HSNARE), R, R, O, R, // 28-31 snare, bass gated
};

const PAT0 = mus.Pat{ .rows = @intCast(ROWS), .speed = SPD, .events = &EV0 };
const PATS = [_]*const mus.Pat{&PAT0};
const ORDERS = [_]u8{0};
const SONG = mus.Song{
    .num_insts = @intCast(INSTS.len),
    .num_pats = @intCast(PATS.len),
    .num_orders = @intCast(ORDERS.len),
    .loop_ord = 0,
    .insts = &INSTS,
    .pats = &PATS,
    .orders = &ORDERS,
};

export fn boot() void {
    // CORRUPTION-16 — https://lospec.com/palette-list/corruption-16
    vex.pal(0, 0xe1d8cb);
    vex.pal(1, 0xc3b197);
    vex.pal(2, 0xa68a64);
    vex.pal(3, 0x614f38);
    vex.pal(4, 0x362c20);
    vex.pal(5, 0x82998f);
    vex.pal(6, 0x525e5a);
    vex.pal(7, 0x3a4040);
    vex.pal(8, 0x222326);
    vex.pal(9, 0x0a0d0a);
    vex.pal(10, 0x20331f);
    vex.pal(11, 0x495840);
    vex.pal(12, 0x888f72);
    vex.pal(13, 0xc7c7a5);
    vex.pal(14, 0xde8d7d);
    vex.pal(15, 0x833121);

    mus.load(&SONG);
    mus.play();

    vex.title("Für Elise - grimy Berlin");
}

var frame: i32 = 0;
var vu: [4]i32 = .{ 0, 0, 0, 0 };
var paused: bool = false;
var grain: u32 = 0x12345;

export fn update() void {
    if (vex.pressed(vex.Z)) paused = !paused;
    if (!paused) mus.tick();

    frame = (frame + 1) & 0xFFFFF; // wrap so the *% grain math stays sane

    const pos = mus.pos();
    const row: i32 = (pos >> 8) & 0xFF;
    const cur = @rem(row, ROWS);

    // decay VU meters
    for (0..mus.CHANNELS) |ch| {
        if (vu[ch] > 0) vu[ch] -= 2;
        if (vu[ch] < 0) vu[ch] = 0;
    }

    const base = EV0[@as(usize, @intCast(cur)) * mus.CHANNELS ..][0..mus.CHANNELS];

    for (0..mus.CHANNELS) |ch| {
        // a triggered note lights the meter; OFF must not
        if (base[ch].note != mus.REST and base[ch].note != mus.OFF) vu[ch] = 28;
    }

    vex.cls(9); // almost black
    vex.rectb(2, 2, vex.WIDTH - 4, vex.HEIGHT - 4, 7);

    vex.text("Fur Elise (grimy)", 6, 6, 13);
    vex.text("128 BPM", vex.WIDTH - 62, 6, 5);

    // progress
    vex.rect(6, 18, @divFloor((vex.WIDTH - 12) * cur, ROWS), 2, 14);

    // pattern view — 32 rows × 4 ch, each channel distinct
    var r: i32 = 0;
    while (r < ROWS) : (r += 1) {
        const y = 28 + r * 3;
        const isCur = r == cur;
        for (0..mus.CHANNELS) |ch| {
            const ev = EV0[@as(usize, @intCast(r)) * mus.CHANNELS + ch];
            const x = 6 + @as(i32, @intCast(ch)) * 78;
            const w: i32 = 72;
            if (ev.note == mus.REST or ev.note == mus.OFF) {
                if (isCur) vex.rect(x, y, w, 1, 8);
            } else {
                const v = @as(i32, @intCast(ev.note)) - 40; // 0..~80
                if (ch == 2) { // bass — thick, dark, full width
                    vex.rect(x, y, w, 3, if (isCur) 3 else 4);
                } else if (ch == 3) { // hats — dot, jittered by pitch, lit by vol
                    const cx = x + @divFloor(w - 6, 2) + @rem(v * 7 + 3, 8);
                    const bright: i32 = if (ev.vol >= HSNARE) 14 else if (ev.vol >= H1) 12 else 5;
                    vex.rect(cx, y, 6, 2, if (isCur) 5 else bright);
                    if (isCur) vex.rect(cx, y - 1, @divFloor(6 * vu[ch], 28), 1, 5);
                } else { // lead/harm — pitch = length, brightness = octave
                    const len = 24 + @divFloor(v * 28, 40); // 24..52
                    const col: i32 = if (ch == 0) 14 else 11;
                    const vv: i32 = if (isCur) col else if (v > 30) 13 else 6;
                    vex.rect(x, y, len, 2, vv);
                    if (isCur) vex.rect(x, y - 1, @divFloor(len * vu[ch], 28), 1, 14);
                }
            }
        }

        if (isCur) {
            vex.rect(4, y, 2, 2, 14);
            vex.rect(4 + 78 * 4 + 2, y, 2, 2, 14);
        }
    }

    // large VU per channel at bottom, reacts to trigger
    for (0..mus.CHANNELS) |ch| {
        const x = 6 + @as(i32, @intCast(ch)) * 78;

        vex.rect(x, 130, 72, 4, 8);
        vex.rect(x, 130, @divFloor(72 * vu[ch], 28), 4, if (ch == 0) 14 else if (ch == 2) 2 else 5);
    }

    // subtle grain: 2×2 specks, 2 per frame
    grain = grain *% 1664525 +% 1013904223;

    const c: i32 = if (grain & 1 != 0) 12 else 6;

    vex.rect(
        @intCast((grain >> 16) % @as(u32, @intCast(vex.WIDTH - 1))),
        @intCast((grain >> 8) % @as(u32, @intCast(vex.HEIGHT - 1))),
        1,
        2,
        c,
    );

    if (paused) {
        vex.rect(vex.WIDTH / 2 - 56, vex.HEIGHT / 2 - 14, 112, 28, 9);
        vex.rectb(vex.WIDTH / 2 - 56, vex.HEIGHT / 2 - 14, 112, 28, 7);

        vex.text("PAUSED", vex.WIDTH / 2 - 24, vex.HEIGHT / 2 - 10, 14);
        vex.text("Z to resume", vex.WIDTH / 2 - 44, vex.HEIGHT / 2 + 2, 12);
    } else {
        vex.text("Z to pause", 6, vex.HEIGHT - 13, 12);
    }
}
