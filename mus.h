// mus.h - chiptune tracker library for vex carts.
//
// Include this after vex.h (or it includes vex.h for you).  Define your
// instruments, patterns, and song, then call mus_tick() from update().
//
//   #include "mus.h"
//
//   static const MusInst insts[] = { ... };
//   static const MusEvent events[] = { ... };
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
#define MUS_OFF  128   // note-off: release the channel (noise hard-cuts)
#define MUS_CHORD_MIN 129
#define MUS_CHORD_MAX 141
#define MUS_SUSTAIN_HOLD 255 // sustain indefinitely until OFF or a new note

// An instrument preset. Maps to tone() parameters. ADSR.
typedef struct {
    unsigned char wave;    // waveform: VEX_TONE_PULSE, VEX_TONE_NOISE, VEX_TONE_TRI
    unsigned char duty;    // pulse duty: VEX_TONE_MODE0..3
    unsigned char attack;  // attack  length in frames (0..255)
    unsigned char decay;   // decay   length in frames
    unsigned char sustain; // 0: pattern speed*2; 1..254: frames; 255: hold
    unsigned char release; // release length in frames
    unsigned char volume;  // default volume (0..100)
    unsigned char pan;     // 0=center, VEX_TONE_PAN_LEFT, VEX_TONE_PAN_RIGHT
    unsigned char pwm;       // 0 disables PWM; otherwise start/end are widths
    unsigned char pwm_start; // 0..255 start width
    unsigned char pwm_end;   // 0..255 sweep target width
    unsigned char fm;        // 0 disables 2-op FM
    unsigned char fm_ratio;  // modulator/carrier frequency ratio
    unsigned char fm_index;  // modulation index 0..255
} MusInst;

// A note event (3 bytes, one per channel per row).
typedef struct {
    unsigned char note;    // MUS_REST, MUS_OFF, MIDI note 1..127, or chord MUS_CHORD_MIN..MUS_CHORD_MAX
    unsigned char inst;    // instrument index 1..num_insts (0 = no note)
    unsigned char vol;     // 0 = use instrument volume; 1..100 = override
} MusEvent;

// A pattern: `rows` rows, each with MUS_CHANNELS note events.
typedef struct {
    unsigned char rows;          // row count (1..255)
    unsigned char speed;         // frames per row (controls tempo)
    const MusEvent *events;      // rows * MUS_CHANNELS events
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

void mus_mute(int ch, int muted);

// ---- implementation --------------------------------------------------------

static const MusSong *_mus_song;
static int _mus_on, _mus_ord, _mus_row, _mus_tick;

typedef struct {
    unsigned char inst;
    unsigned char note;
    unsigned char vol;
} MusVoice;
static MusVoice _mus_voice[MUS_CHANNELS];
static unsigned char _mus_muted;

// Silence a channel: zero-volume, zero-envelope tone.
static void _mus_silence(int ch) {
    tone(440, 0, 0, VEX_TONE_FLAGS(ch, 0, 0));
    _mus_voice[ch] = (MusVoice){0};
}

static void _mus_clear(void) {
    for (int ch = 0; ch < MUS_CHANNELS; ch++) _mus_silence(ch);
}

static void _mus_release(int ch) {
    MusVoice *v = &_mus_voice[ch];
    if (!_mus_song || v->inst == 0 || v->inst > _mus_song->num_insts) {
        _mus_silence(ch);
        return;
    }
    const MusInst *inst = &_mus_song->insts[v->inst - 1];
    if (inst->wave == VEX_TONE_NOISE || inst->release == 0) {
        _mus_silence(ch);
        return;
    }
    tone(0, VEX_TONE_DURATION(0, 0, 0, inst->release), 0,
         VEX_TONE_FLAGS(ch, 0, VEX_TONE_RELEASE));
    _mus_voice[ch] = (MusVoice){0};
}

void mus_mute(int ch, int muted) {
    if (ch < 0 || ch >= MUS_CHANNELS) return;
    if (muted) {
        _mus_muted |= (unsigned char)(1u << ch);
        _mus_release(ch);
    } else {
        _mus_muted &= (unsigned char)~(1u << ch);
    }
}

// Chord code (129..141) -> the chord tone's MIDI note for arpeggio `step`.
// 129..135: C/D/E/F/G/A/B minor; 136..141: G/A/B/C/D/E major.
// Use plain MIDI events for other roots or qualities.
static int _mus_chord_note(int code, int step) {
    static const int roots[] = {48, 50, 52, 53, 55, 57, 59,
                                55, 57, 59, 60, 62, 64};
    if (code < MUS_CHORD_MIN || code > MUS_CHORD_MAX) return MUS_REST;

    int root = roots[code - MUS_CHORD_MIN];
    int third = code >= 136 ? 4 : 3;
    int interval = step % 3 == 0 ? 0 : step % 3 == 1 ? third : 7;
    return root + interval;
}

void mus_load(const MusSong *song) {
    _mus_clear();
    _mus_song = song;
    _mus_on = 0;
    _mus_ord = 0;
    _mus_row = 0;
    _mus_tick = 0;
}

void mus_play(void) {
    _mus_clear();
    _mus_on = 1;
    _mus_ord = 0;
    _mus_row = 0;
    _mus_tick = 0;
}

void mus_stop(void) {
    _mus_on = 0;
    _mus_clear();
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

    // This runs before row triggers; a row trigger replaces the voice below.
    int arp_step = pat->speed >= 3 ? _mus_tick * 3 / pat->speed : 0;
    int arp_prev = pat->speed >= 3 && _mus_tick > 0
                       ? (_mus_tick - 1) * 3 / pat->speed
                       : 0;
    if (arp_step > arp_prev) {
        for (int ch = 0; ch < MUS_CHANNELS; ch++) {
            if (_mus_muted & (1u << ch)) continue;
            MusVoice *v = &_mus_voice[ch];
            if (v->note < MUS_CHORD_MIN || v->note > MUS_CHORD_MAX ||
                v->inst == 0 || v->inst > _mus_song->num_insts)
                continue;
            const MusInst *inst = &_mus_song->insts[v->inst - 1];
            int pwm = inst->pwm
                          ? VEX_TONE_PULSE_WIDTH(inst->pwm_start, inst->pwm_end)
                          : 0;
            int fm = inst->fm ? VEX_TONE_FM : 0;
            int fm_payload = inst->fm
                                 ? VEX_TONE_FM_PARAMS(inst->fm_ratio, inst->fm_index)
                                 : 0;
            int mode = pwm ? 0 : inst->duty;
            int flags = VEX_TONE_FLAGS(ch, mode,
                inst->wave | inst->pan | VEX_TONE_NOTE_MODE | pwm | fm |
                (inst->sustain == MUS_SUSTAIN_HOLD ? VEX_TONE_HOLD : 0));
            int sus = inst->sustain ? inst->sustain : pat->speed * 2;
            tone(_mus_chord_note(v->note, arp_step),
                 VEX_TONE_DURATION(inst->attack, inst->decay,
                                  sus, inst->release),
                 VEX_TONE_VOLUME(v->vol, v->vol) | fm_payload, flags);
        }
    }

    // Trigger notes only on the first frame of a row.
    if (_mus_tick == 0) {
        if (_mus_row >= pat->rows) { mus_stop(); return; }
        for (int ch = 0; ch < MUS_CHANNELS; ch++) {
            if (_mus_muted & (1u << ch)) {
                _mus_voice[ch].note = MUS_REST;
                continue;
            }
            const MusEvent *ev = &pat->events[_mus_row * MUS_CHANNELS + ch];

            // note-off: release the current voice
            if (ev->note == MUS_OFF) {
                _mus_release(ch);
                continue;
            }

            // rest or no instrument: nothing to trigger
            if (ev->note == MUS_REST || ev->inst == 0) {
                _mus_voice[ch].note = MUS_REST;
                continue;
            }

            // resolve instrument (1-indexed), bounds-checked
            if (ev->inst > _mus_song->num_insts) {
                _mus_voice[ch].note = MUS_REST;
                continue;
            }
            const MusInst *inst = &_mus_song->insts[ev->inst - 1];
            int is_chord = ev->note >= MUS_CHORD_MIN && ev->note <= MUS_CHORD_MAX;
            if (ev->note > 127 && !is_chord) {
                _mus_voice[ch].note = MUS_REST;
                continue;
            }

            // flags: channel, duty, waveform + pan
            int pwm = inst->pwm
                          ? VEX_TONE_PULSE_WIDTH(inst->pwm_start, inst->pwm_end)
                          : 0;
            int fm = inst->fm ? VEX_TONE_FM : 0;
            int fm_payload = inst->fm
                                 ? VEX_TONE_FM_PARAMS(inst->fm_ratio, inst->fm_index)
                                 : 0;
            int mode = pwm ? 0 : inst->duty;
            int flags = VEX_TONE_FLAGS(ch, mode,
                inst->wave | inst->pan | VEX_TONE_NOTE_MODE | pwm | fm |
                (inst->sustain == MUS_SUSTAIN_HOLD ? VEX_TONE_HOLD : 0));

            // volume: instrument default, overridden by per-note vol if set.
            // Peak tracks level (like mus.zig) so the attack has no extra
            // punch; peak 0 would mean full-scale during the attack.
            int level = ev->vol > 0 ? ev->vol : inst->volume;
            if (level > 100) level = 100;
            if (level == 0) {
                _mus_silence(ch);
                continue;
            }
            int vol = VEX_TONE_VOLUME(level, level) | fm_payload;

            // envelope: ADSR — sustain defaults to two rows so notes
            // ring, but a non-zero inst sustain overrides for short hats
            int sus = inst->sustain ? inst->sustain : pat->speed * 2;
            int dur = VEX_TONE_DURATION(
                inst->attack,
                inst->decay,
                sus,
                inst->release);

            // play the note using host MIDI note mode
            int note = is_chord ? _mus_chord_note(ev->note, 0) : ev->note;
            tone(note, dur, vol, flags);
            _mus_voice[ch] = (MusVoice){ev->inst, ev->note, (unsigned char)level};
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
