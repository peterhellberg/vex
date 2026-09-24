const std = @import("std");
const mus = @import("mus");
const vex = @import("vex");

const instruments = [_]mus.Inst{
    .{
        .wave = vex.TONE_PULSE,
        .duty = vex.TONE_MODE0,
        .attack = 0,
        .decay = 0,
        .sustain = 0,
        .release = 1,
        .volume = 0,
        .pan = 0,
    },
    .{
        .wave = vex.TONE_PULSE,
        .duty = vex.TONE_MODE0,
        .attack = 0,
        .decay = 0,
        .sustain = 0,
        .release = 4,
        .volume = 60,
        .pan = 0,
        .pwm = 1,
        .pwm_start = 32,
        .pwm_end = 192,
    },
};

const events = [_]mus.Event{
    .{ .note = 60, .inst = 2, .vol = 0 },
    .{ .note = 129, .inst = 2, .vol = 0 },
    .{ .note = mus.REST, .inst = 0, .vol = 0 },
    .{ .note = mus.REST, .inst = 0, .vol = 0 },
    .{ .note = 60, .inst = 1, .vol = 0 },
    .{ .note = mus.REST, .inst = 0, .vol = 0 },
    .{ .note = mus.REST, .inst = 0, .vol = 0 },
    .{ .note = mus.REST, .inst = 0, .vol = 0 },
};

const pattern = mus.Pat{ .rows = 2, .speed = 7, .events = &events };
const patterns = [_]*const mus.Pat{&pattern};
const orders = [_]u8{0};
const song = mus.Song{
    .num_insts = instruments.len,
    .num_pats = patterns.len,
    .num_orders = orders.len,
    .loop_ord = 0xFF,
    .insts = &instruments,
    .pats = &patterns,
    .orders = &orders,
};

const fm_instruments = [_]mus.Inst{
    .{
        .wave = vex.TONE_PULSE,
        .duty = vex.TONE_MODE0,
        .attack = 0,
        .decay = 0,
        .sustain = 2,
        .release = 2,
        .volume = 50,
        .pan = 0,
        .fm = 1,
        .fm_ratio = 2,
        .fm_index = 128,
    },
};
const fm_events = [_]mus.Event{
    .{ .note = 139, .inst = 1, .vol = 0 },
    .{ .note = 142, .inst = 1, .vol = 0 },
    .{ .note = mus.REST, .inst = 0, .vol = 0 },
    .{ .note = mus.REST, .inst = 0, .vol = 0 },
};
const fm_pattern = mus.Pat{ .rows = 1, .speed = 7, .events = &fm_events };
const fm_patterns = [_]*const mus.Pat{&fm_pattern};
const fm_orders = [_]u8{0};
const fm_song = mus.Song{
    .num_insts = fm_instruments.len,
    .num_pats = fm_patterns.len,
    .num_orders = fm_orders.len,
    .loop_ord = 0xFF,
    .insts = &fm_instruments,
    .pats = &fm_patterns,
    .orders = &fm_orders,
};
const zero_speed_pattern = mus.Pat{ .rows = 1, .speed = 0, .events = &fm_events };
const zero_speed_patterns = [_]*const mus.Pat{&zero_speed_pattern};
const zero_speed_song = mus.Song{
    .num_insts = fm_instruments.len,
    .num_pats = zero_speed_patterns.len,
    .num_orders = fm_orders.len,
    .loop_ord = 0xFF,
    .insts = &fm_instruments,
    .pats = &zero_speed_patterns,
    .orders = &fm_orders,
};

test "pre-load mute is state-only" {
    mus.mute(0, false);
    vex.reset();
    mus.mute(0, true);
    try std.testing.expectEqual(@as(usize, 0), vex.call_count);

    mus.load(&song);
    mus.play();
    vex.reset();
    mus.tick();
    try std.testing.expectEqual(@as(usize, 1), vex.call_count);
    try std.testing.expectEqual(@as(i32, 1), vex.calls[0].flags & 3);
    mus.mute(0, false);
}

test "zero volume, arpeggio, and rest" {
    mus.load(&song);
    mus.play();
    vex.reset();

    for (0..14) |_| mus.tick();

    try std.testing.expect((mus.pos() & 0xFF) == 0);
    try std.testing.expectEqual(@as(usize, 9), vex.call_count);
    try std.testing.expectEqual(@as(i32, 60), vex.calls[0].freq);
    try std.testing.expectEqual(@as(i32, 0), vex.calls[4].duration);
    try std.testing.expectEqual(@as(i32, 0), vex.calls[4].volume);
    try std.testing.expectEqual(@as(i32, 0), vex.calls[4].flags & 3);

    const expected_notes = [_]i32{ 48, 51, 55 };
    for (expected_notes, 1..4) |note, index| {
        try std.testing.expectEqual(note, vex.calls[index].freq);
        try std.testing.expectEqual(@as(i32, 14), vex.calls[index].duration & 0xFF);
        try std.testing.expect(vex.calls[index].flags & vex.TONE_NOTE_MODE != 0);
        try std.testing.expect(vex.calls[index].flags & vex.TONE_PWM != 0);
        try std.testing.expectEqual(@as(i32, 32), (vex.calls[index].flags >> 12) & 255);
        try std.testing.expectEqual(@as(i32, 192), (vex.calls[index].flags >> 20) & 255);
    }

    for (vex.calls[5..9], 0..4) |call, channel| {
        try std.testing.expectEqual(@as(i32, 0), call.duration);
        try std.testing.expectEqual(@as(i32, 0), call.volume);
        try std.testing.expectEqual(@as(i32, @intCast(channel)), call.flags & 3);
    }
}

test "FM instrument" {
    mus.load(&fm_song);
    mus.play();
    vex.reset();
    mus.tick();
    try std.testing.expectEqual(@as(usize, 1), vex.call_count);
    try std.testing.expectEqual(@as(i32, 60), vex.calls[0].freq);
    try std.testing.expect(vex.calls[0].flags & vex.TONE_FM != 0);
    try std.testing.expectEqual(@as(i32, 2), (vex.calls[0].volume >> 16) & 255);
    try std.testing.expectEqual(@as(i32, 128), (vex.calls[0].volume >> 24) & 255);
}

test "zero speed stops safely" {
    mus.load(&zero_speed_song);
    mus.play();
    vex.reset();
    mus.tick();
    try std.testing.expectEqual(@as(usize, 4), vex.call_count);
}

test "release ignores frequency" {
    mus.load(&fm_song);
    mus.play();
    vex.reset();
    mus.tick();
    mus.mute(0, true);
    try std.testing.expectEqual(@as(usize, 2), vex.call_count);
    try std.testing.expectEqual(@as(i32, 0), vex.calls[1].freq);
    try std.testing.expect(vex.calls[1].flags & vex.TONE_RELEASE != 0);
    mus.mute(0, false);
}

test "repeated mute preserves active release" {
    mus.mute(0, false);
    mus.load(&song);
    mus.play();
    vex.reset();

    mus.tick();
    try std.testing.expectEqual(@as(usize, 2), vex.call_count);
    mus.mute(0, true);
    mus.mute(0, true);
    try std.testing.expectEqual(@as(usize, 3), vex.call_count);
    try std.testing.expectEqual(@as(i32, 0), vex.calls[2].freq);
    try std.testing.expect(vex.calls[2].flags & vex.TONE_RELEASE != 0);
    mus.mute(0, false);
}

test "mute" {
    mus.mute(0, false);
    mus.load(&song);
    mus.play();
    vex.reset();

    mus.mute(0, true);
    try std.testing.expectEqual(@as(usize, 1), vex.call_count);
    mus.tick();
    try std.testing.expectEqual(@as(usize, 2), vex.call_count);
    try std.testing.expectEqual(@as(i32, 1), vex.calls[1].flags & 3);

    mus.mute(0, false);
    mus.load(&song);
    mus.play();
    vex.reset();
    mus.tick();
    try std.testing.expectEqual(@as(usize, 2), vex.call_count);
    try std.testing.expectEqual(@as(i32, 0), vex.calls[0].flags & 3);
    try std.testing.expectEqual(@as(i32, 1), vex.calls[1].flags & 3);
}
