pub const TONE_CHANNELS = 4;
pub const TONE_PULSE: i32 = 0;
pub const TONE_NOISE: i32 = 1 << 6;
pub const TONE_TRI: i32 = 2 << 6;
pub const TONE_MODE0: i32 = 0;
pub const TONE_MODE1: i32 = 1 << 2;
pub const TONE_MODE2: i32 = 2 << 2;
pub const TONE_MODE3: i32 = 3 << 2;
pub const TONE_PAN_LEFT: i32 = 1 << 4;
pub const TONE_PAN_RIGHT: i32 = 2 << 4;
pub const TONE_NOTE_MODE: i32 = 1 << 8;
pub const TONE_HOLD: i32 = 1 << 9;
pub const TONE_RELEASE: i32 = 1 << 10;
pub const TONE_PWM: i32 = 1 << 11;
pub const TONE_FM: i32 = 1 << 30;

pub const ToneDuration = struct {
    attack: i32 = 0,
    decay: i32 = 0,
    sustain: i32 = 0,
    release: i32 = 0,

    pub fn pack(value: ToneDuration) i32 {
        return toneByte(value.sustain) |
            (toneByte(value.release) << 8) |
            (toneByte(value.decay) << 16) |
            (toneByte(value.attack) << 24);
    }
};

pub const ToneVolume = struct {
    level: i32,
    peak: i32 = 0,

    pub fn pack(value: ToneVolume) i32 {
        return toneByte(value.level) | (toneByte(value.peak) << 8);
    }
};

pub fn tonePulseWidth(start: i32, end: i32) i32 {
    return TONE_PWM | (toneByte(start) << 12) | (toneByte(end) << 20);
}

pub fn toneFmParams(ratio: i32, index: i32) i32 {
    const value: u32 = (@as(u32, @intCast(ratio & 0xFF)) << 16) |
        (@as(u32, @intCast(index & 0xFF)) << 24);
    return @bitCast(value);
}

pub fn toneFlags(channel: i32, mode: i32, extra: i32) i32 {
    return (channel & 3) | (mode & (3 << 2)) | (extra & ~@as(i32, 3 | (3 << 2)));
}

pub const Call = struct {
    freq: i32,
    duration: i32,
    volume: i32,
    flags: i32,
};

pub var calls: [64]Call = undefined;
pub var call_count: usize = 0;

pub fn reset() void {
    call_count = 0;
}

pub fn tone(freq: i32, duration: i32, volume: i32, flags: i32) void {
    if (call_count < calls.len) {
        calls[call_count] = .{
            .freq = freq,
            .duration = duration,
            .volume = volume,
            .flags = flags,
        };
    }
    call_count += 1;
}

pub fn silence(channel: i32) void {
    tone(440, 0, 0, toneFlags(channel, 0, 0));
}

fn toneByte(value: i32) i32 {
    return if (value < 0) 0 else if (value > 255) 255 else value;
}
