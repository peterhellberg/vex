package main

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"flag"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"testing"

	"github.com/tetratelabs/wazero"
)

// The golden render test runs every built cart (../../bin/carts/*.wasm) for
// 30 frames under wazero with a headless Game and pins the framebuffer hash
// in testdata/. This catches rasterization regressions in the Go host; the
// conformance tests at the repo root pin the shared tables.
//
// Regenerate goldens after an intentional rendering change:
//
//	go test ./... -update
//
// The test skips when the carts haven't been built (`zig build --prefix .`).

var update = flag.Bool("update", false, "rewrite golden framebuffer hashes")

const (
	goldenFrames = 30
)

func TestCartRenderGolden(t *testing.T) {
	cartsDir := filepath.Join("..", "..", "bin", "carts")

	entries, err := os.ReadDir(cartsDir)
	if err != nil {
		t.Skipf("carts not built (%v); run `zig build --prefix .` first", err)
	}

	var names []string
	for _, e := range entries {
		if strings.HasSuffix(e.Name(), ".wasm") {
			names = append(names, e.Name())
		}
	}
	if len(names) == 0 {
		t.Skip("no carts found in bin/carts")
	}
	sort.Strings(names)

	ctx := context.Background()

	for _, name := range names {
		t.Run(name, func(t *testing.T) {
			wasmBytes, err := os.ReadFile(filepath.Join(cartsDir, name))
			if err != nil {
				t.Fatalf("read cart: %v", err)
			}

			g := newTestGame()

			r := wazero.NewRuntime(ctx)
			defer r.Close(ctx)

			if err := buildEnvModule(ctx, g, r); err != nil {
				t.Fatalf("build env module: %v", err)
			}

			mod, err := r.InstantiateWithConfig(ctx, wasmBytes,
				wazero.NewModuleConfig().WithName("golden"))
			if err != nil {
				t.Fatalf("instantiate: %v", err)
			}

			updateFn := mod.ExportedFunction("update")
			if updateFn == nil {
				t.Fatal("cart has no update() export")
			}
			bootFn := mod.ExportedFunction("boot")

			// Same start order as initCart, minus the audio gating.
			g.palreset()
			g.cls(0)
			if bootFn != nil {
				if _, err := bootFn.Call(ctx); err != nil {
					t.Fatalf("boot: %v", err)
				}
			}

			for i := range goldenFrames {
				if _, err := updateFn.Call(ctx); err != nil {
					t.Fatalf("update frame %d: %v", i, err)
				}
			}

			sum := sha256.Sum256(g.pixels)
			hash := hex.EncodeToString(sum[:])

			goldPath := filepath.Join("testdata", strings.TrimSuffix(name, ".wasm")+".golden")

			if *update {
				if err := os.MkdirAll("testdata", 0o755); err != nil {
					t.Fatalf("mkdir testdata: %v", err)
				}
				if err := os.WriteFile(goldPath, []byte(hash+"\n"), 0o644); err != nil {
					t.Fatalf("write golden: %v", err)
				}
				return
			}

			want, err := os.ReadFile(goldPath)
			if err != nil {
				t.Skipf("no golden yet (%v); run `go test ./... -update` to create it", err)
			}
			if strings.TrimSpace(string(want)) != hash {
				t.Fatalf("framebuffer hash changed for %s:\n got %s\nwant %s\n(if intentional, re-run with -update)",
					name, hash, strings.TrimSpace(string(want)))
			}
		})
	}
}
