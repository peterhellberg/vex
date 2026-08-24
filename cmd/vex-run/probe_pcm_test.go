package main

// Temporary end-to-end probe (not for the suite): loads a real cart through
// wazero with the full env module, lets boot() trigger one 24000-byte
// sample at 48 kHz, and measures how long the engine produces output.

import (
	"context"
	"os"
	"testing"

	"github.com/tetratelabs/wazero"
)

func TestProbeCartSampleDuration(t *testing.T) {
	ctx := context.Background()

	wasm, err := os.ReadFile("/tmp/opencode/smpprobe/probe.wasm")
	if err != nil {
		t.Fatal(err)
	}

	g := newTestGame()
	g.audio = &toneEngine{}
	g.audioCtx = testAudioContext()

	r := wazero.NewRuntime(ctx)
	defer r.Close(ctx)
	if err := buildEnvModule(ctx, g, r); err != nil {
		t.Fatalf("env module: %v", err)
	}
	mod, err := r.InstantiateWithConfig(ctx, wasm,
		wazero.NewModuleConfig().WithName("probe"))
	if err != nil {
		t.Fatalf("instantiate: %v", err)
	}
	bootFn := mod.ExportedFunction("boot")
	if bootFn == nil {
		t.Fatal("no boot export")
	}
	if _, err := bootFn.Call(ctx); err != nil {
		t.Fatalf("boot: %v", err)
	}

	// Drain 1 second of engine audio.
	buf := make([]byte, 4*48000)
	if _, err := g.audio.Read(buf); err != nil {
		t.Fatal(err)
	}
	lastNonZero := -1
	for i := range 48000 {
		l := int16(buf[i*4]) | int16(buf[i*4+1])<<8
		if l != 0 {
			lastNonZero = i
		}
	}
	t.Logf("non-silent until frame %d (want ~24000 = 0.5s)", lastNonZero+1)
	if lastNonZero < 23000 || lastNonZero > 25000 {
		t.Fatalf("sample duration wrong: %d frames", lastNonZero+1)
	}
}
