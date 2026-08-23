package main

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"flag"
	"fmt"
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
			sha := hex.EncodeToString(sum[:])

			// FNV-1a 64 over the same bytes: the C host prints this exact
			// value for a headless run (`vex -n 30 cart.wasm`), so golden
			// files double as cross-host pixel-parity fixtures.
			const ( // FNV-1a offset basis and prime
				fnvOffset = 1469598103934665603
				fnvPrime  = 1099511628211
			)
			var fnv uint64 = fnvOffset
			for _, b := range g.pixels {
				fnv ^= uint64(b)
				fnv *= fnvPrime
			}

			if dumpDir := os.Getenv("VEX_GOLDEN_DUMP"); dumpDir != "" {
				_ = os.MkdirAll(dumpDir, 0o755)
				_ = os.WriteFile(filepath.Join(dumpDir, name+".raw"), g.pixels, 0o644)
			}

			goldPath := filepath.Join("testdata", strings.TrimSuffix(name, ".wasm")+".golden")

			if *update {
				if err := os.MkdirAll("testdata", 0o755); err != nil {
					t.Fatalf("mkdir testdata: %v", err)
				}
				body := fmt.Sprintf("sha256 %s\nfnv1a64 %016x\n", sha, fnv)
				if err := os.WriteFile(goldPath, []byte(body), 0o644); err != nil {
					t.Fatalf("write golden: %v", err)
				}
				return
			}

			want, err := os.ReadFile(goldPath)
			if err != nil {
				t.Skipf("no golden yet (%v); run `go test ./... -update` to create it", err)
			}
			wantSha, wantFnv, ok := parseGolden(string(want))
			if !ok {
				t.Fatalf("malformed golden file %s", goldPath)
			}
			if wantSha != sha {
				t.Fatalf("framebuffer sha256 changed for %s:\n got %s\nwant %s", name, sha, wantSha)
			}
			if wantFnv != fmt.Sprintf("%016x", fnv) {
				t.Fatalf("framebuffer fnv1a64 changed for %s: got %016x want %s", name, fnv, wantFnv)
			}
		})
	}
}

// parseGolden reads "sha256 <hex>" / "fnv1a64 <hex>" lines.
func parseGolden(s string) (sha, fnv string, ok bool) {
	for _, line := range strings.Split(strings.TrimSpace(s), "\n") {
		fields := strings.Fields(line)
		if len(fields) != 2 {
			continue
		}
		switch fields[0] {
		case "sha256":
			sha = fields[1]
		case "fnv1a64":
			fnv = fields[1]
		}
	}
	return sha, fnv, sha != "" && fnv != ""
}
