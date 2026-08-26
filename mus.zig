//! Chiptune tracker library for vex carts.
//!
//! Import alongside vex and call `load`, `play`, and `tick`:
//!
//! ```zig
//! const vex = @import("vex");
//! const mus = @import("mus");
//!
//! export fn boot() void {
//!     mus.load(&song);
//!     mus.play();
//! }
//!
//! export fn update() void {
//!     mus.tick();
//! }
//! ```
//!
//! All state lives in WASM linear memory; no host changes needed.

const std = @import("std");
const vex = @import("vex");

/// Number of mixer channels (same as vex.TONE_CHANNELS).
pub const CHANNELS = vex.TONE_CHANNELS;

/// Note values.
pub const REST = 0; // no note (let previous ring or stay silent)
pub const OFF = 128; // note-off: silence the channel

/// An instrument preset (8 bytes). Maps to tone() parameters.
pub const Inst = extern struct {
    wave: u8, // vex.TONE_PULSE, vex.TONE_NOISE, vex.TONE_TRI
    duty: u8, // vex.TONE_MODE0..3
    attack: u8, // attack  length in frames (0..255)
    decay: u8, // decay   length in frames
    sustain: u8, // sustain volume (0..100)
    release: u8, // release length in frames
    volume: u8, // default volume (0..100)
    pan: u8, // 0=center, vex.TONE_PAN_LEFT, vex.TONE_PAN_RIGHT
};

/// A note event (4 bytes, one per channel per row).
pub const Event = extern struct {
    note: u8, // REST, OFF, or MIDI note 1..127
    inst: u8, // instrument index 1..16 (0 = no note played)
    vol: u8, // 0 = use instrument volume; 1..100 = override
    fx: u8, // reserved, must be 0
};

/// A pattern.
pub const Pat = extern struct {
    rows: u8, // row count (1..255)
    speed: u8, // frames per row (controls tempo)
    events: [*]const Event, // rows * CHANNELS events
};

/// A complete song.
pub const Song = extern struct {
    num_insts: u8, // instrument count (1..16)
    num_pats: u8, // pattern count
    num_orders: u8, // order list length
    loop_ord: u8, // order to loop to (0xFF = play once)
    insts: [*]const Inst, // instrument array
    pats: [*]const *const Pat, // array of pattern pointers
    orders: [*]const u8, // order list: pattern indices
};

// -- state -------------------------------------------------------------------

var _song: ?*const Song = null;
var _on: bool = false;
var _ord: u8 = 0;
var _row: u8 = 0;
var _tick: u8 = 0;

/// Load a song (resets position to the start).
pub fn load(song: *const Song) void {
    _song = song;
    _on = false;
    _ord = 0;
    _row = 0;
    _tick = 0;
}

/// Start playback.
pub fn play() void {
    _on = true;
    _ord = 0;
    _row = 0;
    _tick = 0;
}

/// Stop playback and silence all channels.
pub fn stop() void {
    _on = false;
    inline for (0..CHANNELS) |ch| {
        vex.tone(440, 0, 0, vex.toneFlags(@intCast(ch), 0, 0));
    }
}

/// Current position: low 8 bits = order, bits 8..15 = row.
pub fn pos() i32 {
    return @as(i32, _ord) | (@as(i32, _row) << 8);
}

/// Advance the sequencer by one frame. Call from update().
pub fn tick() void {
    const song = _song orelse return;
    if (!_on) return;

    const pat = song.pats[song.orders[_ord]];

    // Trigger notes for this tick on all channels.
    inline for (0..CHANNELS) |ch| {
        const ev = &pat.events[@as(usize, _row) * CHANNELS + ch];

        // note-off: silence the channel
        if (ev.note == OFF) {
            vex.tone(440, 0, 0, vex.toneFlags(@intCast(ch), 0, 0));
            continue;
        }

        // rest or no instrument: nothing to trigger
        if (ev.note == REST or ev.inst == 0) continue;

        // resolve instrument (1-indexed)
        const inst = &song.insts[ev.inst - 1];

        // flags: waveform, duty, pan, note mode
        const flags = vex.toneFlags(
            @intCast(ch),
            inst.duty,
            inst.wave | inst.pan | vex.TONE_NOTE_MODE,
        );

        // volume: instrument default, overridden by per-note vol if set
        const vol: i32 = if (ev.vol > 0) ev.vol else inst.volume;
        const packed_vol = (ToneVolume{ .level = vol, .peak = 0 }).pack();

        // duration: instrument ADSR (sustain = one row's worth of frames)
        const dur = (ToneDuration{
            .sustain = pat.speed,
            .release = inst.release,
            .decay = inst.decay,
            .attack = inst.attack,
        }).pack();

        // play the note (MIDI note number via note mode)
        vex.tone(vex.TONE_NOTE_MODE | @as(i32, ev.note), dur, packed_vol, flags);
    }

    // advance tick counter
    _tick += 1;
    if (_tick < pat.speed) return;
    _tick = 0;
    _row += 1;

    // advance to next row / pattern / order
    if (_row >= pat.rows) {
        _row = 0;
        _ord += 1;
        if (_ord >= song.num_orders) {
            if (song.loop_ord >= song.num_orders) {
                stop();
                return;
            }
            _ord = song.loop_ord;
        }
    }
}

// -- internal helpers (match vex.h packed layouts) ---------------------------

const ToneDuration = struct {
    sustain: i32 = 0,
    release: i32 = 0,
    decay: i32 = 0,
    attack: i32 = 0,

    fn pack(d: ToneDuration) i32 {
        return toneByte(d.sustain) |
            (toneByte(d.release) << 8) |
            (toneByte(d.decay) << 16) |
            (toneByte(d.attack) << 24);
    }
};

const ToneVolume = struct {
    level: i32,
    peak: i32 = 0,

    fn pack(v: ToneVolume) i32 {
        return toneByte(v.level) | (toneByte(v.peak) << 8);
    }
};

fn toneByte(v: i32) i32 {
    return if (v < 0) 0 else if (v > 255) 255 else v;
}
