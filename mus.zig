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
//!
//! Notes ring past their row: `REST` does not cut off a previous note,
//! it only declines to trigger a new one.  Use `OFF` to silence a channel.

const std = @import("std");
const vex = @import("vex");

/// Number of mixer channels (same as vex.TONE_CHANNELS).
pub const CHANNELS = vex.TONE_CHANNELS;

/// Note values.
pub const REST = 0; // no note (let previous ring)
pub const OFF = 128; // note-off: silence the channel

/// An instrument preset (8 bytes). Maps to tone() parameters.
pub const Inst = extern struct {
    wave: u8, // vex.TONE_PULSE, vex.TONE_NOISE, vex.TONE_TRI
    duty: u8, // vex.TONE_MODE0..3
    attack: u8, // attack  length in frames (0..255)
    decay: u8, // decay   length in frames
    sustain: u8 = 0, // RESERVED: tone() has no per-phase sustain volume;
    // kept for data compatibility with mus.h, currently unused
    release: u8, // release length in frames
    volume: u8, // default volume (0..100)
    pan: u8, // 0=center, vex.TONE_PAN_LEFT, vex.TONE_PAN_RIGHT
};

/// A note event (4 bytes, one per channel per row).
pub const Event = extern struct {
    note: u8, // REST, OFF, or MIDI note 1..127
    inst: u8, // instrument index 1..num_insts (0 = no note played)
    vol: u8, // 0 = use instrument volume; 1..100 = override
    fx: u8 = 0, // reserved, must be 0
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

/// Current position: low 8 bits = order, bits 8..15 = row.
pub fn pos() i32 {
    return @as(i32, _ord) | (@as(i32, _row) << 8);
}

// -- internal helpers --------------------------------------------------------

/// Silence a channel: zero-volume, zero-envelope tone.
fn silence(ch: usize) void {
    vex.tone(440, 0, 0, vex.toneFlags(@intCast(ch), 0, 0));
}

/// MIDI note number -> frequency in Hz.  Equal temperament, A4=440 Hz.
/// Fixed-point semitone ratios (1024 = 1.0), exact to within 1 Hz over range.
/// Matches mus.h's `_mus_note_hz`.
fn noteHz(note_in: i32) i32 {
    // 2^(k/12) * 1024 for k = 0..11 (rounded to nearest)
    const RATIO = [12]i32{
        1024, 1085, 1150, 1218, 1291, 1367,
        1448, 1533, 1624, 1721, 1822, 1930,
    };
    var note = note_in;
    if (note < 12) note = 12; // clamp: C0
    if (note > 119) note = 119; // clamp: B8

    var semi = @rem(note - 69, 12);
    if (semi < 0) semi += 12;
    const oct = @divTrunc(note - 69 - semi, 12); // octaves below/above A4

    const hz: i64 = @as(i64, 440) * RATIO[@intCast(semi)];
    if (oct >= 0)
        return @intCast((hz >> 10) << @intCast(oct));
    return @intCast(hz >> @intCast(10 - oct));
}

fn stop() void {
    _on = false;
    inline for (0..CHANNELS) |ch| {
        silence(ch);
    }
}

/// Advance the sequencer by one frame. Call from update().
pub fn tick() void {
    const song = _song orelse return;
    if (!_on) return;

    // resolve current order -> pattern, with bounds checks
    if (_ord >= song.num_orders) {
        stop();
        return;
    }
    const pat_i = song.orders[_ord];
    if (pat_i >= song.num_pats) {
        stop();
        return;
    }
    const pat = song.pats[pat_i];

    // Trigger notes only on the first frame of a row.
    if (_tick == 0) {
        if (_row >= pat.rows) {
            stop();
            return;
        }
        inline for (0..CHANNELS) |ch| {
            const ev = &pat.events[@as(usize, _row) * CHANNELS + ch];

            // note-off: silence the channel
            if (ev.note == OFF) {
                silence(ch);
                continue;
            }

            // rest or no instrument: nothing to trigger
            if (ev.note == REST or ev.inst == 0) continue;

            // resolve instrument (1-indexed), bounds-checked
            if (ev.inst > song.num_insts) continue;
            const inst = &song.insts[ev.inst - 1];

            // flags: channel, duty, waveform + pan
            const flags = vex.toneFlags(
                @intCast(ch),
                inst.duty,
                inst.wave | inst.pan,
            );

            // volume: instrument default, overridden by per-note vol if set
            var vol: i32 = if (ev.vol > 0) ev.vol else inst.volume;
            if (vol > 100) vol = 100;
            const packed_vol = (ToneVolume{ .level = vol, .peak = 0 }).pack();

            // envelope: hold at full volume for two rows' worth of frames,
            // then the instrument's release tail
            const dur = (ToneDuration{
                .sustain = @as(i32, pat.speed) * 2,
                .release = inst.release,
                .decay = inst.decay,
                .attack = inst.attack,
            }).pack();

            // play the note as an explicit Hz frequency
            vex.tone(noteHz(ev.note), dur, packed_vol, flags);
        }
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
