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
//!   255 (HOLD)     hold the sustain level until OFF / next note

const vex = @import("vex");

/// Number of mixer channels (same as vex.TONE_CHANNELS).
pub const CHANNELS = vex.TONE_CHANNELS;

/// Note values.
pub const REST = 0; // no note (let previous ring)
pub const OFF = 128; // note-off: release / silence the channel

/// Hold the sustain level until the channel is retriggered or silenced.
pub const SUSTAIN_HOLD: u8 = 255;

/// An instrument preset. Maps to tone() parameters. ADSR.
pub const Inst = extern struct {
    wave: u8, // vex.TONE_PULSE, vex.TONE_NOISE, vex.TONE_TRI
    duty: u8, // vex.TONE_MODE0..3
    attack: u8, // attack  length in frames (0..255)
    decay: u8, // decay   length in frames
    sustain: u8 = 0, // sustain length in frames (see SUSTAIN_HOLD)
    release: u8, // release length in frames
    volume: u8, // default volume (0..100)
    pan: u8, // 0=center, vex.TONE_PAN_LEFT, vex.TONE_PAN_RIGHT
    pwm: u8 = 0, // 0 disables PWM; otherwise start/end are widths
    pwm_start: u8 = 0, // 0..255 start width
    pwm_end: u8 = 0, // 0..255 sweep target width
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
};
var _voice: [CHANNELS]Voice = @splat(.{});

/// Load a song (resets position to the start).
pub fn load(song: *const Song) void {
    stop();
    _song = song;
    _ord = 0;
    _row = 0;
    _tick = 0;
}

/// Start playback.
pub fn play() void {
    stop();
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

/// Silence a channel: an all-zero duration ends whatever the voice plays.
fn silence(ch: usize) void {
    vex.silence(@intCast(ch));

    _voice[ch] = .{};
}

/// Issue a tone for channel `ch`: MIDI `note` with the given instrument,
/// using the resolved sustain length. Pitch is exact — vex's
/// TONE_NOTE_MODE interprets the freq parameter as a MIDI note number.
fn playInst(ch: usize, inst: *const Inst, note: i32, vol: i32, sustain: i32) void {
    const duration = (vex.ToneDuration{
        .sustain = sustain,
        .release = inst.release,
        .decay = inst.decay,
        .attack = inst.attack,
    }).pack();

    const volume = (vex.ToneVolume{
        .level = vol,
        .peak = vol,
    }).pack();

    const pwm = if (inst.pwm != 0)
        vex.tonePulseWidth(inst.pwm_start, inst.pwm_end)
    else
        0;
    const mode = if (pwm != 0) 0 else inst.duty;
    const flags = vex.toneFlags(
        @intCast(ch),
        mode,
        inst.wave | inst.pan | vex.TONE_NOTE_MODE | pwm |
            (if (inst.sustain == SUSTAIN_HOLD) vex.TONE_HOLD else 0),
    );

    vex.tone(note, duration, volume, flags);
}

/// Release a channel from its current envelope level.
fn releaseTone(ch: usize, release: i32) void {
    const duration = (vex.ToneDuration{
        .release = release,
    }).pack();
    const flags = vex.toneFlags(@intCast(ch), 0, vex.TONE_RELEASE);
    vex.tone(440, duration, 0, flags);
}

fn sustainFor(inst: *const Inst, speed: u8) i32 {
    return if (inst.sustain == 0) @as(i32, speed) * 2 else inst.sustain;
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

    releaseTone(ch, inst.release);
    v.* = .{};
}

/// Chord code (129..141) -> the chord tone's MIDI note for arpeggio `step`.
/// 129..135: minor triad, root = (code - 129) + 48   (C3..F#3)
/// 136..141: major triad, root = (code - 136) + 55   (G3..B3)
fn chordNote(code: u8, step: u8) i32 {
    const is_major = code >= 136;

    const root: i32 = if (is_major)
        @as(i32, code - 136) + 55
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

/// Stop playback and silence all channels.
pub fn stop() void {
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
    const arp_step: u8 = if (pat.speed >= 3)
        @intCast(@as(u16, _tick) * 3 / pat.speed)
    else
        0;
    const arp_prev: u8 = if (pat.speed >= 3 and _tick > 0)
        @intCast((@as(u16, _tick) - 1) * 3 / pat.speed)
    else
        0;
    if (arp_step > arp_prev) {
        for (0..CHANNELS) |ch| {
            const v = &_voice[ch];

            if (v.note < 129) continue; // not a chord channel
            if (v.inst == 0 or v.inst > song.num_insts) continue;

            const inst = &song.insts[v.inst - 1];

            playInst(ch, inst, chordNote(v.note, arp_step), v.vol, sustainFor(inst, pat.speed));
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
            if (ev.note == REST or ev.inst == 0) {
                v.note = REST;
                continue;
            }

            // resolve instrument (1-indexed), bounds-checked
            if (ev.inst > song.num_insts) {
                v.note = REST;
                continue;
            }
            const inst = &song.insts[ev.inst - 1];

            // volume: instrument default, overridden by per-note vol if set
            var vol: i32 = if (ev.vol > 0) ev.vol else inst.volume;
            if (vol > 100) vol = 100;
            if (vol == 0) {
                silence(ch);
                continue;
            }

            // resolve note (plain MIDI note or chord root)
            const note: i32 = if (ev.note >= 129)
                chordNote(ev.note, 0)
            else
                @as(i32, ev.note);

            playInst(ch, inst, note, vol, sustainFor(inst, pat.speed));

            // remember voice state for OFF tails and arps
            v.inst = ev.inst;
            v.note = ev.note;
            v.vol = @intCast(vol);
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
