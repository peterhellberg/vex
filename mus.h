// mus.h - chiptune tracker library for vex carts.
//
// Include this after vex.h (or it includes vex.h for you).  Define your
// instruments, patterns, and song, then call mus_tick() from update().
//
//   #include "mus.h"
//
//   static const MusInst insts[] = { ... };
//   static const MusNote events[] = { ... };
//   static const MusPat pat = { rows, speed, events };
//   static const MusPat *const pats[] = { &pat };
//   static const uint8_t orders[] = { 0 };
//   static const MusSong song = { ... };
//
//   void boot(void) { mus_load(&song); mus_play(); }
//   void update(void) { mus_tick(); }
//
// All state lives in WASM linear memory; no host changes needed.
// Each mus_tick() call processes one frame of sequencer time.
//
// Notes ring past their row: MUS_REST does not cut off a previous note,
// it only declines to trigger a new one.  Use MUS_OFF to silence a channel.
// This header must be included from a single translation unit (all
// definitions are static), which matches the one-file cart model.
#ifndef MUS_H
#define MUS_H

#include "vex.h"

// Number of mixer channels (same as tone() channels).
#define MUS_CHANNELS VEX_TONE_CHANNELS

// Note values.
#define MUS_REST 0     // no note (let previous ring)
#define MUS_OFF  128   // note-off: silence the channel

// An instrument preset (8 bytes).  Maps to tone() parameters.
typedef struct {
    unsigned char wave;    // waveform: VEX_TONE_PULSE, VEX_TONE_NOISE, VEX_TONE_TRI
    unsigned char duty;    // pulse duty: VEX_TONE_MODE0..3
    unsigned char attack;  // attack  length in frames (0..255)
    unsigned char decay;   // decay   length in frames
    unsigned char sustain; // RESERVED: tone() has no per-phase sustain volume;
                           // kept for data compatibility, currently unused
    unsigned char release; // release length in frames
    unsigned char volume;  // default volume (0..100)
    unsigned char pan;     // 0=center, VEX_TONE_PAN_LEFT, VEX_TONE_PAN_RIGHT
} MusInst;

// A note event (4 bytes, one per channel per row).
typedef struct {
    unsigned char note;    // MUS_REST, MUS_OFF, or MIDI note 1..127
    unsigned char inst;    // instrument index 1..num_insts (0 = no note)
    unsigned char vol;     // 0 = use instrument volume; 1..100 = override
    unsigned char fx;      // reserved, must be 0
} MusNote;

// A pattern: `rows` rows, each with MUS_CHANNELS note events.
typedef struct {
    unsigned char rows;          // row count (1..255)
    unsigned char speed;         // frames per row (controls tempo)
    const MusNote *events;       // rows * MUS_CHANNELS events
} MusPat;

// A complete song.
typedef struct {
    unsigned char num_insts;           // instrument count (1..16)
    unsigned char num_pats;            // pattern count
    unsigned char num_orders;          // order list length
    unsigned char loop;                // order to loop to (0xFF = play once)
    const MusInst *insts;              // instrument array
    const MusPat *const *pats;         // array of pattern pointers
    const unsigned char *orders;       // order list: pattern indices
} MusSong;

// ---- API -------------------------------------------------------------------

// Load a song (resets position to the start).
void mus_load(const MusSong *song);

// Start playback.
void mus_play(void);

// Stop playback and silence all channels.
void mus_stop(void);

// Advance the sequencer by one frame.  Call from update().
void mus_tick(void);

// Current position: low 8 bits = order, bits 8..15 = row.
int mus_pos(void);

// ---- implementation --------------------------------------------------------

static const MusSong *_mus_song;
static int _mus_on, _mus_ord, _mus_row, _mus_tick;

// Silence a channel: zero-volume, zero-envelope tone.
static void _mus_silence(int ch) {
    tone(440, 0, 0, VEX_TONE_FLAGS(ch, 0, 0));
}

// MIDI note number -> frequency in Hz.  Equal temperament, A4=440 Hz.
// hz = 440 * 2^(semi/12) / 2^octaves, with the semitone ratio carried as a
// fixed-point multiplier (1024 = 1.0), exact to within 1 Hz over the range.
static int _mus_note_hz(int note) {
    // 2^(k/12) * 1024 for k = 0..11 (rounded to nearest)
    static const int RATIO[12] = {
        1024, 1085, 1150, 1218, 1291, 1367,
        1448, 1533, 1624, 1721, 1822, 1930
    };
    if (note < 12) note = 12;       // clamp: C0
    if (note > 119) note = 119;     // clamp: B8

    int semi = (note - 69) % 12;
    if (semi < 0) semi += 12;
    int oct = (note - 69 - semi) / 12;   // octaves below/above A4

    long hz = 440L * RATIO[semi];
    if (oct >= 0)
        return (int)((hz >> 10) << oct);
    return (int)(hz >> (10 - oct));
}

void mus_load(const MusSong *song) {
    _mus_song = song;
    _mus_on = 0;
    _mus_ord = 0;
    _mus_row = 0;
    _mus_tick = 0;
}

void mus_play(void) {
    _mus_on = 1;
    _mus_ord = 0;
    _mus_row = 0;
    _mus_tick = 0;
}

void mus_stop(void) {
    _mus_on = 0;
    for (int ch = 0; ch < MUS_CHANNELS; ch++)
        _mus_silence(ch);
}

int mus_pos(void) {
    return _mus_ord | (_mus_row << 8);
}

void mus_tick(void) {
    if (!_mus_song || !_mus_on) return;

    // resolve current order -> pattern, with bounds checks
    if (_mus_ord >= _mus_song->num_orders) { mus_stop(); return; }
    unsigned char pat_i = _mus_song->orders[_mus_ord];
    if (pat_i >= _mus_song->num_pats) { mus_stop(); return; }
    const MusPat *pat = _mus_song->pats[pat_i];

    // Trigger notes only on the first frame of a row.
    if (_mus_tick == 0) {
        if (_mus_row >= pat->rows) { mus_stop(); return; }
        for (int ch = 0; ch < MUS_CHANNELS; ch++) {
            const MusNote *ev = &pat->events[_mus_row * MUS_CHANNELS + ch];

            // note-off: silence the channel
            if (ev->note == MUS_OFF) {
                _mus_silence(ch);
                continue;
            }

            // rest or no instrument: nothing to trigger
            if (ev->note == MUS_REST || ev->inst == 0) continue;

            // resolve instrument (1-indexed), bounds-checked
            if (ev->inst > _mus_song->num_insts) continue;
            const MusInst *inst = &_mus_song->insts[ev->inst - 1];

            // flags: channel, duty, waveform + pan
            int flags = VEX_TONE_FLAGS(ch, inst->duty,
                inst->wave | inst->pan);

            // volume: instrument default, overridden by per-note vol if set
            int vol = ev->vol > 0 ? ev->vol : inst->volume;
            if (vol > 100) vol = 100;
            vol = VEX_TONE_VOLUME(vol, 0);

            // envelope: hold at full volume for two rows' worth of frames,
            // then the instrument's release tail
            int dur = VEX_TONE_DURATION(
                pat->speed * 2,
                inst->release,
                inst->decay,
                inst->attack);

            // play the note as an explicit Hz frequency
            tone(_mus_note_hz(ev->note), dur, vol, flags);
        }
    }

    // advance tick counter
    _mus_tick++;
    if (_mus_tick < pat->speed) return;
    _mus_tick = 0;
    _mus_row++;

    // advance to next row / pattern / order
    if (_mus_row >= pat->rows) {
        _mus_row = 0;
        _mus_ord++;
        if (_mus_ord >= _mus_song->num_orders) {
            if (_mus_song->loop >= _mus_song->num_orders) {
                mus_stop();
                return;
            }
            _mus_ord = _mus_song->loop;
        }
    }
}

#endif // MUS_H
