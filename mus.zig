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
//! All notes are MIDI note numbers (vex handles pitch via TONE_NOTE_MODE);
//! there is no frequency math in this library.
//!
//! Note values on an Event:
//!   REST (0)      no note: let the previous one ring
//!   OFF (128)     note-off: apply the instrument's release envelope
//!                 (noise instruments hard-cut instead — noise tails hiss)
//!   1..127        MIDI note number
//!   129..141      chord code, arpeggiated across the row (see `chordNote`);
//!                 NOTE: minor codes only cover roots C3..F#3 (129..135),
//!                 major codes roots G3..B3 (136..141)
//!
//! Sustain semantics on `Inst.sustain`:
//!   0              default: pattern speed * 2 frames
//!   1..254         fixed length in frames
//!   255 (HOLD)     key-follow: rings until OFF / next note

const vex = @import("vex");

/// Number of mixer channels (same as vex.TONE_CHANNELS).
pub const CHANNELS = vex.TONE_CHANNELS;

/// Note values.
pub const REST = 0; // no note (let previous ring)
pub const OFF = 128; // note-off: release / silence the channel

/// Inst.sustain: hold until OFF (or next note on the channel).
pub const SUSTAIN_HOLD: u8 = 255;

/// An instrument preset (8 bytes). Maps to tone() parameters. ADSR.
pub const Inst = extern struct {
    wave: u8, // vex.TONE_PULSE, vex.TONE_NOISE, vex.TONE_TRI
    duty: u8, // vex.TONE_MODE0..3
    attack: u8, // attack  length in frames (0..255)
    decay: u8, // decay   length in frames
    sustain: u8 = 0, // sustain length in frames (see SUSTAIN_HOLD)
    release: u8, // release length in frames
    volume: u8, // default volume (0..100)
    pan: u8, // 0=center, vex.TONE_PAN_LEFT, vex.TONE_PAN_RIGHT
};

/// A note event (3 bytes, one per channel per row).
pub const Event = struct {
    note: u8, // REST, OFF, MIDI 1..127, or chord 129..141
    inst: u8, // instrument index 1..num_insts (0 = no note played)
    vol: u8, // 0 = use instrument volume; 1..100 = override
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

/// Per-channel playback memory, so OFF can apply a release tail and
/// chord channels can arpeggiate.
const Voice = struct {
    inst: u8 = 0, // last triggered instrument (1-indexed), 0 = none
    note: u8 = REST, // last triggered note / chord code
    vol: u8 = 0, // resolved volume at trigger time
    arp_step: u8 = 0, // arpeggio step counter
};
var _voice: [CHANNELS]Voice = @splat(.{});

/// Load a song (resets position to the start).
pub fn load(song: *const Song) void {
    _song = song;
    _on = false;
    _ord = 0;
    _row = 0;
    _tick = 0;
    _voice = @splat(.{});
}

/// Start playback.
pub fn play() void {
    _on = true;
    _ord = 0;
    _row = 0;
    _tick = 0;
    _voice = @splat(.{});
}

/// Current position: low 8 bits = order, bits 8..15 = row.
pub fn pos() i32 {
    return @as(i32, _ord) | (@as(i32, _row) << 8);
}

// -- internal helpers --------------------------------------------------------

/// Silence a channel: an all-zero duration ends whatever the voice plays.
fn silence(ch: usize) void {
    vex.silence(@intCast(ch));

    _voice[ch] = .{};
}

/// Issue a tone for channel `ch`: MIDI `note` with the given instrument,
/// using the instrument's own envelope. Pitch is exact — vex's
/// TONE_NOTE_MODE interprets the freq parameter as a MIDI note number.
fn playInst(ch: usize, inst: *const Inst, note: i32, vol: i32) void {
    const duration = (vex.ToneDuration{
        .sustain = inst.sustain,
        .release = inst.release,
        .decay = inst.decay,
        .attack = inst.attack,
    }).pack();

    const volume = (vex.ToneVolume{
        .level = vol,
        .peak = 0,
    }).pack();

    const flags = vex.toneFlags(
        @intCast(ch),
        inst.duty,
        inst.wave | inst.pan | vex.TONE_NOTE_MODE,
    );

    vex.tone(note, duration, volume, flags);
}

/// Issue a tone with an explicit sustain length (used for OFF tails).
fn playSustain(ch: usize, inst: *const Inst, note: i32, vol: i32, sus: i32) void {
    const duration = (vex.ToneDuration{
        .sustain = sus,
        .release = inst.release,
        .decay = inst.decay,
        .attack = inst.attack,
    }).pack();

    const volume = (vex.ToneVolume{
        .level = vol,
        .peak = 0,
    }).pack();

    const flags = vex.toneFlags(
        @intCast(ch),
        inst.duty,
        inst.wave | inst.pan | vex.TONE_NOTE_MODE,
    );

    vex.tone(note, duration, volume, flags);
}

/// Apply the release tail of the channel's current instrument, or hard-cut
/// if nothing is playing, the instrument is noise, or it has no release.
fn releaseVoice(ch: usize) void {
    const song = _song orelse return;
    const v = &_voice[ch];

    if (v.inst == 0 or v.inst > song.num_insts) {
        silence(ch);
        return;
    }

    const inst = &song.insts[v.inst - 1];

    if (inst.wave == vex.TONE_NOISE or inst.release == 0) {
        silence(ch);
        return;
    }

    // Re-issue with minimal sustain so the envelope falls through into
    // the release segment rather than restarting the gate.
    playSustain(ch, inst, v.note, v.vol, 1);
    v.inst = 0; // voice is finished; a second OFF hard-cuts
}

/// Chord code (129..141) -> the chord tone's MIDI note for arpeggio `step`.
/// 129..135: minor triad, root = (code - 129) + 48   (C3..F#3)
/// 136..141: major triad, root = (code - 136) + 51   (G3..B3)
fn chordNote(code: u8, step: u8) i32 {
    const is_major = code >= 136;

    const root: i32 = if (is_major)
        @as(i32, code - 136) + 51
    else
        @as(i32, code - 129) + 48;

    const third: i32 = if (is_major) 4 else 3;

    const interval = switch (step % 3) {
        0 => 0,
        1 => third,
        else => 7,
    };

    return root + interval;
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

    // Mid-row arpeggio retrigger: chord channels cycle their triad once
    // per row. Retriggering restarts the envelope, so arps want
    // attack=0 / decay=0 instruments with a long-ish sustain.
    const arp_frame: u8 = pat.speed / 3;
    if (arp_frame > 0 and _tick > 0 and _tick % arp_frame == 0) {
        for (0..CHANNELS) |ch| {
            const v = &_voice[ch];

            if (v.note < 129) continue; // not a chord channel
            if (v.inst == 0 or v.inst > song.num_insts) continue;

            const inst = &song.insts[v.inst - 1];

            v.arp_step +%= 1;

            playInst(ch, inst, chordNote(v.note, v.arp_step), v.vol);
        }
    }

    // Trigger notes only on the first frame of a row.
    if (_tick == 0) {
        if (_row >= pat.rows) {
            stop();
            return;
        }

        for (0..CHANNELS) |ch| {
            const ev = &pat.events[@as(usize, _row) * CHANNELS + ch];
            const v = &_voice[ch];

            // note-off: release tail (or hard cut for noise / no tail)
            if (ev.note == OFF) {
                releaseVoice(ch);
                continue;
            }

            // rest or no instrument: nothing to trigger
            if (ev.note == REST or ev.inst == 0) continue;

            // resolve instrument (1-indexed), bounds-checked
            if (ev.inst > song.num_insts) continue;
            const inst = &song.insts[ev.inst - 1];

            // volume: instrument default, overridden by per-note vol if set
            var vol: i32 = if (ev.vol > 0) ev.vol else inst.volume;
            if (vol > 100) vol = 100;

            // resolve note (plain MIDI note or chord root)
            const note: i32 = if (ev.note >= 129)
                chordNote(ev.note, 0)
            else
                @as(i32, ev.note);

            playInst(ch, inst, note, vol);

            // remember voice state for OFF tails and arps
            v.inst = ev.inst;
            v.note = ev.note;
            v.vol = @intCast(vol);
            v.arp_step = 0;
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
