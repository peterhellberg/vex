// test_zmusic.zig - Beethoven Für Elise, grimy Berlin club remix.
// Down an octave, hollow 75% pulse + sub, hats shuffled, 1-bar hypnotic loop.

const vex = @import("vex");
const mus = @import("mus");

const INSTS = [_]mus.Inst{
    // 0: kick/bass - pulse 75% hollow, very low, short punchy, drives soft-clip
    .{ .wave = vex.TONE_PULSE, .duty = vex.TONE_MODE3, .attack = 0, .decay = 1, .sustain = 4, .release = 10, .volume = 92, .pan = 0 },
    // 1: Für Elise lead - pulse 12.5% but down 12 semitones (E4 not E5), thin/dark
    .{ .wave = vex.TONE_PULSE, .duty = vex.TONE_MODE2, .attack = 0, .decay = 2, .sustain = 0, .release = 7, .volume = 52, .pan = vex.TONE_PAN_LEFT },
    // 2: sub - TRI 2 octaves below lead, just to rattle the chest
    .{ .wave = vex.TONE_TRI, .duty = vex.TONE_MODE0, .attack = 0, .decay = 1, .sustain = 0, .release = 14, .volume = 48, .pan = vex.TONE_PAN_RIGHT },
    // 3: hats - noise, shuffled, lower 9k + ghost 8k, quieter
    .{ .wave = vex.TONE_NOISE, .duty = 0, .attack = 0, .decay = 0, .sustain = 1, .release = 3, .volume = 18, .pan = 0 },
};

const ROWS: u8 = 32; // 8 bars × 4 beats, hypnotic 1-bar loop repeated
const SPD: u8 = 7; // 8.5 rows/sec → 128 BPM (4 rows=beat)

const R = mus.Event{ .note = mus.REST, .inst = 0, .vol = 0 };
const O = mus.Event{ .note = mus.OFF, .inst = 0, .vol = 0 };
fn N(n: u8, i: u8) mus.Event {
    return .{ .note = n, .inst = i, .vol = 0 };
}

// PAT0: A minor E4 D#4 etc (down 12, less music-box), bass C1-A1 every beat
// Bass is now C1 (24) / A1 (33) etc — sub rumble, not A2.
// Hats shuffled: 110, 108, ghost 96, not straight.
// Lead is sparse — 1 note per 2 rows, leaves space = grimy.
const EV0 = [_]mus.Event{
    // row: ch0(lead) ch1      ch2(bass) ch3(hat) | bar
    N(64, 2), R, N(33, 1), N(110, 4),  N(63, 2), R, N(33, 1), R,   N(64, 2), R, N(45, 1), N(108, 4),  N(63, 2), R, R,       R, // 0-3  E4 D#4 / C1 A1
    N(64, 2), R, N(33, 1), N(110, 4),  N(59, 2), R, N(33, 1), R,   N(62, 2), R, N(40, 1), N(96, 4),   N(60, 2), R, R,       R, // 4-7  B3 D4 C4
    N(57, 2), R, N(45, 1), N(110, 4),  R,       R, N(21, 3), R,   R,       R, N(33, 1), N(108, 4),  R,       R, R,       R, // 8-11 A3 + sub drop C1
    N(48, 2), R, N(36, 1), N(110, 4),  N(52, 2), R, N(36, 1), R,   N(57, 2), R, N(48, 1), N(108, 4),  R,       R, R,       R, //12-15 C3 E3 A3 — low
    N(59, 2), R, N(40, 1), N(110, 4),  N(52, 2), R, N(40, 1), R,   N(56, 2), R, N(44, 1), N(96, 4),   N(59, 2), R, R,       R, //16-19 E3 G#3 B3
    N(59, 2), R, N(47, 1), N(110, 4),  N(60, 2), R, N(47, 1), R,   N(64, 2), R, N(48, 1), N(108, 4),  R,       R, R,       R, //20-23 B3 C4 E4
    N(63, 2), R, N(33, 1), N(110, 4),  N(64, 2), R, N(33, 1), R,   N(64, 2), R, N(45, 1), N(108, 4),  N(59, 2), R, R,       R, //24-27 again E4 D#4
    N(62, 2), R, N(40, 1), N(110, 4),  N(60, 2), R, N(40, 1), R,   N(57, 2), R, N(45, 1), N(112, 4),  R,       R, R,       R, //28-31 snare 112
};

const PAT0 = mus.Pat{ .rows = ROWS, .speed = SPD, .events = &EV0 };
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
    mus.load(&SONG);
    mus.play();
    vex.title("Für Elise - grimy Berlin");
}

export fn update() void {
    mus.tick();
    const pos = mus.pos();
    const row: i32 = (pos >> 8) & 0xFF;

    vex.cls(0);
    vex.rectb(2, 2, vex.WIDTH - 4, vex.HEIGHT - 4, 2);
    vex.text("Fur Elise (grimy)", 4, 4, 12);
    vex.text("128 BPM - Berghain", 4, 14, 10);
    vex.rect(4, 30, @divFloor((vex.WIDTH - 24) * @rem(row, @as(i32, ROWS)), ROWS), 4, 6);

    const base = if (row < ROWS) EV0[@as(usize, @intCast(@rem(row, @as(i32, ROWS)))) * mus.CHANNELS ..][0..mus.CHANNELS] else EV0[0..mus.CHANNELS];
    for (0..mus.CHANNELS) |ch| {
        const ev = base[ch];
        const color: i32 = if (ev.note != mus.REST) 11 else 5;
        vex.rect(4 + @as(i32, @intCast(ch)) * 12, 44, 8, 8, color);
    }
    vex.text("mus.zig", 4, vex.HEIGHT - 10, 10);
}
