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

// toneTables extracts the shared tone() mixer constants -- pulse duty cycles,
// constant-power pan gains, and the soft-clip knee/top -- from each host's
// audio source. The mixers must agree on these or the same cart sounds
// different per host.
func TestToneTablesMatchAcrossHosts(t *testing.T) {
	floats := func(s string) []float64 {
		if i := strings.Index(s, "{"); i >= 0 {
			s = s[i+1:] // drop any type/bracket prefix like "[4]float64"
		}
		var out []float64
		for _, m := range regexp.MustCompile(`-?\d+(?:\.\d+)?`).FindAllString(s, -1) {
			v, _ := strconv.ParseFloat(m, 64)
			out = append(out, v)
		}
		return out
	}

	cs := section(mustRead(t, "cmd/vex/main.c"),
		"static const float duty_table[4]", ";")
	cPan := section(mustRead(t, "cmd/vex/main.c"),
		"static const float pan_l[3]", ";")
	cKnee := section(mustRead(t, "cmd/vex/main.c"),
		"const float knee = ", ";")

	gs := mustRead(t, "cmd/vex-run/main.go")
	gd := floats(section(gs, "toneDutyTable = [4]float64{", "}"))
	gl := floats(section(gs, "tonePanL      = [3]float64{", "}"))
	gr := floats(section(gs, "tonePanR      = [3]float64{", "}"))
	if len(gd) != 4 || len(gl) != 3 || len(gr) != 3 {
		t.Fatalf("Go tone tables moved: duty=%v panL=%v panR=%v", gd, gl, gr)
	}
	gKnee := section(gs, "const knee, top = ", "\n")

	jsSrc := mustRead(t, "cmd/vex-web/assets/vex.js")
	jsDuty := section(jsSrc, "this.dutyTable = ", ";")
	jsPanL := section(jsSrc, "this.panL = ", ";")
	jsPanR := section(jsSrc, "this.panR = ", ";")

	jd, jl, jr := floats(jsDuty), floats(jsPanL), floats(jsPanR)
	if !strings.Contains(jsSrc, "const knee = 24000, top = 32767") {
		t.Fatal("JS soft-clip constants missing or moved")
	}

	cd, cl := floats(cs), floats(cPan)
	if len(cd) != 4 || len(cl) != 3 {
		t.Fatalf("C tone tables moved: duty=%v panL=%v", cd, cl)
	}
	for i := range cd {
		if cd[i] != gd[i] || jd[i] != gd[i] {
			t.Fatalf("duty cycle %d diverges: c=%v go=%v js=%v", i, cd[i], gd[i], jd[i])
		}
	}
	for i := range cl {
		if cl[i] != gl[i] || jl[i] != gl[i] {
			t.Fatalf("pan left gain %d diverges: c=%v go=%v js=%v", i, cl[i], gl[i], jl[i])
		}
	}
	if gr[1] != 0 || gr[2] != 1 || jr[1] != 0 || jr[2] != 1 {
		t.Fatalf("pan right tables unexpected: go=%v js=%v", gr, jr)
	}

	ck, gk := floats(cKnee), floats(gKnee)
	if ck[0] != 24000 || gk[0] != 24000 {
		t.Fatalf("soft-clip knee diverges: c=%v go=%v", ck[0], gk[0])
	}
}
