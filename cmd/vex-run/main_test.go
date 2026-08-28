package main

import (
	"encoding/binary"
	"testing"
	"unsafe"
)

// newTestGame builds a Game without ebiten/audio initialization so drawing
// logic can be exercised headlessly.
func newTestGame() *Game {
	g := &Game{}
	g.pixels = make([]byte, VEX_W*VEX_H*4)
	g.frame = unsafe.Slice((*uint32)(unsafe.Pointer(&g.pixels[0])), VEX_W*VEX_H)
	g.palreset()
	return g
}

func colorAt(g *Game, x, y int32) uint32 {
	return g.frame[y*VEX_W+x]
}

func TestParse(t *testing.T) {
	t.Run("defaults", func(t *testing.T) {
		in, cart, err := parse([]string{"game.wasm"})
		if err != nil {
			t.Fatalf("unexpected error: %v", err)
		}
		if cart != "game.wasm" || in.scale != VEX_SCALE_DEF || in.watch {
			t.Fatalf("got %+v cart=%q", in, cart)
		}
	})

	t.Run("scale clamped to 1..20", func(t *testing.T) {
		in, _, err := parse([]string{"-s", "0", "x.wasm"})
		if err != nil || in.scale != 1 {
			t.Fatalf("low clamp: scale=%d err=%v", in.scale, err)
		}
		in, _, err = parse([]string{"--scale", "99", "x.wasm"})
		if err != nil || in.scale != VEX_SCALE_MAX {
			t.Fatalf("high clamp: scale=%d err=%v", in.scale, err)
		}
	})

	t.Run("watch flag", func(t *testing.T) {
		in, _, err := parse([]string{"-w", "x.wasm"})
		if err != nil || !in.watch {
			t.Fatalf("got %+v err=%v", in, err)
		}
	})

	t.Run("missing cart errors", func(t *testing.T) {
		if _, _, err := parse(nil); err == nil {
			t.Fatal("expected error for missing cart")
		}
	})

	t.Run("unknown flag errors", func(t *testing.T) {
		if _, _, err := parse([]string{"--bogus"}); err == nil {
			t.Fatal("expected error for unknown flag")
		}
	})
}

func TestRectRasterization(t *testing.T) {
	g := newTestGame()
	g.cls(1)
	g.rect(5, 5, 10, 10, 2)

	c2 := g.palette[2]
	if colorAt(g, 5, 5) != c2 || colorAt(g, 14, 14) != c2 {
		t.Fatal("rect interior missing at corners")
	}
	if colorAt(g, 15, 15) == c2 {
		t.Fatal("rect bled past its bounds")
	}

	// Negative origin clips instead of wrapping or panicking.
	g.rect(-5, -5, 8, 8, 3)
	if colorAt(g, 0, 0) != g.palette[3] {
		t.Fatal("clipped rect should cover (0,0)")
	}
	if colorAt(g, 0, 3) != g.palette[1] {
		t.Fatal("rect from y=-5 with h=8 must stop at y=2")
	}
}

func TestHlineClipsToFramebuffer(t *testing.T) {
	g := newTestGame()
	g.cls(1)

	g.hline(3, -100, 1000, 4)
	c4 := g.palette[4]
	for x := range int32(VEX_W) {
		if colorAt(g, x, 3) != c4 {
			t.Fatalf("hline gap at (%d,3)", x)
		}
	}
	if colorAt(g, 10, 2) != g.palette[1] || colorAt(g, 10, 4) != g.palette[1] {
		t.Fatal("hline leaked outside its row")
	}
}

func TestTriFixture(t *testing.T) {
	g := newTestGame()
	g.cls(1)
	g.tri(0, 0, 10, 0, 0, 10, 2)

	c2 := g.palette[2]
	// Hypotenuse from (10,0) to (0,10): row y spans x in [0, 10-y].
	if colorAt(g, 0, 0) != c2 || colorAt(g, 5, 5) != c2 || colorAt(g, 10, 0) != c2 {
		t.Fatal("tri missing expected interior/edge pixels")
	}
	if colorAt(g, 6, 5) == c2 {
		t.Fatal("tri filled beyond hypotenuse at (6,5)")
	}
	if colorAt(g, 11, 0) == c2 {
		t.Fatal("tri bled past right vertex")
	}

	// Hostile span is rejected rather than drawn or panicking.
	g.tri(0, -6000, 6000, 6000, -6000, 6000, 3)
}

func TestToneEngineReadPhaseAndEnd(t *testing.T) {
	e := &toneEngine{}

	// Sustain 5 frames with no attack/decay/release: the engine pads
	// zero-length attacks to 32 samples (~0.66 ms) to avoid clicks, so
	// the first 32 samples ramp 0 -> full and the rest holds sustain.
	// flags 0 selects channel 0, 50% duty, and center panning
	// (constant-power ~0.707).
	e.tone(100, 5, 100, 0)

	buf := make([]byte, 4*64)
	n, err := e.Read(buf)
	if err != nil {
		t.Fatalf("Read: %v", err)
	}
	if n != len(buf) {
		t.Fatalf("Read returned %d bytes, want %d", n, len(buf))
	}

	// First sample starts at silence due to the padded attack.
	if l := int16(binary.LittleEndian.Uint16(buf[0:])); l != 0 {
		t.Fatalf("frame 0: L = %d, want 0 (padded attack)", l)
	}
	if r := int16(binary.LittleEndian.Uint16(buf[2:])); r != 0 {
		t.Fatalf("frame 0: R = %d, want 0 (padded attack)", r)
	}
	// After the 32-sample fade-in the pulse is at full scale.
	want := int16(5656) // 8000 * constant-power center gain, truncated
	for i := 32; i < 48; i++ {
		l := int16(binary.LittleEndian.Uint16(buf[i*4:]))
		r := int16(binary.LittleEndian.Uint16(buf[i*4+2:]))
		if l != want || r != want {
			t.Fatalf("frame %d: L/R = %d/%d, want %d/%d", i, l, r, want, want)
		}
	}
	// Still in sustain a few hundred samples later.
	for i := 32; i < 64; i++ {
		l := int16(binary.LittleEndian.Uint16(buf[i*4:]))
		if l != want {
			t.Fatalf("frame %d: L = %d, want %d", i, l, want)
		}
	}

	// The voice ends after attack+sustain+release frames; past the end
	// the stream is silence and never returns io.EOF.
	e.tone(1, 1, 100, 0) // retrigger: 1 frame of sustain, no release (padded to 32+800)
	whole := make([]byte, 4*(toneRate/60+32))
	if n, err := e.Read(whole); err != nil || n != len(whole) {
		t.Fatalf("voice-length Read = (%d, %v)", n, err)
	}
	tail := make([]byte, 8)
	if n, err := e.Read(tail); err != nil || n != 8 {
		t.Fatalf("tail Read = (%d, %v), want (8, nil)", n, err)
	}
	for i := range 8 {
		if tail[i] != 0 {
			t.Fatalf("expected silence after voice end, byte %d = %d", i, tail[i])
		}
	}
}

func TestToneEngineKillAndClamps(t *testing.T) {
	e := &toneEngine{}

	// A sustained voice silenced by the kill idiom on another channel's
	// trigger must not keep sounding: kill only affects its own channel.
	// The padded 32-sample attack means sample 0 is silence; check after
	// the fade-in.
	e.tone(440, 10, 100, 0) // channel 0 sustains for 10 frames
	e.tone(262, 0, 0, 3)    // channel 3: all-zero duration is a no-op
	buf := make([]byte, 4*64)
	if _, err := e.Read(buf); err != nil {
		t.Fatalf("Read: %v", err)
	}
	// First sample is silence due to attack padding.
	if l := int16(binary.LittleEndian.Uint16(buf[0:])); l != 0 {
		t.Fatalf("frame 0 should be silence due to padded attack, got %d", l)
	}
	l := int16(binary.LittleEndian.Uint16(buf[32*4:]))
	if l == 0 {
		t.Fatal("channel 0 should still sound after attack; kill on channel 3 leaked")
	}

	// Extreme arguments clamp instead of misbehaving.
	e.tone(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF)
	if _, err := e.Read(make([]byte, 4*800)); err != nil {
		t.Fatalf("hostile Read: %v", err)
	}
}

func TestToneEngineDutyFlipsAtQuarter(t *testing.T) {
	e := &toneEngine{}
	// 1000 Hz pulse, 25% duty (MODE1), sustained flat: phase advances
	// freq/rate per sample, period 48 samples. The padded 32-sample
	// attack ramps level 0->1, so check after the fade-in where level
	// is stable and phase is deterministic (ph = i*freq/rate %1).
	e.tone(1000, 4, 100, 1<<2)

	buf := make([]byte, 4*96)
	if _, err := e.Read(buf); err != nil {
		t.Fatalf("Read: %v", err)
	}
	sample := func(i int) int16 { return int16(binary.LittleEndian.Uint16(buf[i*4:])) }
	// After attack the envelope is at full scale, so magnitude is 5656.
	for i := 32; i < 96; i++ {
		ph := float64(i) * 1000.0 / float64(toneRate)
		ph -= float64(int(ph))
		wantHigh := ph < 0.25
		s := sample(i)
		if wantHigh && s != 5656 {
			t.Fatalf("sample %d ph=%.3f should be high 5656, got %d", i, ph, s)
		}
		if !wantHigh && s != -5656 {
			t.Fatalf("sample %d ph=%.3f should be low -5656, got %d", i, ph, s)
		}
	}
}

func TestToneEnvelopeRampsAreLinear(t *testing.T) {
	e := &toneEngine{}
	// attack=8, decay=0, sustain=8, release=8 frames at 48000 Hz => 640
	// samples per segment; zero-length decay snaps peak->sustain instantly.
	const att, susFrames, rel = 8, 8, 8
	e.tone(440, susFrames|(rel<<8)|(att<<24), 100, 0)

	const spf = float64(toneRate) / 60.0 // 800 samples/frame
	attSamples := int(att * spf)
	susSamples := int(susFrames * spf)
	total := attSamples + susSamples + int(rel*spf)

	buf := make([]byte, 4*(total+64))
	if _, err := e.Read(buf); err != nil {
		t.Fatalf("Read: %v", err)
	}
	sample := func(i int) int16 { return int16(binary.LittleEndian.Uint16(buf[i*4:])) }

	amp := func(i int) int {
		v := int(sample(i))
		if v < 0 {
			return -v
		}
		return v
	}
	// Mid-attack is roughly half full scale (linear ramp from 0); signs are
	// waveform phase, so compare magnitudes.
	if m := amp(attSamples / 2); m < 2000 || m > 3600 {
		t.Fatalf("mid-attack magnitude %d, want ~half of 5656", m)
	}
	// End of attack ~= full scale (center pan).
	full := amp(attSamples - 1)
	if full < 5400 || full > 5656 {
		t.Fatalf("post-attack magnitude %d, want ~5656", full)
	}
	// Sustain holds the same level (1 LSB truncation jitter allowed).
	if s := amp(attSamples + susSamples - 1); s-full > 1 && s < full-1 {
		t.Fatalf("sustain end magnitude %d, want ~%d", s, full)
	}
	// Release decays back toward silence.
	if s := amp(total - 1); s > 600 {
		t.Fatalf("release tail magnitude %d, want near zero", s)
	}
	// And after the release the voice is silent for good.
	for i := total; i < total+60; i++ {
		if sample(i) != 0 {
			t.Fatalf("voice still sounding %d frames past release", i-total)
		}
	}
}

func TestToneSlideReachesTarget(t *testing.T) {
	e := &toneEngine{}
	// Slide 220 -> 880 Hz over 20 frames of sustain; measure the average
	// period over the last 240 samples (one nominal cycle at 880 Hz) via
	// zero crossings and require it to land near the target octave.
	e.tone(220|(880<<16), 20, 100, 0)

	n := int(20 * toneRate / 60)
	buf := make([]byte, 4*n)
	if _, err := e.Read(buf); err != nil {
		t.Fatalf("Read: %v", err)
	}
	get := func(i int) float64 { return float64(int16(binary.LittleEndian.Uint16(buf[i*4:]))) }
	cross := func(from, to int) float64 {
		count := 0
		for i := from + 1; i < to; i++ {
			if get(i-1) <= 0 && get(i) > 0 {
				count++
			}
		}
		return float64(count) * float64(toneRate) / float64(to-from)
	}
	endHz := cross(n-2400, n-1)
	if endHz < 700 || endHz > 1060 {
		t.Fatalf("slide ended near %.0f Hz, want approaching 880", endHz)
	}
}

func TestToneNoiseMatchesLFSR(t *testing.T) {
	e := &toneEngine{}
	e.tone(8000, 8, 100, 1<<6) // noise on channel 0

	buf := make([]byte, 4*300)
	if _, err := e.Read(buf); err != nil {
		t.Fatalf("Read: %v", err)
	}
	get := func(i int) int16 { return int16(binary.LittleEndian.Uint16(buf[i*4:])) }
	// Noise: 15-bit LFSR with taps 14/12, clock clamped to
	// [8000,48000] (2*freq = 16000 here), one-pole lowpass at 0.18
	// with 1.4 gain, and the padded 32-sample attack. Reference
	// mirrors that exactly so any tap/clock/filter/attack drift
	// shows up as a sample mismatch.
	lfsr := uint16(0xACE1)
	nph := 0.0
	noiseRaw := 0.0
	noiseLp := 0.0
	level := 0.0
	slope := 1.0 / 32
	segLeft := int64(32)
	for i := range 300 {
		nclk := 2 * 8000.0
		if nclk < toneNoiseClkMin {
			nclk = toneNoiseClkMin
		}
		if nclk > toneNoiseClkMax {
			nclk = toneNoiseClkMax
		}
		nph += nclk / toneRate
		for nph >= 1 {
			nph--
			fb := uint16(1 - (((lfsr >> 14) ^ (lfsr >> 12)) & 1))
			lfsr = lfsr<<1 | fb
			if lfsr&1 == 1 {
				noiseRaw = 1
			} else {
				noiseRaw = -1
			}
		}
		noiseLp += 0.18 * (noiseRaw - noiseLp)
		s := noiseLp * 1.4
		want := int16(s * toneFullAmp * level * tonePanL[0])
		if get(i) != want {
			t.Fatalf("sample %d = %d, want %d (lfsr=%04x lp=%.4f level=%.4f)", i, get(i), want, lfsr, noiseLp, level)
		}
		if segLeft > 0 {
			segLeft--
		}
		if segLeft <= 0 {
			level = 1
			segLeft = 6400 // sustain remainder, large enough for 300 samples
		} else {
			level += slope
		}
		if i >= 31 {
			level = 1
		}
	}
}

func TestToneNoteModeIsConcertPitch(t *testing.T) {
	e := &toneEngine{}
	// MIDI note 69 = A4 = 440 Hz.
	e.tone(69, 8, 100, 1<<8)

	const win = 4800 // ~22 cycles at 440 Hz: crossing resolution ~10 Hz
	buf := make([]byte, 4*win)
	if _, err := e.Read(buf); err != nil {
		t.Fatalf("Read: %v", err)
	}
	get := func(i int) float64 { return float64(int16(binary.LittleEndian.Uint16(buf[i*4:]))) }
	crossings := 0
	for i := 1; i < win; i++ {
		if get(i-1) <= 0 && get(i) > 0 {
			crossings++
		}
	}
	hz := float64(crossings) * toneRate / win
	if hz < 420 || hz > 462 {
		t.Fatalf("note 69 measured %.1f Hz, want ~440", hz)
	}
}
