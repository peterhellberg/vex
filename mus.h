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
#ifndef MUS_H
#define MUS_H

#include "vex.h"

// Number of mixer channels (same as tone() channels).
#define MUS_CHANNELS VEX_TONE_CHANNELS

// Note values.
#define MUS_REST 0     // no note (let previous ring or stay silent)
#define MUS_OFF  128   // note-off: silence the channel

// An instrument preset (8 bytes).  Maps to tone() parameters.
typedef struct {
    unsigned char wave;    // waveform: VEX_TONE_PULSE, VEX_TONE_NOISE, VEX_TONE_TRI
    unsigned char duty;    // pulse duty: VEX_TONE_MODE0..3
    unsigned char attack;  // attack  length in frames (0..255)
    unsigned char decay;   // decay   length in frames
    unsigned char sustain; // sustain volume (0..100)
    unsigned char release; // release length in frames
    unsigned char volume;  // default volume (0..100)
    unsigned char pan;     // 0=center, VEX_TONE_PAN_LEFT, VEX_TONE_PAN_RIGHT
} MusInst;

// A note event (4 bytes, one per channel per row).
typedef struct {
    unsigned char note;    // MUS_REST, MUS_OFF, or MIDI note 1..127
    unsigned char inst;    // instrument index 1..16 (0 = no note played)
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
        tone(440, 0, 0, VEX_TONE_FLAGS(ch, 0, 0));
}

int mus_pos(void) {
    return _mus_ord | (_mus_row << 8);
}

void mus_tick(void) {
    if (!_mus_song || !_mus_on) return;

    const MusPat *pat = _mus_song->pats[_mus_song->orders[_mus_ord]];

    // Trigger notes for this tick on all channels.
    for (int ch = 0; ch < MUS_CHANNELS; ch++) {
        const MusNote *ev = &pat->events[_mus_row * MUS_CHANNELS + ch];

        // note-off: silence the channel
        if (ev->note == MUS_OFF) {
            tone(440, 0, 0, VEX_TONE_FLAGS(ch, 0, 0));
            continue;
        }

        // rest or no instrument: nothing to trigger
        if (ev->note == MUS_REST || ev->inst == 0) continue;

        // resolve instrument (1-indexed)
        const MusInst *inst = &_mus_song->insts[ev->inst - 1];

        // flags: waveform, duty, pan, note mode
        int flags = VEX_TONE_FLAGS(ch, inst->duty,
            inst->wave | inst->pan | VEX_TONE_NOTE_MODE);

        // volume: instrument default, overridden by per-note vol if set
        int vol = ev->vol > 0 ? ev->vol : inst->volume;
        vol = VEX_TONE_VOLUME(vol, 0);

        // duration: instrument ADSR (sustain = one row's worth of frames)
        int dur = VEX_TONE_DURATION(
            pat->speed,
            inst->release,
            inst->decay,
            inst->attack);

        // play the note (MIDI note number via note mode)
        tone(VEX_TONE_NOTE_MODE | ev->note, dur, vol, flags);
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
