// Package vex carries repo-wide conformance checks. The FONT8 glyph table
// and the SWEETIE-16 palette are hand-mirrored across both hosts
// (cmd/vex/main.c, cmd/vex-web/assets/vex.js); these tests turn the
// "keep in sync with..." comments into enforced invariants.
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
	c, js := fontFromC(t), fontFromJS(t)
	for i := range c {
		if c[i] != js[i] {
			t.Fatalf("FONT8 glyph %d diverges:\n c:  %#016x\n js: %#016x", i, c[i], js[i])
		}
	}
}

// palette extracts the SWEETIE-16 RGB values from a host source.
type rgb struct{ r, g, b uint32 }

func palette(t *testing.T) (c, js []rgb) {
	t.Helper()

	cs := mustRead(t, "cmd/vex/main.c")
	for _, m := range cColor.FindAllStringSubmatch(section(cs, "DEFAULT_PALETTE[16]", "};"), -1) {
		v := func(i int) uint32 { v, _ := strconv.ParseUint(m[i], 10, 32); return uint32(v) }
		c = append(c, rgb{v(1), v(2), v(3)})
	}

	jss := section(mustRead(t, "cmd/vex-web/assets/vex.js"), "const DEFAULT_PALETTE = [", "];")
	for _, m := range jsHex6.FindAllStringSubmatch(jss, -1) {
		v, _ := strconv.ParseUint(m[1], 16, 32)
		js = append(js, rgb{uint32(v >> 16 & 0xFF), uint32(v >> 8 & 0xFF), uint32(v & 0xFF)})
	}

	if len(c) != 16 || len(js) != 16 {
		t.Fatalf("expected 16 palette entries per host, got c=%d js=%d", len(c), len(js))
	}
	return c, js
}

func TestPalettesMatchAcrossHosts(t *testing.T) {
	c, js := palette(t)
	for i := range c {
		if c[i] != js[i] {
			t.Fatalf("palette entry %d diverges:\n c:  %+v\n js: %+v", i, c[i], js[i])
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
		"duty_table[4]", ";")
	cPan := section(mustRead(t, "cmd/vex/main.c"),
		"pan_l[3]", ";")
	cKnee := section(mustRead(t, "cmd/vex/main.c"),
		"knee = ", ";")

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
		if cd[i] != jd[i] {
			t.Fatalf("duty cycle %d diverges: c=%v js=%v", i, cd[i], jd[i])
		}
	}
	for i := range cl {
		if cl[i] != jl[i] {
			t.Fatalf("pan left gain %d diverges: c=%v js=%v", i, cl[i], jl[i])
		}
	}
	if jr[1] != 0 || jr[2] != 1 {
		t.Fatalf("pan right tables unexpected: js=%v", jr)
	}

	ck := floats(cKnee)
	if ck[0] != 24000 {
		t.Fatalf("soft-clip knee diverges: c=%v", ck[0])
	}
}

// The mixer voice model (noise clock band, one-pole filter coefficients,
// full-scale amplitude) is hand-mirrored across both hosts; pin the
// literals so a retune in one host fails loudly instead of silently diverging
// the mix.
func TestToneMixerMatchAcrossHosts(t *testing.T) {
	cSrc := section(mustRead(t, "cmd/vex/main.c"),
		"// ---- audio (tone)", "static M3Result link_host")
	jsAll := mustRead(t, "cmd/vex-web/assets/vex.js")
	jsSrc := section(jsAll, "function toneWorkletMain", "registerProcessor")

	// Literals shared verbatim by both mixer loops.
	shared := []string{
		"0.22 *", // triangle one-pole coefficient
		"0.995",  // pulse DC blocker pole
	}
	for _, lit := range shared {
		if !strings.Contains(cSrc, lit) {
			t.Errorf("C mixer moved: %q missing from audio section", lit)
		}
		if !strings.Contains(jsSrc, lit) {
			t.Errorf("JS mixer moved: %q missing", lit)
		}
	}

	// Same values, spelled per host (8000 vs 8000.0, named consts).
	pinned := []struct {
		desc, c, js string
	}{
		{"full-scale amplitude", "TONE_FULL_AMP 8000.0", "this.fullAmp = 8000"},
		{"noise clock min", "TONE_NOISE_CLK_MIN 8000.0", "TONE_NOISE_CLK_MIN = 8000"},
		{"noise clock max", "TONE_NOISE_CLK_MAX 48000.0", "TONE_NOISE_CLK_MAX = 48000"},
		{"noise seed", "0x2CE1", "0x2CE1"},
		{"noise feedback", "(v->lfsr ^ (v->lfsr >> 1)) & 1", "(v.lfsr ^ (v.lfsr >> 1)) & 1"},
		{"noise right shift", "v->lfsr >> 1", "v.lfsr >> 1"},
		{"noise output", "s = (v->lfsr & 1) ? 1.0 : -1.0", "s = (v.lfsr & 1) ? 1 : -1"},
		{"noise clock cap", "TONE_NOISE_CLK_MAX 48000.0", "Math.min(Math.max(nclk, TONE_NOISE_CLK_MIN), TONE_NOISE_CLK_MAX)"},
		{"PWM motion", "v->duty_left", "v.dutyLeft"},
		{"pulse warmth", "v->lp += 0.5 *", "v.lp += 0.5 * (raw - v.lp)"},
		{"release trigger field", "release_only", "releaseOnly"},
		{"release branch", "t->release_only", "t.releaseOnly"},
		{"FM trigger field", "int fm = (flags >> 30) & 1", "fm: false"},
		{"FM state", "mod_ph", "modPh"},
	}
	for _, p := range pinned {
		if !strings.Contains(cSrc, p.c) {
			t.Errorf("C %s moved: %q missing from audio section", p.desc, p.c)
		}
		if !strings.Contains(jsSrc, p.js) {
			t.Errorf("JS %s moved: %q missing", p.desc, p.js)
		}
	}
	for _, lit := range []string{
		"const pwm = (flags & 0x800) !== 0",
		"const duty = pwm ?",
		"const fm = (flags & 0x40000000) !== 0",
	} {
		if !strings.Contains(jsAll, lit) {
			t.Errorf("JS PWM parser moved: %q missing", lit)
		}
	}
}

func TestZeroSustainTargetsSnapAcrossHosts(t *testing.T) {
	hosts := []struct {
		name, source, start, end, seg, freq, duty string
	}{
		{"C", mustRead(t, "cmd/vex/main.c"), "if (n <= 0) {", "continue;", "v->seg == 2", "v->freq = v->freq_to", "v->duty = v->duty_to"},
		{"JS", mustRead(t, "cmd/vex-web/assets/vex.js"), "if (n <= 0) {", "continue;", "v.seg === SEG_SUSTAIN", "v.freq = v.freqTo", "v.duty = v.dutyTo"},
	}
	for _, host := range hosts {
		branch := section(host.source, host.start, host.end)
		for _, pin := range []string{host.seg, host.freq, host.duty} {
			if !strings.Contains(branch, pin) {
				t.Errorf("%s zero-length sustain does not snap %q", host.name, pin)
			}
		}
	}
}