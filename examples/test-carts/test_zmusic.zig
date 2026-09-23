// test_zmusic.zig - Zig port of test_music.c.

const vex = @import("vex");
const mus = @import("mus");

const INSTS = [_]mus.Inst{
    .{ .wave = vex.TONE_PULSE, .duty = vex.TONE_MODE1, .attack = 0, .decay = 1, .sustain = mus.SUSTAIN_HOLD, .release = 6, .volume = 84, .pan = 0, .pwm = 1, .pwm_start = 64, .pwm_end = 64 },
    .{ .wave = vex.TONE_PULSE, .duty = vex.TONE_MODE2, .attack = 0, .decay = 0, .sustain = 0, .release = 4, .volume = 52, .pan = vex.TONE_PAN_LEFT, .pwm = 1, .pwm_start = 16, .pwm_end = 192 },
    .{ .wave = vex.TONE_PULSE, .duty = vex.TONE_MODE2, .attack = 0, .decay = 1, .sustain = 3, .release = 3, .volume = 34, .pan = vex.TONE_PAN_RIGHT, .pwm = 1, .pwm_start = 32, .pwm_end = 160 },
    .{ .wave = vex.TONE_NOISE, .duty = 0, .attack = 0, .decay = 0, .sustain = 1, .release = 4, .volume = 18, .pan = 0 },
    .{ .wave = vex.TONE_PULSE, .duty = vex.TONE_MODE1, .attack = 0, .decay = 1, .sustain = 0, .release = 3, .volume = 48, .pan = vex.TONE_PAN_LEFT, .fm = 1, .fm_ratio = 3, .fm_index = 128 },
    .{ .wave = vex.TONE_PULSE, .duty = vex.TONE_MODE1, .attack = 0, .decay = 0, .sustain = 2, .release = 2, .volume = 28, .pan = vex.TONE_PAN_RIGHT, .fm = 1, .fm_ratio = 2, .fm_index = 96 },
    .{ .wave = vex.TONE_PULSE, .duty = vex.TONE_MODE2, .attack = 0, .decay = 1, .sustain = 0, .release = 4, .volume = 72, .pan = 0, .pwm = 1, .pwm_start = 32, .pwm_end = 32 },
};

const ROWS: u8 = 16;
const SPD: u8 = 6;
const R = mus.Event{ .note = mus.REST, .inst = 0, .vol = 0 };
const O = mus.Event{ .note = mus.OFF, .inst = 0, .vol = 0 };

fn E(n: u8, i: u8) mus.Event {
    return .{ .note = n, .inst = i, .vol = 0 };
}

fn V(n: u8, i: u8, v: u8) mus.Event {
    return .{ .note = n, .inst = i, .vol = v };
}

const EV0 = [_]mus.Event{
    V(134, 3, 44), V(69, 2, 56), V(45, 1, 80), V(45, 4, 32),
    R,             R,            R,            E(110, 4),
    V(134, 3, 44), E(72, 2),     V(52, 1, 62), V(110, 4, 14),
    R,             R,            R,            V(45, 4, 26),
    V(132, 3, 44), V(76, 2, 56), V(48, 1, 78), V(112, 4, 26),
    R,             R,            R,            E(110, 4),
    V(132, 3, 44), E(79, 2),     R,            V(110, 4, 20),
    R,             O,            R,            V(45, 4, 26),
    V(139, 3, 44), V(81, 2, 56), V(52, 1, 76), V(45, 4, 32),
    R,             R,            R,            E(110, 4),
    V(139, 3, 44), E(84, 2),     R,            V(110, 4, 18),
    R,             R,            R,            V(45, 4, 26),
    V(136, 3, 44), V(83, 2, 56), V(55, 1, 78), V(112, 4, 26),
    R,             R,            R,            E(110, 4),
    V(136, 3, 44), V(81, 2, 56), R,            V(112, 4, 24),
    V(134, 3, 44), O,            O,            V(45, 4, 26),
};

const EV1 = [_]mus.Event{
    V(132, 3, 44), V(77, 2, 56), V(41, 1, 80), V(45, 4, 32),
    R,             R,            R,            E(110, 4),
    V(132, 3, 44), E(72, 2),     V(48, 1, 62), V(110, 4, 14),
    R,             R,            R,            V(45, 4, 26),
    V(134, 3, 44), V(76, 2, 56), V(45, 1, 78), V(112, 4, 26),
    R,             R,            R,            E(110, 4),
    V(134, 3, 44), V(74, 2, 56), R,            V(110, 4, 20),
    R,             O,            R,            V(45, 4, 26),
    V(133, 3, 44), V(69, 2, 56), V(43, 1, 76), V(45, 4, 32),
    R,             R,            R,            E(110, 4),
    V(133, 3, 44), E(72, 2),     R,            V(110, 4, 16),
    R,             R,            R,            V(45, 4, 26),
    V(131, 3, 44), V(74, 2, 56), V(40, 1, 78), V(112, 4, 26),
    R,             R,            R,            E(110, 4),
    V(131, 3, 44), E(72, 2),     R,            V(112, 4, 24),
    V(132, 3, 44), O,            O,            V(45, 4, 26),
};

const EV2 = [_]mus.Event{
    V(131, 6, 42), V(76, 5, 52), V(40, 7, 80), V(45, 4, 32),
    R,             R,            R,            E(110, 4),
    V(131, 6, 42), E(79, 5),     V(47, 7, 62), V(110, 4, 20),
    R,             R,            R,            V(45, 4, 26),
    V(136, 6, 42), V(83, 5, 52), V(45, 7, 78), V(112, 4, 26),
    R,             R,            R,            E(110, 4),
    V(136, 6, 42), V(81, 5, 52), R,            V(110, 4, 18),
    R,             O,            R,            V(45, 4, 26),
    V(133, 6, 42), E(79, 5),     V(43, 7, 76), V(45, 4, 32),
    R,             R,            R,            E(110, 4),
    V(133, 6, 42), V(76, 5, 52), R,            V(110, 4, 20),
    R,             R,            R,            V(45, 4, 26),
    V(131, 6, 42), V(74, 5, 52), V(40, 7, 78), V(112, 4, 26),
    R,             R,            R,            E(110, 4),
    V(131, 6, 42), V(76, 5, 52), R,            V(112, 4, 24),
    V(131, 6, 42), O,            O,            V(45, 4, 32),
};

const EV3 = [_]mus.Event{
    V(134, 6, 42), V(81, 5, 52), V(45, 7, 80), R,
    R,             R,            R,            R,
    R,             E(84, 5),     V(52, 7, 62), R,
    R,             R,            R,            R,
    V(132, 6, 42), E(79, 5),     V(48, 7, 78), R,
    R,             R,            R,            R,
    R,             V(76, 5, 52), R,            R,
    R,             R,            R,            R,
    V(136, 6, 42), V(81, 5, 52), V(45, 7, 80), V(45, 4, 32),
    R,             R,            R,            E(110, 4),
    R,             E(84, 5),     R,            E(112, 4),
    R,             R,            R,            V(45, 4, 26),
    V(131, 6, 42), V(83, 5, 52), V(43, 7, 78), V(112, 4, 34),
    R,             R,            R,            V(110, 4, 20),
    R,             V(81, 5, 52), R,            V(112, 4, 38),
    V(136, 6, 42), O,            O,            V(45, 4, 44),
};

const EV4 = [_]mus.Event{
    V(134, 3, 40), V(69, 2, 56), V(45, 1, 80), R,
    R,             R,            R,            R,
    V(134, 3, 40), E(72, 2),     V(52, 1, 62), R,
    R,             R,            R,            R,
    V(132, 3, 40), V(76, 2, 56), V(41, 1, 78), R,
    R,             R,            R,            R,
    V(132, 3, 40), E(79, 2),     R,            R,
    R,             O,            R,            R,
    V(139, 3, 40), V(81, 2, 56), V(52, 1, 76), V(45, 4, 32),
    R,             R,            R,            E(110, 4),
    V(139, 3, 40), E(84, 2),     R,            E(108, 4),
    R,             R,            R,            V(45, 4, 26),
    V(136, 3, 40), V(83, 2, 56), V(55, 1, 78), V(112, 4, 26),
    R,             R,            R,            E(110, 4),
    V(136, 3, 40), V(81, 2, 56), R,            E(108, 4),
    V(134, 3, 40), O,            O,            V(45, 4, 32),
};

const PAT0 = mus.Pat{ .rows = ROWS, .speed = SPD, .events = &EV0 };
const PAT1 = mus.Pat{ .rows = ROWS, .speed = SPD, .events = &EV1 };
const PAT2 = mus.Pat{ .rows = ROWS, .speed = SPD, .events = &EV2 };
const PAT3 = mus.Pat{ .rows = ROWS, .speed = SPD, .events = &EV3 };
const PAT4 = mus.Pat{ .rows = ROWS, .speed = SPD, .events = &EV4 };
const PATS = [_]*const mus.Pat{ &PAT0, &PAT1, &PAT2, &PAT3, &PAT4 };
const ORDERS = [_]u8{ 4, 0, 1, 2, 1, 0, 3, 0, 1 };
const SONG = mus.Song{
    .num_insts = @intCast(INSTS.len),
    .num_pats = @intCast(PATS.len),
    .num_orders = @intCast(ORDERS.len),
    .loop_ord = 0,
    .insts = &INSTS,
    .pats = &PATS,
    .orders = &ORDERS,
};

const names = [_][*:0]const u8{ "A", "B", "C", "D", "E" };
const labels = [_][*:0]const u8{ "A / MAIN", "B / LEAD", "C / BRIDGE", "D / OUTRO", "E / INTRO" };
const channel_labels = [_][*:0]const u8{ "ARP", "LEAD", "BASS", "DRUM" };
var channel_muted = [_]bool{ false, false, false, false };
var mouse_down = false;

export fn boot() void {
    vex.pal(0, 0x140C00);
    vex.pal(1, 0x690804);
    vex.pal(2, 0xDE2C2C);
    vex.pal(3, 0xFA5555);
    vex.pal(4, 0x382400);
    vex.pal(5, 0xA1858D);
    vex.pal(6, 0xD0B2BA);
    vex.pal(7, 0xFACACA);
    vex.pal(8, 0x002000);
    vex.pal(9, 0x405544);
    vex.pal(10, 0x617561);
    vex.pal(11, 0x99B295);
    vex.pal(12, 0x0C3044);
    vex.pal(13, 0x556D89);
    vex.pal(14, 0x7595B6);
    vex.pal(15, 0xDEEEFF);
    mus.load(&SONG);
    mus.play();
    vex.title("vex - test_music (cracktro)");
}

export fn update() void {
    const mouse_down_now = vex.mbtn(vex.MOUSE_LEFT) != 0;
    if (mouse_down_now and !mouse_down) {
        const mouse_x = vex.mx();
        const mouse_y = vex.my();
        for (0..mus.CHANNELS) |ch| {
            const x: i32 = 4 + @as(i32, @intCast(ch)) * 38;
            if (mouse_x >= x and mouse_x < x + 34 and mouse_y >= 60 and mouse_y < 86) {
                channel_muted[ch] = !channel_muted[ch];
                mus.mute(ch, channel_muted[ch]);
            }
        }
    }
    mouse_down = mouse_down_now;
    mus.tick();

    const pos = mus.pos();
    const order: usize = @intCast(pos & 0xFF);
    const pattern: usize = @intCast(ORDERS[order]);
    const row: usize = @intCast((pos >> 8) & 0xFF);

    vex.cls(8);
    vex.rectb(2, 2, vex.WIDTH - 4, vex.HEIGHT - 4, 10);
    vex.text("VEX // CRACKTRO", 6, 6, 15);
    vex.rect(vex.WIDTH - 26, 4, 22, 11, 9);
    vex.rectb(vex.WIDTH - 26, 4, 22, 11, 11);
    vex.text(names[pattern], vex.WIDTH - 17, 6, 15);

    vex.rect(4, 22, vex.WIDTH - 8, 1, 10);
    vex.text("SEQUENCE", 4, 27, 10);
    vex.rect(4, 36, vex.WIDTH - 8, 6, 9);
    vex.rect(4, 36, @divFloor((vex.WIDTH - 8) * @as(i32, @intCast(row % ROWS)), ROWS), 6, 11);

    vex.text("CHANNELS", 4, 50, 10);
    const base = PATS[pattern].events[row * mus.CHANNELS ..][0..mus.CHANNELS];
    for (0..mus.CHANNELS) |ch| {
        const x: i32 = 4 + @as(i32, @intCast(ch)) * 38;
        var c: i32 = if (channel_muted[ch]) 9 else if (base[ch].note != mus.REST) 11 else 9;
        if (!channel_muted[ch] and base[ch].note == mus.OFF) c = 13;
        vex.rect(x, 60, 34, 26, 9);
        vex.rectb(x, 60, 34, 26, if (channel_muted[ch]) 9 else 10);
        const label_x: i32 = x + @as(i32, if (ch == 0) 5 else 1);
        vex.text(channel_labels[ch], label_x, 64, c);
        vex.rect(x + 4, 78, 26, 4, c);
    }

    vex.rect(4, 96, vex.WIDTH - 8, 24, 9);
    vex.rectb(4, 96, vex.WIDTH - 8, 24, 10);
    vex.text("SYNC LOCK", 8, 101, 11);
    vex.text(labels[pattern], 8, 110, 15);
    vex.text("150 BPM / 4CH / PWM", 4, 126, 10);
}
