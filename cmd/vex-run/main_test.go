package main

import (
	"encoding/binary"
	"sync"
	"testing"
	"unsafe"

	"github.com/hajimehoshi/ebiten/v2/audio"
)

// newTestGame builds a Game without ebiten/audio initialization so drawing
// logic can be exercised headlessly.
var (
	testCtxOnce sync.Once
	testCtx     *audio.Context
)

// testAudioContext returns the process-wide ebiten audio context (creating
// it panics if called twice).
func testAudioContext() *audio.Context {
	testCtxOnce.Do(func() { testCtx = audio.NewContext(toneRate) })
	return testCtx
}

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

func readFrames(t *testing.T, e *toneEngine, frames int) []byte {
	t.Helper()
	buf := make([]byte, 4*frames)
	n, err := e.Read(buf)
	if err != nil {
		t.Fatalf("Read: %v", err)
	}
	if n != len(buf) {
		t.Fatalf("Read returned %d bytes, want %d", n, len(buf))
	}
	return buf
}

func s16At(buf []byte, i int) int16 {
	return int16(binary.LittleEndian.Uint16(buf[i*4:]))
}

func TestToneEngineReadPhaseAndLegacyBlip(t *testing.T) {
	e := &toneEngine{}

	e.tone(0, 100, -1) // half period = 48000/(2*100) = 240 samples

	buf := readFrames(t, e, 16)
	for i := range 16 {
		if s := s16At(buf, i); s != 8000 {
			t.Fatalf("frame %d: L = %d, want 8000 (phase must start positive)", i, s)
		}
		if l := int16(binary.LittleEndian.Uint16(buf[i*4+2:])); l != 8000 {
			t.Fatalf("frame %d: R = %d, want 8000", i, l)
		}
	}

	// The flat legacy blip (ms < 0) ends after legacyBlipFrames (48000/10);
	// past the end the stream is silence and never returns io.EOF.
	e.tone(0, 1, -1) // retrigger: new tone spans [pos, pos+legacyBlipFrames)
	whole := readFrames(t, e, legacyBlipFrames)
	for i := range legacyBlipFrames {
		s := s16At(whole, i)
		if s != 8000 && s != -8000 {
			t.Fatalf("frame %d inside flat blip: %d, want ±8000", i, s)
		}
	}
	tail := make([]byte, 8)
	if n, err := e.Read(tail); err != nil || n != 8 {
		t.Fatalf("tail Read = (%d, %v), want (8, nil)", n, err)
	}
	for i := range 8 {
		if tail[i] != 0 {
			t.Fatalf("expected silence after tone end, byte %d = %d", i, tail[i])
		}
	}
}

func TestToneEngineSustainHoldsUntilReplaced(t *testing.T) {
	e := &toneEngine{}

	e.tone(0, 440, 0) // ms == 0: sustain at flat amplitude

	// Well past the legacy blip length the voice must still be going.
	buf := readFrames(t, e, legacyBlipFrames*3+64)
	last := legacyBlipFrames*3 + 60
	if s := s16At(buf, last); s != 8000 && s != -8000 {
		t.Fatalf("sustained frame %d: %d, want ±8000", last, s)
	}

	// A zero-freq event is what silences a sustained voice.
	e.tone(0, 0, 0)
	tail := readFrames(t, e, 16)
	for i := range 16 {
		if s := s16At(tail, i); s != 0 {
			t.Fatalf("frame %d after silencing sustained voice: %d, want 0", i, s)
		}
	}
}

func TestToneEngineChannelsOverlapAndClamp(t *testing.T) {
	e := &toneEngine{}

	// Two channels in phase sum to double amplitude (linear below the knee).
	// Sustained voices (ms == 0) keep the overlap going indefinitely.
	e.tone(0, 440, 0)
	e.tone(1, 440, 0)

	buf := readFrames(t, e, 16)
	if s := s16At(buf, 0); s != 16000 {
		t.Fatalf("two voices in phase: %d, want 16000", s)
	}

	// Out-of-range channels clamp onto 0..3 instead of erroring or panicking.
	e.tone(-7, 0, -1)  // silences channel 0
	e.tone(42, 440, 0) // plays on channel 3

	// Channel 0 is silent now, but channel 1 and the clamped channel 3 are
	// both live and in phase.
	buf = readFrames(t, e, 16)
	if s := s16At(buf, 0); s != 16000 {
		t.Fatalf("after silencing ch0 (clamped from -7): %d, want 16000 (ch1 + clamped ch3)", s)
	}

	// freq <= 0 silences a channel; all channels idle -> silence.
	e.tone(3, 0, 0)
	e.tone(1, 0, 0)
	buf = readFrames(t, e, 16)
	for i := range 16 {
		if s := s16At(buf, i); s != 0 {
			t.Fatalf("frame %d after silencing all channels: %d, want 0", i, s)
		}
	}
}

func TestToneEngineDecayEndsSilent(t *testing.T) {
	e := &toneEngine{}

	e.tone(0, 1000, 50) // decaying 50ms tone

	buf := readFrames(t, e, toneRate/20)
	for i := range 8 {
		if s := s16At(buf, i); s == 0 {
			t.Fatalf("frame %d of decaying tone is silent", i)
		}
	}

	// Past the duration the channel is silent again.
	tail := make([]byte, 64)
	if _, err := e.Read(tail); err != nil {
		t.Fatalf("tail Read: %v", err)
	}
	for i := range 16 {
		if s := s16At(tail, i); s != 0 {
			t.Fatalf("frame %d after decay end: %d, want 0", i, s)
		}
	}
}

func TestToneEngineVolumeScalesLinearly(t *testing.T) {
	e := &toneEngine{}

	e.setVol(0, 32) // half volume
	e.tone(0, 440, -1)

	buf := readFrames(t, e, 16)
	if s := s16At(buf, 0); s != 4000 {
		t.Fatalf("half-volume sample: %d, want 4000", s)
	}

	// Volume applies live to an already-sustaining voice (volume slides).
	e.tone(0, 0, 0) // retire the half-volume blip first
	e.tone(1, 440, 0)
	readFrames(t, e, 16)
	e.setVol(1, 99) // clamps to unity
	buf = readFrames(t, e, 16)
	if s := s16At(buf, 0); s != 8000 {
		t.Fatalf("clamped vol(99) sample: %d, want 8000", s)
	}
	e.setVol(1, -3) // clamps to silence
	buf = readFrames(t, e, 16)
	if s := s16At(buf, 0); s != 0 {
		t.Fatalf("clamped vol(-3) sample: %d, want 0", s)
	}
}

func TestToneEngineNoiseDistinctFromSquare(t *testing.T) {
	countFlips := func(e *toneEngine, frames int) int {
		buf := readFrames(t, e, frames)
		flips := 0
		for i := 1; i < frames; i++ {
			a, b := s16At(buf, i-1), s16At(buf, i)
			if (a > 0) != (b > 0) && a != b {
				flips++
			}
		}
		return flips
	}

	sq := &toneEngine{}
	sq.tone(0, 1000, 0) // half = 24 samples -> a flip every 24 frames
	squareFlips := countFlips(sq, toneRate/100)

	ns := &toneEngine{}
	ns.noise(0, 1000, 0) // LFSR stepped at the same cadence...
	ns.setVol(0, 3)      // ...and exercising the per-channel gain path
	noiseFlips := countFlips(ns, toneRate/100)

	if noiseFlips == 0 || noiseFlips == squareFlips {
		t.Fatalf("noise zero-crossings %d must differ from square's %d and be nonzero",
			noiseFlips, squareFlips)
	}
}

func TestToneEngineAposAdvancesWithStream(t *testing.T) {
	e := &toneEngine{}

	if got := e.apos(); got != 0 {
		t.Fatalf("fresh apos = %d, want 0", got)
	}
	readFrames(t, e, 480)
	if got := e.apos(); got != 480 {
		t.Fatalf("apos after 480 frames = %d", got)
	}
	readFrames(t, e, 123)
	if got := e.apos(); got != 603 {
		t.Fatalf("apos after 603 frames = %d", got)
	}
}

func TestToneEnginePitchRetunesWithoutRestart(t *testing.T) {
	e := &toneEngine{}

	// Sustained square at 250 Hz (half = 96 samples); let it settle into its
	// cycle, then retune mid-note to 500 Hz.
	e.tone(0, 250, 0)
	buf := readFrames(t, e, 200)
	if s := s16At(buf, 0); s != 8000 {
		t.Fatalf("voice must start positive, got %d", s)
	}
	e.pitch(0, 500) // half = 48

	// Post-pitch cadence: polarity must toggle every 48 output frames.
	// The exact count over 480 frames depends on where in the old cycle the
	// retune landed, so allow one toggle of slack either way.
	toggles := countToggles(t, e, 480)
	if toggles < 9 || toggles > 11 {
		t.Fatalf("post-pitch toggles over 480 frames = %d, want ~10 (500 Hz)", toggles)
	}

	// Pitch on a silent channel is a no-op.
	e.tone(0, 0, 0) // retire ch0 first
	e.tone(1, 0, 0)
	e.pitch(1, 440) // must not panic or resurrect anything
	buf = readFrames(t, e, 16)
	for i := range 16 {
		if s := s16At(buf, i); s != 0 {
			t.Fatalf("pitch on silent channel produced sound: %d", s)
		}
	}
}

func countToggles(t *testing.T, e *toneEngine, frames int) int {
	t.Helper()
	buf := readFrames(t, e, frames)
	toggles := 0
	prev := s16At(buf, 0) > 0
	for i := 1; i < frames; i++ {
		pos := s16At(buf, i) > 0
		if pos != prev {
			toggles++
			prev = pos
		}
	}
	return toggles
}

func TestToneEngineSamplePlayback(t *testing.T) {
	e := &toneEngine{}

	// 48 bytes of ramp PCM played at 96000 Hz = 24 output frames per pass;
	// values are signed 8-bit.
	pcm := make([]byte, 48)
	for i := range pcm {
		pcm[i] = byte(int8(i*2 - 48)) // quiet ramp: stays below the soft-clip knee
	}

	e.Sample(0, pcm, 96000, 0)
	buf := readFrames(t, e, 24)

	// First frame renders PCM[0] exactly (-48 * 256, linear region).
	if s := s16At(buf, 0); s != -48*256 {
		t.Fatalf("first sample = %d, want %d", s, -48*256)
	}

	// One-shot: after len/rate seconds the channel is silent again.
	tail := readFrames(t, e, 8)
	for i := range 8 {
		if s := s16At(tail, i); s != 0 {
			t.Fatalf("one-shot sample still sounding %d frames later: %d", i, s)
		}
	}

	// Tail loop sustains indefinitely.
	e.Sample(1, pcm, toneRate, 12) // 48-byte sample looping its last 12 bytes
	readFrames(t, e, 60)
	buf = readFrames(t, e, 64)
	live := false
	for i := range 64 {
		if s := s16At(buf, i); s != 0 {
			live = true
		}
	}
	if !live {
		t.Fatal("looped sample went silent")
	}
	// ...and a zero-freq event silences it.
	e.tone(1, 0, 0)
	tail = readFrames(t, e, 16)
	for i := range 16 {
		if s := s16At(tail, i); s != 0 {
			t.Fatalf("looped sample survived silencing: %d", s)
		}
	}

	// Caps: oversized len truncates to sampleMaxLen; rate clamps.
	big := make([]byte, sampleMaxLen+100)
	for i := range big {
		big[i] = 127
	}
	e.Sample(2, big, 999999, -5)
	buf = readFrames(t, e, 4)
	if s := s16At(buf, 0); s <= 0 {
		t.Fatalf("clamped sample should sound, got %d", s)
	}
}

func TestTickAudioClockVirtualWhenHeadless(t *testing.T) {
	g := newTestGame()
	g.audio = &toneEngine{}
	g.audioCtx = testAudioContext()
	g.audioOn = true // player creation already failed (no device)
	g.audioReady = true

	tickAudioClock(g, false)
	if got := g.apos(); got != toneRate/60 {
		t.Fatalf("headless apos after one tick = %d, want %d", got, toneRate/60)
	}
	for range 59 {
		tickAudioClock(g, false)
	}
	if got := g.apos(); got != toneRate {
		t.Fatalf("headless apos after 60 ticks = %d, want %d (one second)", got, toneRate)
	}

	// With a live player the device drains the stream; the tick must not
	// double-advance the clock.
	before := g.apos()
	tickAudioClock(g, true)
	if after := g.apos(); after != before {
		t.Fatalf("apos moved with live player: %d -> %d", before, after)
	}
}
