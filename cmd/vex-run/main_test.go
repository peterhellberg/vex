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

func sampleAt(buf []byte, i int) int16 {
	return int16(binary.LittleEndian.Uint16(buf[i*4:]))
}

func TestToneEngineReadPhaseAndEnd(t *testing.T) {
	e := &toneEngine{}

	e.tone(0, 100, 0) // half period = 48000/(2*100) = 240 samples

	buf := readFrames(t, e, 16)
	for i := range 16 {
		if s := sampleAt(buf, i); s != 8000 {
			t.Fatalf("frame %d: L = %d, want 8000 (phase must start positive)", i, s)
		}
		if l := int16(binary.LittleEndian.Uint16(buf[i*4+2:])); l != 8000 {
			t.Fatalf("frame %d: R = %d, want 8000", i, l)
		}
	}

	// The flat legacy blip ends after legacyBlipFrames (48000/10); past the
	// end the stream is silence and never returns io.EOF.
	e.tone(0, 1, 0) // retrigger: new tone spans [pos, pos+legacyBlipFrames)
	whole := readFrames(t, e, legacyBlipFrames)
	for i := range legacyBlipFrames {
		s := sampleAt(whole, i)
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

func TestToneEngineChannelsOverlapAndClamp(t *testing.T) {
	e := &toneEngine{}

	// Two channels in phase sum to double amplitude (linear below the knee).
	e.tone(0, 440, 0)
	e.tone(1, 440, 0)

	buf := readFrames(t, e, 16)
	if s := sampleAt(buf, 0); s != 16000 {
		t.Fatalf("two voices in phase: %d, want 16000", s)
	}

	// Out-of-range channels clamp onto 0..3 instead of erroring or panicking.
	e.tone(-7, 0, 0)   // silences channel 0
	e.tone(42, 440, 0) // plays on channel 3

	// Channel 0 is silent now, but channel 1 and the clamped channel 3 are
	// both live and in phase.
	buf = readFrames(t, e, 16)
	if s := sampleAt(buf, 0); s != 16000 {
		t.Fatalf("after silencing ch0 (clamped from -7): %d, want 16000 (ch1 + clamped ch3)", s)
	}

	// freq <= 0 silences a channel; all channels idle -> silence.
	e.tone(3, 0, 0)
	e.tone(1, 0, 0)
	buf = readFrames(t, e, 16)
	for i := range 16 {
		if s := sampleAt(buf, i); s != 0 {
			t.Fatalf("frame %d after silencing all channels: %d, want 0", i, s)
		}
	}
}

func TestToneEngineDecayEndsSilent(t *testing.T) {
	e := &toneEngine{}

	e.tone(0, 1000, 50) // decaying 50ms tone

	buf := readFrames(t, e, toneRate/20)
	for i := range 8 {
		if s := sampleAt(buf, i); s == 0 {
			t.Fatalf("frame %d of decaying tone is silent", i)
		}
	}

	// Past the duration the channel is silent again.
	tail := make([]byte, 64)
	if _, err := e.Read(tail); err != nil {
		t.Fatalf("tail Read: %v", err)
	}
	for i := range 16 {
		if s := sampleAt(tail, i); s != 0 {
			t.Fatalf("frame %d after decay end: %d, want 0", i, s)
		}
	}
}
