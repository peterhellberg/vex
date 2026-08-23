// Package vex carries repo-wide conformance checks. The FONT8 glyph table
// and the SWEETIE-16 palette are hand-mirrored across all three hosts
// (cmd/vex/main.c, cmd/vex-run/main.go, cmd/vex-web/assets/vex.js); these
// tests turn the "keep in sync with..." comments into enforced invariants.
package vex

import (
	"os"
	"regexp"
	"strconv"
	"strings"
	"testing"
)

func mustRead(t *testing.T, path string) string {
	t.Helper()
	b, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("read %s: %v", path, err)
	}
	return string(b)
}

// section returns the text between start (inclusive) and the next occurrence
// of end.
func section(s, start, end string) string {
	i := strings.Index(s, start)
	if i < 0 {
		return ""
	}
	s = s[i:]
	j := strings.Index(s[len(start):], end)
	if j < 0 {
		return ""
	}
	return s[:len(start)+j+len(end)]
}

var (
	hex64  = regexp.MustCompile(`0x([0-9A-Fa-f]{16})`)
	cColor = regexp.MustCompile(`\{\s*(\d+),\s*(\d+),\s*(\d+),\s*255\s*\}`)
	goRGB  = regexp.MustCompile(`0x([0-9A-Fa-f]{2}), 0x([0-9A-Fa-f]{2}), 0x([0-9A-Fa-f]{2}), 0xFF`)
	jsHex6 = regexp.MustCompile(`0x([0-9A-Fa-f]{6})`)
)

// fontFromC extracts the 96 FONT8 glyphs from the C host.
func fontFromC(t *testing.T) []uint64 {
	t.Helper()
	src := mustRead(t, "cmd/vex/main.c")
	ms := hex64.FindAllStringSubmatch(src, -1)
	if len(ms) != 96 {
		t.Fatalf("C host: expected 96 font glyphs, found %d", len(ms))
	}
	out := make([]uint64, len(ms))
	for i, m := range ms {
		v, err := strconv.ParseUint(m[1], 16, 64)
		if err != nil {
			t.Fatalf("bad glyph %s: %v", m[1], err)
		}
		out[i] = v
	}
	return out
}

// fontFromGo extracts the 96 glyphs from the Go host's font8 table.
func fontFromGo(t *testing.T) []uint64 {
	t.Helper()
	src := section(mustRead(t, "cmd/vex-run/main.go"), "var font8 = [96]uint64{", "\n}")
	ms := hex64.FindAllStringSubmatch(src, -1)
	if len(ms) != 96 {
		t.Fatalf("Go host: expected 96 font glyphs, found %d", len(ms))
	}
	out := make([]uint64, len(ms))
	for i, m := range ms {
		v, _ := strconv.ParseUint(m[1], 16, 64)
		out[i] = v
	}
	return out
}

// fontFromJS extracts the 96 BigInt glyph literals from vex.js.
func fontFromJS(t *testing.T) []uint64 {
	t.Helper()
	src := section(mustRead(t, "cmd/vex-web/assets/vex.js"), "const FONT8 = [", "];")
	ms := hex64.FindAllStringSubmatch(src, -1)
	if len(ms) != 96 {
		t.Fatalf("JS host: expected 96 font glyphs, found %d", len(ms))
	}
	out := make([]uint64, len(ms))
	for i, m := range ms {
		v, _ := strconv.ParseUint(m[1], 16, 64)
		out[i] = v
	}
	return out
}

func TestFontTablesMatchAcrossHosts(t *testing.T) {
	c, g, js := fontFromC(t), fontFromGo(t), fontFromJS(t)
	for i := range c {
		if c[i] != g[i] || c[i] != js[i] {
			t.Fatalf("FONT8 glyph %d diverges:\n c:  %#016x\n go: %#016x\n js: %#016x", i, c[i], g[i], js[i])
		}
	}
}

// palette extracts the SWEETIE-16 RGB values from a host source.
type rgb struct{ r, g, b uint32 }

func palette(t *testing.T) (c, g, js []rgb) {
	t.Helper()

	cs := mustRead(t, "cmd/vex/main.c")
	for _, m := range cColor.FindAllStringSubmatch(section(cs, "DEFAULT_PALETTE[16]", "};"), -1) {
		v := func(i int) uint32 { v, _ := strconv.ParseUint(m[i], 10, 32); return uint32(v) }
		c = append(c, rgb{v(1), v(2), v(3)})
	}

	gs := section(mustRead(t, "cmd/vex-run/main.go"), "var defaultPalette = [16][4]uint8{", "\n}")
	for _, m := range goRGB.FindAllStringSubmatch(gs, -1) {
		v := func(i int) uint32 { v, _ := strconv.ParseUint(m[i], 16, 32); return uint32(v) }
		g = append(g, rgb{v(1), v(2), v(3)})
	}

	jss := section(mustRead(t, "cmd/vex-web/assets/vex.js"), "const DEFAULT_PALETTE = [", "];")
	for _, m := range jsHex6.FindAllStringSubmatch(jss, -1) {
		v, _ := strconv.ParseUint(m[1], 16, 32)
		js = append(js, rgb{uint32(v >> 16 & 0xFF), uint32(v >> 8 & 0xFF), uint32(v & 0xFF)})
	}

	if len(c) != 16 || len(g) != 16 || len(js) != 16 {
		t.Fatalf("expected 16 palette entries per host, got c=%d go=%d js=%d", len(c), len(g), len(js))
	}
	return c, g, js
}

func TestPalettesMatchAcrossHosts(t *testing.T) {
	c, g, js := palette(t)
	for i := range c {
		if c[i] != g[i] || c[i] != js[i] {
			t.Fatalf("palette entry %d diverges:\n c:  %+v\n go: %+v\n js: %+v", i, c[i], g[i], js[i])
		}
	}
}
