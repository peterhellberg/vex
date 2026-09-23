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

test "zero volume, arpeggio, and rest" {
    mus.load(&song);
    mus.play();
    vex.reset();

    for (0..14) |_| mus.tick();

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
    }

    for (vex.calls[5..9], 0..4) |call, channel| {
        try std.testing.expectEqual(@as(i32, 0), call.duration);
        try std.testing.expectEqual(@as(i32, 0), call.volume);
        try std.testing.expectEqual(@as(i32, @intCast(channel)), call.flags & 3);
    }
}
