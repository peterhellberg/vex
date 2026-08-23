package main

import (
	// Must precede the ebitengine import: ebitengine opens an X11 connection
	// from a package init() that runs before this package's init()s, so the
	// xgb logger override has to be installed by an earlier package init.
	_ "github.com/peterhellberg/vex/cmd/vex-run/xgbquiet"

	"bufio"
	"context"
	"errors"
	"flag"
	"fmt"
	"os"
	"runtime"
	"strings"
	"sync"
	"time"
	"unsafe"

	"github.com/hajimehoshi/ebiten/v2"
	"github.com/hajimehoshi/ebiten/v2/audio"
	"github.com/hajimehoshi/ebiten/v2/inpututil"
	"github.com/tetratelabs/wazero"
	"github.com/tetratelabs/wazero/api"
	"github.com/tetratelabs/wazero/imports/wasi_snapshot_preview1"
	"golang.org/x/sys/unix"
)

const (
	VEX_W          = 320
	VEX_H          = 180
	VEX_SCALE_DEF  = 3
	VEX_SCALE_MAX  = 20
	VEX_FONT_FIRST = 32
	VEX_COORD_MAX  = VEX_W * 16
	VEX_NUM_BTNS   = 6
)

var defaultPalette = [16][4]uint8{
	{0x1A, 0x1C, 0x2C, 0xFF},
	{0x5D, 0x27, 0x5D, 0xFF},
	{0xB1, 0x3E, 0x53, 0xFF},
	{0xEF, 0x7D, 0x57, 0xFF},
	{0xFF, 0xCD, 0x75, 0xFF},
	{0xA7, 0xF0, 0x70, 0xFF},
	{0x38, 0xB7, 0x64, 0xFF},
	{0x25, 0x71, 0x79, 0xFF},
	{0x29, 0x36, 0x6F, 0xFF},
	{0x3B, 0x5D, 0xC9, 0xFF},
	{0x41, 0xA6, 0xF6, 0xFF},
	{0x73, 0xEF, 0xF7, 0xFF},
	{0xF4, 0xF4, 0xF4, 0xFF},
	{0x94, 0xB0, 0xC2, 0xFF},
	{0x56, 0x6C, 0x86, 0xFF},
	{0x33, 0x3C, 0x57, 0xFF},
}

var vexKeys = [VEX_NUM_BTNS]ebiten.Key{
	ebiten.KeyLeft,
	ebiten.KeyRight,
	ebiten.KeyUp,
	ebiten.KeyDown,
	ebiten.KeyZ,
	ebiten.KeyX,
}

var mouseBtns = [...]ebiten.MouseButton{
	ebiten.MouseButtonLeft,
	ebiten.MouseButtonRight,
	ebiten.MouseButtonMiddle,
}

var font8 = [96]uint64{
	0x0000000000000000, 0x1818181818001800, 0x2828000000000000,
	0x28287C287C282800, 0x103C5038147C1000, 0x6264081020460600,
	0x304848304A443A00, 0x1010000000000000, 0x1020404040201000,
	0x1008040404081000, 0x00141C3E1C140000, 0x0010107C10100000,
	0x0000000000101020, 0x0000007C00000000, 0x0000000000100000,
	0x0204081020408000, 0x3C66666E76663C00, 0x1838181818183C00,
	0x3C66061C30607E00, 0x3C66061C06663C00, 0x0C1C2C4C7E0C0C00,
	0x7E607C0606663C00, 0x3C66607C66663C00, 0x7E060C1830303000,
	0x3C66663C66663C00, 0x3C66663E06663C00, 0x0000200000200000,
	0x0000200000202040, 0x0C18306030180C00, 0x00007C007C000000,
	0x6030180C18306000, 0x3C66060C10001000, 0x3C666E6E6E603C00,
	0x183C66667E666600, 0x7C66667C66667C00, 0x3C66606060663C00,
	0x786C6666666C7800, 0x7E60607860607E00, 0x7E60607860606000,
	0x3C66606E66663C00, 0x6666667E66666600, 0x3C18181818183C00,
	0x1E0C0C0C6C6C3800, 0x666C7878786C6600, 0x6060606060607E00,
	0x63777F6B63636300, 0x66767E7E6E666600, 0x3C66666666663C00,
	0x7C66667C60606000, 0x3C6666666A6C3A00, 0x7C66667C786C6600,
	0x3C66603C06663C00, 0x7E18181818181800, 0x6666666666663C00,
	0x66666666663C1800, 0x6363636B7F776300, 0x66663C3C66666600,
	0x6666663C18181800, 0x7E060C1830607E00, 0x3C30303030303C00,
	0x8040201008040200, 0x3C0C0C0C0C0C3C00, 0x10386C0000000000,
	0x000000000000007F, 0x2010080000000000, 0x00003C063E663E00,
	0x60607C6666667C00, 0x00003C6660663C00, 0x06063E6666663E00,
	0x00003C667E603C00, 0x1C30307830303000, 0x00003E66663E063C,
	0x60607C6666666600, 0x1800181818183C00, 0x060006060666663C,
	0x6060666C786C6600, 0x1818181818181E00, 0x0000667F7F6B6300,
	0x00007C6666666600, 0x00003C6666663C00, 0x00007C66667C6060,
	0x00003E66663E0606, 0x00006C7660606000, 0x00003E603C067C00,
	0x30307C3030301C00, 0x0000666666663E00, 0x00006666663C1800,
	0x0000636B7F7F3600, 0x0000663C183C6600, 0x00006666663E0C38,
	0x00007E0C18307E00, 0x0E18183018180E00, 0x1818180018181800,
	0x7018180C18187000, 0x0000000000000000, 0x0000000000000000,
}

var fontRows [96 * 8]uint8

func init() {
	runtime.LockOSThread()

	for i := range font8 {
		glyph := font8[i]
		for row := range 8 {
			fontRows[i*8+row] = uint8(glyph >> ((7 - row) * 8) & 0xFF)
		}
	}
}

func main() {
	if err := run(os.Args[1:]); err != nil && !errors.Is(err, flag.ErrHelp) {
		fmt.Fprintf(os.Stderr, "error: %v\n", err)
		os.Exit(1)
	}
}

type Input struct {
	scale int
	watch bool
}

func parse(args []string) (in Input, cart string, _ error) {
	fs := flag.NewFlagSet("vex-run", flag.ContinueOnError)
	in.scale = VEX_SCALE_DEF
	fs.IntVar(&in.scale, "s", VEX_SCALE_DEF, "window scale factor (1..20)")
	fs.IntVar(&in.scale, "scale", VEX_SCALE_DEF, "window scale factor (1..20)")
	fs.BoolVar(&in.watch, "w", false, "watch cart file for changes and auto-reload")
	fs.BoolVar(&in.watch, "watch", false, "watch cart file for changes and auto-reload")
	if err := fs.Parse(args); err != nil {
		return Input{}, "", err
	}

	cart = fs.Arg(0)
	if cart == "" {
		return Input{}, "", fmt.Errorf("missing cart path")
	}

	if in.scale < 1 {
		in.scale = 1
	}

	if in.scale > VEX_SCALE_MAX {
		in.scale = VEX_SCALE_MAX
	}

	return in, cart, nil
}

func run(args []string) error {
	if runtime.GOOS == "darwin" {
		defer filterStderr()()
	}

	in, cart, err := parse(args)
	if err != nil {
		fmt.Fprintf(os.Stderr, "usage: vex-run [-s scale] [-w] <cart.wasm>\n")
		return err
	}

	wasmBytes, err := os.ReadFile(cart)
	if err != nil {
		return fmt.Errorf("read cart: %w", err)
	}

	ctx := context.Background()

	r := wazero.NewRuntime(ctx)
	defer r.Close(ctx)

	wasi_snapshot_preview1.MustInstantiate(ctx, r)

	game := NewGame()

	if err := buildEnvModule(ctx, game, r); err != nil {
		return fmt.Errorf("build env module: %w", err)
	}

	module, err := r.Instantiate(ctx, wasmBytes)
	if err != nil {
		return fmt.Errorf("instantiate cart: %w", err)
	}

	game.module = module

	game.updateFn = module.ExportedFunction("update")
	if game.updateFn == nil {
		module.Close(ctx)
		return fmt.Errorf("cart has no update() export")
	}

	game.bootFn = module.ExportedFunction("boot")
	game.runtime = r
	game.cart = cart

	game.watch = in.watch
	if fi, _ := os.Stat(cart); fi != nil {
		game.lastMod = fi.ModTime()
	}

	ebiten.SetWindowSize(VEX_W*in.scale, VEX_H*in.scale)
	ebiten.SetWindowTitle("vex")
	ebiten.SetWindowResizingMode(ebiten.WindowResizingModeEnabled)
	ebiten.SetTPS(60)

	game.uiReady = true
	if err := ebiten.RunGame(game); err != nil && err != ebiten.Termination {
		return err
	}

	module.Close(ctx)

	return nil
}

type Game struct {
	pixels []byte
	frame  []uint32
	// palette holds the 16 palette colors packed as RGBA into a uint32
	// (little-endian: R is the low byte). It mirrors defaultPalette and is
	// written by pal()/palreset(). The drawing primitives write packed colors
	// straight into frame (a uint32 view over pixels) instead of four
	// separate byte stores.
	palette [16]uint32

	prevBtns uint8

	updateFn   api.Function
	bootFn     api.Function
	module     api.Module
	bootCalled bool

	runtime  wazero.Runtime
	cart     string
	watch    bool
	lastMod  time.Time
	pollTick int
	instSeq  int

	// uiReady is set just before ebiten.RunGame starts. Input/window queries
	// (btn, btnp, mx, my, mbtn) return zero until then, so a cart driven
	// headlessly (e.g. from tests, which never open a window) gets stable,
	// device-free results instead of poking ebiten's uninitialized UI layer.
	uiReady bool

	// Reusable scanline buffers for tri(), avoiding a map + per-row heap
	// allocation on every filled triangle.
	triL []int32
	triR []int32

	// Audio for beep(): one shared context and a single persistent mixer
	// player that synthesizes the beeps (see beepEngine).
	audioCtx   *audio.Context
	audio      *beepEngine
	audioPl    *audio.Player
	audioOn    bool // the persistent player has been created
	audioReady bool // the device has started consuming the stream
	startedAt  time.Time
}

func NewGame() *Game {
	pixels := make([]byte, VEX_W*VEX_H*4)
	g := &Game{
		pixels:    pixels,
		frame:     unsafe.Slice((*uint32)(unsafe.Pointer(&pixels[0])), VEX_W*VEX_H),
		audioCtx:  audio.NewContext(beepRate),
		audio:     &beepEngine{},
		startedAt: time.Now(),
	}
	g.palreset()
	return g
}

func (g *Game) coordOK(v int32) bool {
	return v >= -VEX_COORD_MAX && v <= VEX_COORD_MAX
}

func (g *Game) pset(x, y int32, color uint32) {
	if uint32(x) >= VEX_W || uint32(y) >= VEX_H {
		return
	}
	g.frame[int(y)*VEX_W+int(x)] = g.palette[color&15]
}

func (g *Game) cls(color uint32) {
	v := g.palette[color&15]
	frame := g.frame
	frame[0] = v
	for n := 1; n < len(frame); {
		c := copy(frame[n:], frame[:n])
		if c < n {
			break
		}
		n += c
	}
}

func (g *Game) hline(y, x0, x1 int32, color uint32) {
	if uint32(y) >= VEX_H {
		return
	}

	if x0 > x1 {
		x0, x1 = x1, x0
	}

	if x1 < 0 || x0 >= VEX_W {
		return
	}

	if x0 < 0 {
		x0 = 0
	}

	if x1 >= VEX_W {
		x1 = VEX_W - 1
	}

	v := g.palette[color&15]
	frame := g.frame
	start := int(y)*VEX_W + int(x0)
	end := int(y)*VEX_W + int(x1) + 1
	for i := start; i < end; i++ {
		frame[i] = v
	}
}

func (g *Game) line(x0, y0, x1, y1 int32, color uint32) {
	dx := int32(x1 - x0)
	if dx < 0 {
		dx = -dx
	}

	sx := int32(1)
	if x0 > x1 {
		sx = -1
	}

	dy := int32(y1 - y0)
	if dy < 0 {
		dy = -dy
	}

	sy := int32(1)
	if y0 > y1 {
		sy = -1
	}

	err := dx - dy

	for {
		g.pset(x0, y0, color)

		if x0 == x1 && y0 == y1 {
			break
		}

		e2 := err * 2
		if e2 > -dy {
			err -= dy
			x0 += sx
		}

		if e2 < dx {
			err += dx
			y0 += sy
		}
	}
}

func (g *Game) rect(x, y, w, h int32, color uint32) {
	if w <= 0 || h <= 0 {
		return
	}

	if !g.coordOK(x) || !g.coordOK(y) {
		return
	}

	if w > VEX_W {
		w = VEX_W
	}

	if h > VEX_H {
		h = VEX_H
	}

	x0 := max(x, 0)
	y0 := max(y, 0)
	x1 := min(x+w, VEX_W)
	y1 := min(y+h, VEX_H)
	v := g.palette[color&15]
	frame := g.frame

	for yy := y0; yy < y1; yy++ {
		start := int(yy)*VEX_W + int(x0)
		end := start + int(x1-x0)
		for i := start; i < end; i++ {
			frame[i] = v
		}
	}
}

func (g *Game) rectb(x, y, w, h int32, color uint32) {
	if w <= 0 || h <= 0 {
		return
	}

	if !g.coordOK(x) || !g.coordOK(y) {
		return
	}

	if w > VEX_W {
		w = VEX_W
	}

	if h > VEX_H {
		h = VEX_H
	}

	g.rect(x, y, w, 1, color)
	g.rect(x, y+h-1, w, 1, color)
	g.rect(x, y+1, 1, h-2, color)

	if w > 1 {
		g.rect(x+w-1, y+1, 1, h-2, color)
	}
}

func (g *Game) circ(cx, cy, r int32, color uint32) {
	if r < 0 {
		r = 0
	}

	if r > VEX_COORD_MAX {
		r = VEX_COORD_MAX
	}

	x := r
	y := int32(0)
	err := int32(0)

	for x >= y {
		g.hline(cy+y, cx-x, cx+x, color)
		g.hline(cy+x, cx-y, cx+y, color)
		g.hline(cy-y, cx-x, cx+x, color)
		g.hline(cy-x, cx-y, cx+y, color)

		y++
		if err <= 0 {
			err += 2*y + 1
		} else {
			x--
			err += 2*(y-x) + 1
		}
	}
}

func (g *Game) circb(cx, cy, r int32, color uint32) {
	if r < 0 {
		r = 0
	}

	if r > VEX_COORD_MAX {
		r = VEX_COORD_MAX
	}

	x := r
	y := int32(0)
	err := int32(0)

	for x >= y {
		g.pset(cx+x, cy+y, color)
		g.pset(cx+y, cy+x, color)
		g.pset(cx-y, cy+x, color)
		g.pset(cx-x, cy+y, color)
		g.pset(cx-x, cy-y, color)
		g.pset(cx-y, cy-x, color)
		g.pset(cx+y, cy-x, color)
		g.pset(cx+x, cy-y, color)

		y++
		if err <= 0 {
			err += 2*y + 1
		} else {
			x--
			err += 2*(y-x) + 1
		}
	}
}

func (g *Game) tri(x1, y1, x2, y2, x3, y3 int32, color uint32) {
	ymin := min(y1, min(y2, y3))
	ymax := max(y1, max(y2, y3))

	n := int(ymax - ymin + 1)
	// Guard the scanline buffer size: the C host rejects any vertex beyond
	// +-VEX_COORD_MAX, so a triangle spanning more rows than that is hostile
	// (a huge y range would otherwise allocate enormous buffers here).
	const maxRows = 2*VEX_COORD_MAX + 1
	if n <= 0 || n > maxRows {
		return
	}

	if n > len(g.triL) {
		g.triL = make([]int32, n)
		g.triR = make([]int32, n)
	}
	l := g.triL[:n]
	r := g.triR[:n]

	const inf = int32(1) << 30
	for i := range l {
		l[i] = inf
		r[i] = -inf
	}

	addEdge := func(ax, ay, bx, by int32) {
		if ay == by {
			return
		}

		slope := float64(bx-ax) / float64(by-ay)

		yStart, yEnd := ay, by
		if yStart > yEnd {
			yStart, yEnd = yEnd, yStart
		}

		for y := yStart; y <= yEnd; y++ {
			xf := float64(ax) + float64(y-ay)*slope

			xi := int32(xf)
			if xf < 0 && xf-float64(xi) > 0 {
				xi--
			}

			i := int(y - ymin)
			if xi < l[i] {
				l[i] = xi
			}

			if xi > r[i] {
				r[i] = xi
			}
		}
	}

	addEdge(x1, y1, x2, y2)
	addEdge(x2, y2, x3, y3)
	addEdge(x3, y3, x1, y1)

	for i := range n {
		if l[i] <= r[i] {
			g.hline(ymin+int32(i), l[i], r[i], color)
		}
	}
}

func (g *Game) trib(x1, y1, x2, y2, x3, y3 int32, color uint32) {
	g.line(x1, y1, x2, y2, color)
	g.line(x2, y2, x3, y3, color)
	g.line(x3, y3, x1, y1, color)
}

func (g *Game) blit(m api.Module, ptr uint32, x, y, w, h int32, key uint32) {
	if w <= 0 || h <= 0 {
		return
	}

	if !g.coordOK(x) || !g.coordOK(y) {
		return
	}

	if w > VEX_W {
		w = VEX_W
	}

	if h > VEX_H {
		h = VEX_H
	}

	size := uint32(w) * uint32(h)

	data, ok := m.Memory().Read(ptr, size)
	if !ok {
		return
	}

	for row := int32(0); row < h; row++ {
		yy := y + row
		if yy < 0 || yy >= VEX_H {
			continue
		}

		src := data[row*w : (row+1)*w]
		frame := g.frame
		rowStart := int(yy) * VEX_W

		col := int32(0)
		for col < w {
			// Raw compare against the full key value: a key outside 0..255
			// never matches any pixel byte, matching the C and JS hosts.
			for col < w && uint32(src[col]) == key {
				col++
			}

			if col >= w {
				break
			}

			start := col

			run := src[col]
			for col < w && src[col] == run {
				col++
			}

			x0 := x + start
			x1 := x + col - 1
			if x0 < 0 {
				x0 = 0
			}

			if x1 >= VEX_W {
				x1 = VEX_W - 1
			}

			if x0 <= x1 {
				v := g.palette[uint32(run)&15]
				start := rowStart + int(x0)
				end := rowStart + int(x1) + 1
				for i := start; i < end; i++ {
					frame[i] = v
				}
			}
		}
	}
}

func (g *Game) text(m api.Module, ptr uint32, x, y int32, color uint32) {
	mem := m.Memory()
	size := mem.Size()
	if ptr >= size {
		return
	}

	// One read (a zero-copy view, not a copy) of the rest of linear memory;
	// carts keep their strings short and NUL-terminated.
	data, ok := mem.Read(ptr, size-ptr)
	if !ok {
		return
	}

	end := 0
	for end < len(data) && data[end] != 0 {
		end++
	}

	v := g.palette[color&15]
	frame := g.frame
	curX := x

	for _, ch := range data[:end] {
		idx := int(ch) - VEX_FONT_FIRST
		if idx < 0 || idx >= len(font8) {
			curX += 8
			continue
		}

		for yy := range int32(8) {
			py := y + yy
			if py < 0 || py >= VEX_H {
				continue
			}

			rowBits := fontRows[idx*8+int(yy)]
			rowStart := int(py) * VEX_W

			for xx := range int32(8) {
				if rowBits&(1<<(7-xx)) != 0 {
					px := curX + xx
					if px >= 0 && px < VEX_W {
						frame[rowStart+int(px)] = v
					}
				}
			}
		}

		curX += 8
	}
}

func (g *Game) title(m api.Module, ptr uint32) {
	ebiten.SetWindowTitle(readCString(m, ptr))
}

func (g *Game) btn(button uint32) uint32 {
	if !g.uiReady || int(button) >= VEX_NUM_BTNS {
		return 0
	}

	if ebiten.IsKeyPressed(vexKeys[button]) {
		return 1
	}

	return 0
}

func (g *Game) btnp(button uint32) uint32 {
	if !g.uiReady || int(button) >= VEX_NUM_BTNS {
		return 0
	}

	held := ebiten.IsKeyPressed(vexKeys[button])

	prev := (g.prevBtns>>button)&1 != 0
	if held && !prev {
		return 1
	}

	return 0
}

func (g *Game) mx() uint32 {
	if !g.uiReady {
		return 0
	}
	x, _ := ebiten.CursorPosition()
	return uint32(max(0, min(x, VEX_W-1)))
}

func (g *Game) my() uint32 {
	if !g.uiReady {
		return 0
	}
	_, y := ebiten.CursorPosition()
	return uint32(max(0, min(y, VEX_H-1)))
}

func (g *Game) mbtn(button uint32) uint32 {
	if !g.uiReady || int(button) < 0 || int(button) >= len(mouseBtns) {
		return 0
	}

	if ebiten.IsMouseButtonPressed(mouseBtns[button]) {
		return 1
	}

	return 0
}

func (g *Game) pal(index, rgb uint32) {
	i := index & 15
	g.palette[i] = (rgb>>16&0xFF)<<0 | (rgb>>8&0xFF)<<8 | (rgb&0xFF)<<16 | 0xFF<<24
}

func (g *Game) palreset() {
	for i, c := range defaultPalette {
		g.palette[i] = uint32(c[0]) | uint32(c[1])<<8 | uint32(c[2])<<16 | uint32(c[3])<<24
	}
}

// ---- audio (beep) ------------------------------------------------------

const (
	// The beep is synthesized at the device's native rate (48000 Hz) so the
	// host's resampler doesn't have to up-convert from a low rate. A 22050 Hz
	// stream gets sinc-resampled by PipeWire/ALSA on its way to the 48000 Hz
	// device and every square-wave edge rings audibly; 48000 passes straight
	// through and matches the C host's clean square.
	beepRate   = 48000
	beepFrames = beepRate / 10 // 100ms

	// audioBufferSize bounds the persistent player's latency. oto's default
	// buffer is 0.5s of audio, which would postpone every new beep; a short
	// buffer keeps the device pulling the stream within a few milliseconds.
	audioBufferSize = 40 * time.Millisecond

	// audioReadyTimeout is how long Update() may hold off starting the cart
	// clock while the audio device warms up (it is typically ready in a few
	// frames; the timeout only guards headless/CI runs).
	audioReadyTimeout = 2 * time.Second

	// audioReadyPosition is how much of the stream the device must have
	// consumed before the cart clock starts. Position() > 0 fires as soon as
	// ALSA starts draining the mux, but the PipeWire sink swallows the first
	// few hundred milliseconds of output during its pre-roll, so beeps placed
	// that early are inaudible. Waiting for ~800ms of consumed audio puts the
	// first beep well past the pre-roll (the C host's first note is lost to
	// device warm-up the same way).
	audioReadyPosition = 800 * time.Millisecond
)

// beepEngine synthesizes 100ms square-wave blips into one long-lived
// audio.Player. beep(freq) is monophonic: if a previous blip is still playing,
// it is cut off and the new one starts at the next sample (phase 0, matching
// the C host's freshly loaded sound wave and the JS host's fresh oscillator).
// This matches the typical "beep" semantics in retro consoles and keeps
// rapid-fire beeps from chaining into the future or stacking on top of each
// other. The earlier 16-voice pool design produced the latter: a cart that
// calls beep() in a tight loop piled up multiple in-phase copies of the same
// blip, hard-clipped, and dropped the 17th; a cart that called beep() across
// frames instead anchored every blip to the previous one's end and silently
// delayed the rest by 100ms each. Monophonic retrigger avoids both.
type beepEngine struct {
	mu      sync.Mutex
	pos     int64      // total frames produced (write head)
	voice   *beepVoice // nil = no active blip
	pending *uint32    // pending freq from a beep() call, applied at next Read
}

type beepVoice struct {
	freq  uint32
	half  int   // samples per square half-period (beepRate / (2*freq))
	phase int   // position within the current half-period
	end   int64 // exclusive last frame of the current blip
}

// beep schedules a 100ms blip at freq Hz, starting at phase 0. The blip
// replaces whatever is currently playing: a beep() arriving while a previous
// blip is still active cuts the previous one off and starts a new one at the
// next sample the audio device pulls. The cart-side call is non-blocking
// (just sets a flag); the audio thread picks it up on its next Read.
func (e *beepEngine) beep(freq uint32) {
	if freq < 1 || freq > 20000 {
		return
	}

	e.mu.Lock()
	defer e.mu.Unlock()
	f := freq
	e.pending = &f
}

// Read implements io.Reader with 16-bit stereo interleaved PCM and never
// returns io.EOF. Idle stream is silence; the active voice (if any) is a
// square wave starting at +8000, hard-clipped at the 16-bit range.
func (e *beepEngine) Read(p []byte) (int, error) {
	e.mu.Lock()
	defer e.mu.Unlock()

	// Apply the most recent pending freq as a retrigger: start a fresh blip
	// or, if one is already playing, replace its frequency and reset its
	// phase so the new note starts cleanly instead of clicking mid-cycle.
	if e.pending != nil {
		freq := *e.pending
		e.pending = nil
		half := beepRate / (2 * int(freq))
		if e.voice == nil {
			e.voice = &beepVoice{freq: freq, half: half, phase: 0, end: e.pos + beepFrames}
		} else {
			e.voice.freq = freq
			e.voice.half = half
			e.voice.phase = 0
			e.voice.end = e.pos + beepFrames
		}
	}

	n := 0
	for n+4 <= len(p) {
		if e.voice != nil && e.pos >= e.voice.end {
			e.voice = nil
		}

		acc := 0
		if e.voice != nil {
			if (e.voice.phase/e.voice.half)&1 == 0 {
				acc += 8000
			} else {
				acc -= 8000
			}
			e.voice.phase++
		}
		e.pos++

		s := int16(acc)
		p[n] = byte(s)
		p[n+1] = byte(uint16(s) >> 8)
		p[n+2] = byte(s)
		p[n+3] = byte(uint16(s) >> 8)
		n += 4
	}

	return n, nil
}

// ensureAudio creates and starts the persistent beep player on first use, so
// the audio device begins warming up before the cart's first beep. If the
// player can't be created (no audio device, headless/CI), audio is considered
// ready immediately so the cart clock isn't held back by the warm-up gate.
func (g *Game) ensureAudio() {
	if g.audioOn {
		return
	}
	g.audioOn = true

	p, err := audio.NewPlayer(g.audioCtx, g.audio)
	if err != nil {
		g.audioReady = true
		return
	}
	p.SetBufferSize(audioBufferSize)
	p.Play()
	g.audioPl = p
}

// audioFlowStarted reports whether the audio device has started consuming the
// persistent stream. The player is "playing" from the moment Play() is called,
// but the device needs a few frames to spin up; beeps queued before that are
// inaudible, which is how the C host's very first note mostly gets lost too.
func (g *Game) audioFlowStarted() bool {
	if g.audioPl == nil {
		return false
	}
	return g.audioPl.Position() > audioReadyPosition || time.Since(g.startedAt) > audioReadyTimeout
}

// beep(freq): play a short square-wave blip at freq Hz.
func (g *Game) beep(freq uint32) {
	if g.audioCtx == nil {
		return
	}
	g.ensureAudio()
	g.audio.beep(freq)
}

// initCart resets the palette, clears the framebuffer, and runs the cart's
// boot() if it exports one. It does not touch g's module/function fields or
// the bootCalled flag; the caller owns those and any rollback on error.
func (g *Game) initCart(ctx context.Context, bootFn api.Function) error {
	g.palreset()
	g.cls(0)

	if bootFn != nil {
		if _, err := bootFn.Call(ctx); err != nil {
			return fmt.Errorf("boot: %w", err)
		}
	}

	return nil
}

func (g *Game) Update() error {
	if inpututil.IsKeyJustPressed(ebiten.KeyEscape) {
		return ebiten.Termination
	}

	super := ebiten.IsKeyPressed(ebiten.KeyMeta) || ebiten.IsKeyPressed(ebiten.KeyMetaLeft) || ebiten.IsKeyPressed(ebiten.KeyMetaRight)

	if super && inpututil.IsKeyJustPressed(ebiten.KeyEnter) {
		ebiten.SetFullscreen(!ebiten.IsFullscreen())
	}

	if g.updateFn == nil {
		return nil
	}

	// Start the audio player immediately (not on the first beep) so the
	// device warms up while the cart is loading, then hold the cart clock
	// until the device is actually consuming the stream. Otherwise the first
	// beep lands before the device produces sound and the opening note is
	// lost -- exactly the ~150ms of inaudible startup the C host suffers.
	g.ensureAudio()
	if !g.audioReady {
		g.audioReady = g.audioFlowStarted()
		if !g.audioReady {
			return nil
		}
	}

	reload := super && inpututil.IsKeyJustPressed(ebiten.KeyR)

	if g.watch {
		g.pollTick++
		if g.pollTick >= 30 {
			g.pollTick = 0
			if fi, err := os.Stat(g.cart); err == nil {
				if m := fi.ModTime(); !m.Equal(g.lastMod) {
					reload = true
				}
			}
		}
	}

	if reload {
		if err := g.reloadCart(context.Background()); err == nil {
			if fi, _ := os.Stat(g.cart); fi != nil {
				g.lastMod = fi.ModTime()
			}
		} else {
			fmt.Fprintf(os.Stderr, "vex: reload: %v\n", err)
		}
	}

	if !g.bootCalled {
		g.bootCalled = true
		if err := g.initCart(context.Background(), g.bootFn); err != nil {
			return err
		}
	}

	_, err := g.updateFn.Call(context.Background())
	if err != nil {
		return fmt.Errorf("update: %w", err)
	}

	g.prevBtns = 0

	for i := range VEX_NUM_BTNS {
		if ebiten.IsKeyPressed(vexKeys[i]) {
			g.prevBtns |= 1 << uint(i)
		}
	}

	return nil
}

func (g *Game) Draw(screen *ebiten.Image) {
	screen.WritePixels(g.pixels)
}

func (g *Game) Layout(_, _ int) (int, int) {
	return VEX_W, VEX_H
}

func (g *Game) reloadCart(ctx context.Context) error {
	wasmBytes, err := os.ReadFile(g.cart)
	if err != nil {
		return fmt.Errorf("read: %w", err)
	}

	g.instSeq++

	module, err := g.runtime.InstantiateWithConfig(ctx, wasmBytes,
		wazero.NewModuleConfig().WithName(fmt.Sprintf("cart_%d", g.instSeq)))
	if err != nil {
		return fmt.Errorf("instantiate: %w", err)
	}

	updateFn := module.ExportedFunction("update")
	if updateFn == nil {
		module.Close(ctx)
		return fmt.Errorf("no update() export")
	}

	bootFn := module.ExportedFunction("boot")

	oldPalette := g.palette
	if err := g.initCart(ctx, bootFn); err != nil {
		g.palette = oldPalette

		module.Close(ctx)

		return err
	}

	g.module.Close(ctx)
	g.module = module
	g.updateFn = updateFn
	g.bootFn = bootFn
	g.bootCalled = true

	return nil
}

func buildEnvModule(ctx context.Context, g *Game, r wazero.Runtime) error {
	type exp struct {
		name string
		fn   any
	}

	exports := []exp{
		{"cls", func(_ context.Context, _ api.Module, color uint32) { g.cls(color) }},
		{"pset", func(_ context.Context, _ api.Module, x, y, color int32) { g.pset(x, y, uint32(color)) }},
		{"rect", func(_ context.Context, _ api.Module, x, y, w, h, color int32) { g.rect(x, y, w, h, uint32(color)) }},
		{"rectb", func(_ context.Context, _ api.Module, x, y, w, h, color int32) { g.rectb(x, y, w, h, uint32(color)) }},
		{"circ", func(_ context.Context, _ api.Module, x, y, r, color int32) { g.circ(x, y, r, uint32(color)) }},
		{"circb", func(_ context.Context, _ api.Module, x, y, r, color int32) { g.circb(x, y, r, uint32(color)) }},
		{"line", func(_ context.Context, _ api.Module, x0, y0, x1, y1, color int32) {
			g.line(x0, y0, x1, y1, uint32(color))
		}},
		{"tri", func(_ context.Context, _ api.Module, x1, y1, x2, y2, x3, y3, color int32) {
			g.tri(x1, y1, x2, y2, x3, y3, uint32(color))
		}},
		{"trib", func(_ context.Context, _ api.Module, x1, y1, x2, y2, x3, y3, color int32) {
			g.trib(x1, y1, x2, y2, x3, y3, uint32(color))
		}},
		{"blit", func(_ context.Context, m api.Module, ptr, x, y, w, h, key int32) {
			g.blit(m, uint32(ptr), x, y, w, h, uint32(key))
		}},
		{"text", func(_ context.Context, m api.Module, ptr, x, y, color int32) {
			g.text(m, uint32(ptr), x, y, uint32(color))
		}},
		{"title", func(_ context.Context, m api.Module, ptr int32) { g.title(m, uint32(ptr)) }},
		{"btn", func(_ context.Context, _ api.Module, button uint32) uint32 { return g.btn(button) }},
		{"btnp", func(_ context.Context, _ api.Module, button uint32) uint32 { return g.btnp(button) }},
		{"mx", func(_ context.Context, _ api.Module) uint32 { return g.mx() }},
		{"my", func(_ context.Context, _ api.Module) uint32 { return g.my() }},
		{"mbtn", func(_ context.Context, _ api.Module, button uint32) uint32 { return g.mbtn(button) }},
		{"pal", func(_ context.Context, _ api.Module, index, rgb int32) { g.pal(uint32(index), uint32(rgb)) }},
		{"palreset", func(_ context.Context, _ api.Module) { g.palreset() }},
		{"beep", func(_ context.Context, _ api.Module, freq uint32) { g.beep(freq) }},
	}

	b := r.NewHostModuleBuilder("env")
	for _, e := range exports {
		b.NewFunctionBuilder().WithFunc(e.fn).Export(e.name)
	}

	_, err := b.Instantiate(ctx)

	return err
}

func readCString(m api.Module, ptr uint32) string {
	mem := m.Memory()

	size := mem.Size()
	if ptr >= size {
		return ""
	}

	// One read (a zero-copy view, not a copy) of the rest of linear memory;
	// carts keep their strings short and NUL-terminated.
	data, ok := mem.Read(ptr, size-ptr)
	if !ok {
		return ""
	}

	end := 0
	for end < len(data) && data[end] != 0 {
		end++
	}

	return string(data[:end])
}

// filterStderr swaps fd 2 for a pipe and forwards everything except the
// given noise lines to the original stderr. Uses golang.org/x/sys/unix so it
// also compiles on darwin/arm64, where syscall.Dup2 doesn't exist.
func filterStderr() (restore func()) {
	orig, _ := unix.Dup(2)

	r, w, _ := os.Pipe()
	unix.Dup2(int(w.Fd()), 2)

	os.Stderr = os.NewFile(uintptr(2), "/dev/stderr")

	go func() {
		sc := bufio.NewScanner(r)
		for sc.Scan() {
			line := sc.Text()
			if strings.Contains(line, "[CAMetalLayer nextDrawable]") {
				continue
			}

			unix.Write(orig, []byte(line+"\n"))
		}
	}()

	return func() {
		unix.Dup2(orig, 2)

		os.Stderr = os.NewFile(uintptr(2), "/dev/stderr")

		w.Close()
		r.Close()
		unix.Close(orig)
	}
}
