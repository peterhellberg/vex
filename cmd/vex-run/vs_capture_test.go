package main

// Temporary: capture toneEngine PCM while running /tmp/vex-sound.wasm.

import (
	"context"
	"os"
	"testing"

	"github.com/tetratelabs/wazero"
)

func TestVSCapture(t *testing.T) {
	ctx := context.Background()
	wasm, err := os.ReadFile("/tmp/vex-sound.wasm")
	if err != nil {
		if os.IsNotExist(err) {
			t.Skip("vex-sound cart not present")
		}
		t.Fatal(err)
	}
	g := newTestGame()
	g.audio = &toneEngine{}
	g.audioCtx = testAudioContext()

	r := wazero.NewRuntime(ctx)
	defer r.Close(ctx)
	if err := buildEnvModule(ctx, g, r); err != nil {
		t.Fatal(err)
	}
	mod, err := r.InstantiateWithConfig(ctx, wasm,
		wazero.NewModuleConfig().WithName("vs"))
	if err != nil {
		t.Fatalf("instantiate: %v", err)
	}
	bootFn := mod.ExportedFunction("boot")
	updateFn := mod.ExportedFunction("update")
	if bootFn != nil {
		if _, err := bootFn.Call(ctx); err != nil {
			t.Fatal(err)
		}
	}

	f, err := os.Create("/tmp/opencode/go_vs.pcm")
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()

	buf := make([]byte, 4*800)
	shorts := make([]byte, 2*800)
	for frame := 0; frame < 120; frame++ {
		if _, err := updateFn.Call(ctx); err != nil {
			t.Fatalf("update %d: %v", frame, err)
		}
		tickAudioClock(g, false)
		if _, err := g.audio.Read(buf); err != nil {
			t.Fatal(err)
		}
		for i := range 800 {
			s := int16(uint16(buf[i*4]) | uint16(buf[i*4+1])<<8)
			shorts[i*2] = byte(s)
			shorts[i*2+1] = byte(uint16(s) >> 8)
		}
		f.Write(shorts)
	}
}
