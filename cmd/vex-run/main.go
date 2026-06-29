package main

import (
	"bufio"
	"context"
	"flag"
	"fmt"
	"os"
	"runtime"
	"strings"
	"syscall"
	"time"

	"github.com/hajimehoshi/ebiten/v2"
	"github.com/hajimehoshi/ebiten/v2/inpututil"
	"github.com/tetratelabs/wazero"
	"github.com/tetratelabs/wazero/api"
	"github.com/tetratelabs/wazero/imports/wasi_snapshot_preview1"
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
	if err := run(os.Args[1:]); err != nil {
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
	fs.Parse(args)

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
		return nil
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
		return fmt.Errorf("build env moduleule: %w", err)
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

	if err := ebiten.RunGame(game); err != nil && err != ebiten.Termination {
		return err
	}

	module.Close(ctx)

	return nil
}

type Game struct {
	pixels   []byte
	palette  [16][4]uint8
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
}

func NewGame() *Game {
	return &Game{
		pixels: make([]byte, VEX_W*VEX_H*4),
	}
}

func (g *Game) coordOK(v int32) bool {
	return v >= -VEX_COORD_MAX && v <= VEX_COORD_MAX
}

func (g *Game) palColor(index uint32) (uint8, uint8, uint8, uint8) {
	c := g.palette[index&15]
	return c[0], c[1], c[2], c[3]
}

func (g *Game) pset(x, y int32, color uint32) {
	if x < 0 || x >= VEX_W || y < 0 || y >= VEX_H {
		return
	}

	r, g_, b, a := g.palColor(color)
	i := (int(y)*VEX_W + int(x)) * 4
	g.pixels[i] = r
	g.pixels[i+1] = g_
	g.pixels[i+2] = b
	g.pixels[i+3] = a
}

func (g *Game) cls(color uint32) {
	r, g_, b, a := g.palColor(color)
	for i := 0; i < len(g.pixels); i += 4 {
		g.pixels[i] = r
		g.pixels[i+1] = g_
		g.pixels[i+2] = b
		g.pixels[i+3] = a
	}
}

func (g *Game) hline(y, x0, x1 int32, color uint32) {
	if y < 0 || y >= VEX_H {
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

	r, g_, b, a := g.palColor(color)

	base := int(y)*VEX_W*4 + int(x0)*4
	for x := x0; x <= x1; x++ {
		i := base + int(x-x0)*4
		g.pixels[i] = r
		g.pixels[i+1] = g_
		g.pixels[i+2] = b
		g.pixels[i+3] = a
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
	r, g_, b, a := g.palColor(color)

	for yy := y0; yy < y1; yy++ {
		base := (int(yy)*VEX_W + int(x0)) * 4
		for xx := x0; xx < x1; xx++ {
			i := base + int(xx-x0)*4
			g.pixels[i] = r
			g.pixels[i+1] = g_
			g.pixels[i+2] = b
			g.pixels[i+3] = a
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
	type span struct{ l, r int32 }

	rows := make(map[int32]*span)

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

			s, ok := rows[y]
			if !ok {
				rows[y] = &span{l: xi, r: xi}
			} else {
				if xi < s.l {
					s.l = xi
				}

				if xi > s.r {
					s.r = xi
				}
			}
		}
	}

	addEdge(x1, y1, x2, y2)
	addEdge(x2, y2, x3, y3)
	addEdge(x3, y3, x1, y1)

	for y, s := range rows {
		g.hline(y, s.l, s.r, color)
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
		src := data[row*w : (row+1)*w]

		col := int32(0)
		for col < w {
			for col < w && src[col] == byte(key) {
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

			g.rect(x+start, y+row, col-start, 1, uint32(run))
		}
	}
}

func (g *Game) text(m api.Module, ptr uint32, x, y int32, color uint32) {
	curX := x

	for _, ch := range []byte(readCString(m, ptr)) {
		idx := int(ch) - VEX_FONT_FIRST
		if idx < 0 || idx >= len(font8) {
			curX += 8
			continue
		}

		for yy := range int32(8) {
			rowBits := fontRows[idx*8+int(yy)]
			for xx := range int32(8) {
				if rowBits&(1<<(7-xx)) != 0 {
					g.pset(curX+xx, y+yy, color)
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
	if int(button) >= VEX_NUM_BTNS {
		return 0
	}

	if ebiten.IsKeyPressed(vexKeys[button]) {
		return 1
	}

	return 0
}

func (g *Game) btnp(button uint32) uint32 {
	if int(button) >= VEX_NUM_BTNS {
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
	x, _ := ebiten.CursorPosition()
	return uint32(max(0, min(x, VEX_W-1)))
}

func (g *Game) my() uint32 {
	_, y := ebiten.CursorPosition()
	return uint32(max(0, min(y, VEX_H-1)))
}

func (g *Game) mbtn(button uint32) uint32 {
	if int(button) < len(mouseBtns) && ebiten.IsMouseButtonPressed(mouseBtns[button]) {
		return 1
	}

	return 0
}

func (g *Game) pal(index, rgb uint32) {
	g.palette[index&15] = [4]uint8{
		uint8((rgb >> 16) & 0xFF),
		uint8((rgb >> 8) & 0xFF),
		uint8(rgb & 0xFF),
		0xFF,
	}
}

func (g *Game) palreset() {
	copy(g.palette[:], defaultPalette[:])
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
		g.palreset()
		g.cls(0)

		if g.bootFn != nil {
			_, err := g.bootFn.Call(context.Background())
			if err != nil {
				return fmt.Errorf("boot: %w", err)
			}
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

	if bootFn != nil {
		oldPalette := g.palette
		g.palreset()

		if _, err := bootFn.Call(ctx); err != nil {
			g.palette = oldPalette

			module.Close(ctx)

			return fmt.Errorf("boot: %w", err)
		}
	} else {
		g.palreset()
	}

	g.module.Close(ctx)
	g.module = module
	g.updateFn = updateFn
	g.bootFn = bootFn
	g.bootCalled = true
	g.cls(0)

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

	var buf []byte

	for i := uint32(0); ; i++ {
		b, ok := mem.ReadByte(ptr + i)
		if !ok || b == 0 {
			break
		}

		buf = append(buf, b)
	}

	return string(buf)
}

func filterStderr() (restore func()) {
	orig, _ := syscall.Dup(2)

	r, w, _ := os.Pipe()
	syscall.Dup2(int(w.Fd()), 2)

	os.Stderr = os.NewFile(uintptr(2), "/dev/stderr")

	go func() {
		sc := bufio.NewScanner(r)
		for sc.Scan() {
			line := sc.Text()
			if strings.Contains(line, "[CAMetalLayer nextDrawable]") {
				continue
			}

			syscall.Write(orig, []byte(line+"\n"))
		}
	}()

	return func() {
		syscall.Dup2(orig, 2)

		os.Stderr = os.NewFile(uintptr(2), "/dev/stderr")

		w.Close()
		r.Close()
		syscall.Close(orig)
	}
}
