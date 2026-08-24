// vex - a minimal WASM fantasy console.
//
// The console is the host: it opens a raylib window, loads a .wasm "cart",
// links a tiny drawing/input API the cart imports, and calls the cart's
// exported update() once per frame. Carts draw into a 320x180 framebuffer
// that is scaled up to the window with nearest-neighbour filtering.
//
//   usage: ./vex [-s scale] [-w] [-n frames] [-t] <cart.wasm>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <stdatomic.h>
#include <time.h>

#include "raylib.h"
#include "rlgl.h"
#include "wasm3.h"

#define VEX_W      320   // logical screen width  (keep in sync with vex.h)
#define VEX_H      180   // logical screen height (keep in sync with vex.h)
#define VEX_SCALE    3   // default window pixels per logical pixel (override with -s)
#define VEX_SCALE_MAX 20 // -s/--scale: upper clamp. A too-large scale can drive
                         // the window beyond the metal device's max texture
                         // size and trip MTLTextureDescriptorValidation.
#define VEX_WATCH_FRAMES 30 // -w/--watch: poll the cart's mtime every ~0.5s (at 60fps)

// Default SWEETIE-16 palette: 16 colors, indexed 0..15. Carts can override
// entries at runtime via pal()/palreset(); `palette` holds the live colors.
static const Color DEFAULT_PALETTE[16] = {
    { 26,  28,  44, 255}, { 93,  39,  93, 255}, {177,  62,  83, 255}, {239, 125,  87, 255},
    {255, 205, 117, 255}, {167, 240, 112, 255}, { 56, 183, 100, 255}, { 37, 113, 121, 255},
    { 41,  54, 111, 255}, { 59,  93, 201, 255}, { 65, 166, 246, 255}, {115, 239, 247, 255},
    {244, 244, 244, 255}, {148, 176, 194, 255}, { 86, 108, 134, 255}, { 51,  60,  87, 255},
};

// Live palette as packed RGBA. Byte order in memory is R,G,B,A -- identical
// to the Go host's pixel buffer, so framebuffer hashes compare across hosts.
static uint32_t g_palette[16];

static inline uint32_t pack_rgba(int r, int g, int b) {
    return (uint32_t)r | (uint32_t)g << 8 | (uint32_t)b << 16 | 0xFFu << 24;
}

// The software framebuffer. Carts rasterize here with plain stores, and the
// finished frame is uploaded to the GPU exactly once per frame. This replaces
// the previous design where every primitive call became raylib GL draw work
// inside a render texture (pset alone was a textured quad per pixel).
static uint32_t g_fb[VEX_W * VEX_H];

// 8x8 bitmap font, shared byte-for-byte with the web host (cmd/vex-web/assets/vex.js)
// so text() looks identical in both. Glyphs cover ASCII 32..127; FONT8[c - 32]
// packs one glyph as a 64-bit value where the most-significant byte is the top
// row and the most-significant bit of each byte is the left pixel.
#define VEX_FONT_FIRST 32
static const uint64_t FONT8[96] = {
    0x0000000000000000ULL, // 32  space
    0x1818181818001800ULL, // 33  !
    0x2828000000000000ULL, // 34  "
    0x28287C287C282800ULL, // 35  #
    0x103C5038147C1000ULL, // 36  $
    0x6264081020460600ULL, // 37  %
    0x304848304A443A00ULL, // 38  &
    0x1010000000000000ULL, // 39  '
    0x1020404040201000ULL, // 40  (
    0x1008040404081000ULL, // 41  )
    0x00141C3E1C140000ULL, // 42  *
    0x0010107C10100000ULL, // 43  +
    0x0000000000101020ULL, // 44  ,
    0x0000007C00000000ULL, // 45  -
    0x0000000000100000ULL, // 46  .
    0x0204081020408000ULL, // 47  /
    0x3C66666E76663C00ULL, // 48  0
    0x1838181818183C00ULL, // 49  1
    0x3C66061C30607E00ULL, // 50  2
    0x3C66061C06663C00ULL, // 51  3
    0x0C1C2C4C7E0C0C00ULL, // 52  4
    0x7E607C0606663C00ULL, // 53  5
    0x3C66607C66663C00ULL, // 54  6
    0x7E060C1830303000ULL, // 55  7
    0x3C66663C66663C00ULL, // 56  8
    0x3C66663E06663C00ULL, // 57  9
    0x0000200000200000ULL, // 58  :
    0x0000200000202040ULL, // 59  ;
    0x0C18306030180C00ULL, // 60  <
    0x00007C007C000000ULL, // 61  =
    0x6030180C18306000ULL, // 62  >
    0x3C66060C10001000ULL, // 63  ?
    0x3C666E6E6E603C00ULL, // 64  @
    0x183C66667E666600ULL, // 65  A
    0x7C66667C66667C00ULL, // 66  B
    0x3C66606060663C00ULL, // 67  C
    0x786C6666666C7800ULL, // 68  D
    0x7E60607860607E00ULL, // 69  E
    0x7E60607860606000ULL, // 70  F
    0x3C66606E66663C00ULL, // 71  G
    0x6666667E66666600ULL, // 72  H
    0x3C18181818183C00ULL, // 73  I
    0x1E0C0C0C6C6C3800ULL, // 74  J
    0x666C7878786C6600ULL, // 75  K
    0x6060606060607E00ULL, // 76  L
    0x63777F6B63636300ULL, // 77  M
    0x66767E7E6E666600ULL, // 78  N
    0x3C66666666663C00ULL, // 79  O
    0x7C66667C60606000ULL, // 80  P
    0x3C6666666A6C3A00ULL, // 81  Q
    0x7C66667C786C6600ULL, // 82  R
    0x3C66603C06663C00ULL, // 83  S
    0x7E18181818181800ULL, // 84  T
    0x6666666666663C00ULL, // 85  U
    0x66666666663C1800ULL, // 86  V
    0x6363636B7F776300ULL, // 87  W
    0x66663C3C66666600ULL, // 88  X
    0x6666663C18181800ULL, // 89  Y
    0x7E060C1830607E00ULL, // 90  Z
    0x3C30303030303C00ULL, // 91  [
    0x8040201008040200ULL, // 92  backslash
    0x3C0C0C0C0C0C3C00ULL, // 93  ]
    0x10386C0000000000ULL, // 94  ^
    0x000000000000007FULL, // 95  _
    0x2010080000000000ULL, // 96  `
    0x00003C063E663E00ULL, // 97  a
    0x60607C6666667C00ULL, // 98  b
    0x00003C6660663C00ULL, // 99  c
    0x06063E6666663E00ULL, // 100 d
    0x00003C667E603C00ULL, // 101 e
    0x1C30307830303000ULL, // 102 f
    0x00003E66663E063CULL, // 103 g
    0x60607C6666666600ULL, // 104 h
    0x1800181818183C00ULL, // 105 i
    0x060006060666663CULL, // 106 j
    0x6060666C786C6600ULL, // 107 k
    0x1818181818181E00ULL, // 108 l
    0x0000667F7F6B6300ULL, // 109 m
    0x00007C6666666600ULL, // 110 n
    0x00003C6666663C00ULL, // 111 o
    0x00007C66667C6060ULL, // 112 p
    0x00003E66663E0606ULL, // 113 q
    0x00006C7660606000ULL, // 114 r
    0x00003E603C067C00ULL, // 115 s
    0x30307C3030301C00ULL, // 116 t
    0x0000666666663E00ULL, // 117 u
    0x00006666663C1800ULL, // 118 v
    0x0000636B7F7F3600ULL, // 119 w
    0x0000663C183C6600ULL, // 120 x
    0x00006666663E0C38ULL, // 121 y
    0x00007E0C18307E00ULL, // 122 z
    0x0E18183018180E00ULL, // 123 {
    0x1818180018181800ULL, // 124 |
    0x7018180C18187000ULL, // 125 }
    0x0000000000000000ULL, // 126 ~
    0x0000000000000000ULL  // 127 DEL
};

// Row-unpacked font: g_font_rows[g*8 + row] holds one row of glyph g (bit 7
// = leftmost pixel), unpacked once at startup so text() can rasterize glyphs
// into the framebuffer with plain byte math instead of textured quads.
static uint8_t g_font_rows[96 * 8];

static void init_font(void) {
    for (int g = 0; g < 96; g++)
        for (int row = 0; row < 8; row++)
            g_font_rows[g * 8 + row] = (uint8_t)(FONT8[g] >> ((7 - row) * 8));
}

// Set after InitWindow() and cleared on teardown; also acts as the "UI is
// live" gate: with -n (headless) there is no window, and input queries must
// return zero deterministically -- mirroring the Go host's uiReady flag.
static bool g_window_open = false;

// Current framebuffer->window mapping (logical points), used to map raylib's
// window-space mouse position back into the cart's logical coordinates.
static float g_view_scale = 1.0f, g_view_ox = 0.0f, g_view_oy = 0.0f;

// Button key mappings and previous-frame state for btnp() edge detection.
static const int VEX_KEYS[6] = { KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN, KEY_Z, KEY_X };
static uint8_t g_prev_btns = 0;

static void reset_palette(void) {
    for (int i = 0; i < 16; i++)
        g_palette[i] = pack_rgba(DEFAULT_PALETTE[i].r, DEFAULT_PALETTE[i].g, DEFAULT_PALETTE[i].b);
}

// Copy a bounded, NUL-terminated string out of a cart's linear memory.
static void cart_cstr(IM3Runtime rt, const void* mem, const char* s, char* buf, int size) {
    uintptr_t end = (uintptr_t)mem + m3_GetMemorySize(rt);
    int n = 0;
    while (n < size - 1 && (uintptr_t)(s + n) < end && s[n]) { buf[n] = s[n]; n++; }
    buf[n] = '\0';
}

// Reject coordinates that are obviously absurd (millions of pixels off-screen)
// so a malformed cart can't pin raylib in its line/circle/triangle rasterizer.
// Carts are allowed to draw slightly off-screen, so the bound is generous.
#define VEX_COORD_MAX (VEX_W * 16) // ~5120 px: ~16x overscan tolerance
static inline bool coord_ok(int32_t v) {
    return v >= -VEX_COORD_MAX && v <= VEX_COORD_MAX;
}
#define COORDS_OK(x, y) (coord_ok(x) && coord_ok(y))

// ---- framebuffer rasterization helpers ------------------------------------
// All primitives write plain uint32 stores; nothing touches the GPU until
// the finished frame is uploaded once per present().

static inline void fb_pset(int32_t x, int32_t y, uint32_t c) {
    if ((uint32_t)x < VEX_W && (uint32_t)y < VEX_H)
        g_fb[(size_t)y * VEX_W + x] = c;
}

static void fb_fill(uint32_t c) {
    for (size_t i = 0; i < VEX_W * VEX_H; i++) g_fb[i] = c;
}

// Inclusive horizontal span on row y, clipped to the framebuffer.
static void fb_hline(int32_t y, int32_t x0, int32_t x1, uint32_t c) {
    if (y < 0 || y >= VEX_H) return;
    if (x0 > x1) { int32_t t = x0; x0 = x1; x1 = t; }
    if (x1 < 0 || x0 >= VEX_W) return;
    if (x0 < 0) x0 = 0;
    if (x1 >= VEX_W) x1 = VEX_W - 1;
    uint32_t* row = &g_fb[(size_t)y * VEX_W];
    for (int32_t x = x0; x <= x1; x++) row[x] = c;
}

// Clipped filled rectangle: identical clipping to the Go host's rect().
static void fb_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c) {
    if (w <= 0 || h <= 0) return;
    if (!coord_ok(x) || !coord_ok(y)) return;
    if (w > VEX_W) w = VEX_W;
    if (h > VEX_H) h = VEX_H;
    int32_t x0 = x < 0 ? 0 : x;
    int32_t y0 = y < 0 ? 0 : y;
    int32_t x1 = x + w < VEX_W ? x + w : VEX_W;
    int32_t y1 = y + h < VEX_H ? y + h : VEX_H;
    for (int32_t yy = y0; yy < y1; yy++) {
        uint32_t* row = &g_fb[(size_t)yy * VEX_W];
        for (int32_t xx = x0; xx < x1; xx++) row[xx] = c;
    }
}

// ---- host API: functions the cart imports from module "env" --------------

m3ApiRawFunction(host_cls) {
    m3ApiGetArg(int32_t, color)
    fb_fill(g_palette[(unsigned)(color) & 15]);
    m3ApiSuccess();
}

m3ApiRawFunction(host_pset) {
    m3ApiGetArg(int32_t, x)
    m3ApiGetArg(int32_t, y)
    m3ApiGetArg(int32_t, color)
    fb_pset(x, y, g_palette[(unsigned)(color) & 15]);
    m3ApiSuccess();
}

m3ApiRawFunction(host_rect) {
    m3ApiGetArg(int32_t, x)
    m3ApiGetArg(int32_t, y)
    m3ApiGetArg(int32_t, w)
    m3ApiGetArg(int32_t, h)
    m3ApiGetArg(int32_t, color)
    fb_rect(x, y, w, h, g_palette[(unsigned)(color) & 15]);
    m3ApiSuccess();
}

m3ApiRawFunction(host_rectb) {
    m3ApiGetArg(int32_t, x)
    m3ApiGetArg(int32_t, y)
    m3ApiGetArg(int32_t, w)
    m3ApiGetArg(int32_t, h)
    m3ApiGetArg(int32_t, color)
    // Four edges decomposed exactly like the Go and JS hosts, so corners and
    // degenerate sizes land on the same pixels everywhere.
    uint32_t c = g_palette[(unsigned)(color) & 15];
    fb_rect(x, y, w, 1, c);
    fb_rect(x, y + h - 1, w, 1, c);
    fb_rect(x, y + 1, 1, h - 2, c);
    if (w > 1) fb_rect(x + w - 1, y + 1, 1, h - 2, c);
    m3ApiSuccess();
}

// Draw a horizontal run of pixels on row y from x0..x1 inclusive -- the same
// hline() the Go and JS hosts use, so circ()/circb()/tri() land on identical
// pixels everywhere (see fb_hline above).

m3ApiRawFunction(host_circ) {
    m3ApiGetArg(int32_t, x)
    m3ApiGetArg(int32_t, y)
    m3ApiGetArg(int32_t, r)
    m3ApiGetArg(int32_t, color)
    if (r < 0) r = 0;
    if (r > VEX_COORD_MAX) r = VEX_COORD_MAX;
    if (!COORDS_OK(x, y)) m3ApiSuccess();
    // Midpoint circle algorithm, filled with horizontal spans -- byte-for-byte
    // the circ() of the Go and JS hosts.
    uint32_t c = g_palette[(unsigned)(color) & 15];
    int32_t rx = r, ry = 0, err = 0;
    while (rx >= ry) {
        fb_hline(y + ry, x - rx, x + rx, c);
        fb_hline(y + rx, x - ry, x + ry, c);
        fb_hline(y - ry, x - rx, x + rx, c);
        fb_hline(y - rx, x - ry, x + ry, c);
        ry++;
        if (err <= 0) {
            err += 2 * ry + 1;
        } else {
            rx--;
            err += 2 * (ry - rx) + 1;
        }
    }
    m3ApiSuccess();
}

m3ApiRawFunction(host_circb) {
    m3ApiGetArg(int32_t, x)
    m3ApiGetArg(int32_t, y)
    m3ApiGetArg(int32_t, r)
    m3ApiGetArg(int32_t, color)
    if (r < 0) r = 0;
    if (r > VEX_COORD_MAX) r = VEX_COORD_MAX;
    if (!COORDS_OK(x, y)) m3ApiSuccess();
    // Midpoint circle outline, 8-way symmetric single pixels -- matching the
    // Go and JS hosts.
    uint32_t c = g_palette[(unsigned)(color) & 15];
    int32_t rx = r, ry = 0, err = 0;
    while (rx >= ry) {
        fb_pset(x + rx, y + ry, c);
        fb_pset(x + ry, y + rx, c);
        fb_pset(x - ry, y + rx, c);
        fb_pset(x - rx, y + ry, c);
        fb_pset(x - rx, y - ry, c);
        fb_pset(x - ry, y - rx, c);
        fb_pset(x + ry, y - rx, c);
        fb_pset(x + rx, y - ry, c);
        ry++;
        if (err <= 0) {
            err += 2 * ry + 1;
        } else {
            rx--;
            err += 2 * (ry - rx) + 1;
        }
    }
    m3ApiSuccess();
}

// Bresenham line -- byte-for-byte the line() of the Go and JS hosts, so all
// three rasterize identical pixels.
static void host_bresenham(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t c) {
    int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int32_t sx = x0 < x1 ? 1 : -1;
    int32_t dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int32_t sy = y0 < y1 ? 1 : -1;
    int32_t err = dx - dy;
    for (;;) {
        fb_pset(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int32_t e2 = err * 2;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

m3ApiRawFunction(host_line) {
    m3ApiGetArg(int32_t, x0)
    m3ApiGetArg(int32_t, y0)
    m3ApiGetArg(int32_t, x1)
    m3ApiGetArg(int32_t, y1)
    m3ApiGetArg(int32_t, color)
    if (!COORDS_OK(x0, y0) || !COORDS_OK(x1, y1)) m3ApiSuccess();
    host_bresenham(x0, y0, x1, y1, g_palette[(unsigned)(color) & 15]);
    m3ApiSuccess();
}

// Integer scanline triangle fill -- byte-for-byte the tri() of the Go host:
// per row, track the floor'd leftmost/rightmost edge crossing and draw one
// hline span. Independent of vertex order or winding, so no normalization
// step is needed.
#define TRI_MAX_ROWS (2 * VEX_COORD_MAX + 1) // vertices are COORDS_OK-bounded
static int32_t g_tri_l[TRI_MAX_ROWS];
static int32_t g_tri_r[TRI_MAX_ROWS];

static void tri_add_edge(int32_t ax, int32_t ay, int32_t bx, int32_t by,
                         int32_t ymin, int32_t* l, int32_t* r) {
    if (ay == by) return; // horizontal edge: its endpoints are covered by the others
    // double math to match the Go and JS hosts bit-for-bit at half-pixel
    // boundaries (a float here would round differently in rare cases).
    double slope = (double)(bx - ax) / (double)(by - ay);
    int32_t ys = ay < by ? ay : by;
    int32_t ye = ay < by ? by : ay;
    for (int32_t y = ys; y <= ye; y++) {
        double xf = (double)ax + (double)(y - ay) * slope;
        int32_t xi = (int32_t)xf;
        if (xf < 0.0 && xf - (double)xi > 0.0) xi--; // true floor
        int32_t i = y - ymin;
        if (xi < l[i]) l[i] = xi;
        if (xi > r[i]) r[i] = xi;
    }
}

m3ApiRawFunction(host_tri) {
    m3ApiGetArg(int32_t, x1)
    m3ApiGetArg(int32_t, y1)
    m3ApiGetArg(int32_t, x2)
    m3ApiGetArg(int32_t, y2)
    m3ApiGetArg(int32_t, x3)
    m3ApiGetArg(int32_t, y3)
    m3ApiGetArg(int32_t, color)
    if (!COORDS_OK(x1, y1) || !COORDS_OK(x2, y2) || !COORDS_OK(x3, y3)) m3ApiSuccess();
    int32_t ymin = y1 < y2 ? (y1 < y3 ? y1 : y3) : (y2 < y3 ? y2 : y3);
    int32_t ymax = y1 > y2 ? (y1 > y3 ? y1 : y3) : (y2 > y3 ? y2 : y3);
    int64_t n64 = (int64_t)ymax - ymin + 1;
    if (n64 <= 0 || n64 > TRI_MAX_ROWS) m3ApiSuccess(); // guard hostile spans
    int n = (int)n64;

    const int32_t INF32 = 1 << 30;
    for (int i = 0; i < n; i++) { g_tri_l[i] = INF32; g_tri_r[i] = -INF32; }
    tri_add_edge(x1, y1, x2, y2, ymin, g_tri_l, g_tri_r);
    tri_add_edge(x2, y2, x3, y3, ymin, g_tri_l, g_tri_r);
    tri_add_edge(x3, y3, x1, y1, ymin, g_tri_l, g_tri_r);

    uint32_t c = g_palette[(unsigned)(color) & 15];
    for (int i = 0; i < n; i++) {
        if (g_tri_l[i] <= g_tri_r[i])
            fb_hline(ymin + i, g_tri_l[i], g_tri_r[i], c);
    }
    m3ApiSuccess();
}

m3ApiRawFunction(host_trib) {
    m3ApiGetArg(int32_t, x1)
    m3ApiGetArg(int32_t, y1)
    m3ApiGetArg(int32_t, x2)
    m3ApiGetArg(int32_t, y2)
    m3ApiGetArg(int32_t, x3)
    m3ApiGetArg(int32_t, y3)
    m3ApiGetArg(int32_t, color)
    // Three Bresenham edges, matching trib() of the Go and JS hosts.
    if (!COORDS_OK(x1, y1) || !COORDS_OK(x2, y2) || !COORDS_OK(x3, y3)) m3ApiSuccess();
    uint32_t c = g_palette[(unsigned)(color) & 15];
    host_bresenham(x1, y1, x2, y2, c);
    host_bresenham(x2, y2, x3, y3, c);
    host_bresenham(x3, y3, x1, y1, c);
    m3ApiSuccess();
}

// blit(data, x, y, w, h, key): draw a w*h bitmap of palette indices (one byte
// per pixel) with its top-left at (x, y). Pixels equal to key are skipped, so a
// key outside 0..15 (e.g. -1) draws every pixel.
m3ApiRawFunction(host_blit) {
    m3ApiGetArgMem(const uint8_t*, data)
    m3ApiGetArg(int32_t, x)
    m3ApiGetArg(int32_t, y)
    m3ApiGetArg(int32_t, w)
    m3ApiGetArg(int32_t, h)
    m3ApiGetArg(int32_t, key)
    if (w <= 0 || h <= 0) m3ApiSuccess();
    if (!COORDS_OK(x, y)) m3ApiSuccess();
    // Cap w and h to the framebuffer so a malformed cart can't make wasm3 walk
    // a multi-gigabyte memory range inside m3ApiCheckMem (DoS via stalled
    // validation) or stall the host drawing millions of pixels per frame.
    if (w > VEX_W) w = VEX_W;
    if (h > VEX_H) h = VEX_H;
    if ((size_t)w > (size_t)-1 / (size_t)h) m3ApiSuccess();
    m3ApiCheckMem(data, (size_t)w * (size_t)h);
    // Batch horizontal runs of equal, non-key pixels into one clipped span
    // fill per run. For a typical sprite (palette entries repeated per row)
    // this is several times fewer inner-loop iterations than per-pixel
    // stores; for key=-1 ("draw every pixel") each row collapses to a single
    // contiguous span. Rows outside the framebuffer are skipped and x spans
    // are clipped, matching the Go host's blit().
    for (int32_t row = 0; row < h; row++) {
        int32_t yy = y + row;
        if (yy < 0 || yy >= VEX_H) continue;
        uint32_t* dst = &g_fb[(size_t)yy * VEX_W];
        const uint8_t* src = data + (size_t)row * w;
        int32_t col = 0;
        while (col < w) {
            while (col < w && (uint32_t)src[col] == (uint32_t)key) col++; // transparent run
            if (col >= w) break;
            int32_t start = col;
            uint8_t run = src[col];
            while (col < w && src[col] == run) col++; // solid run
            int32_t sx = x + start;
            int32_t ex = x + col;
            if (sx < 0) sx = 0;
            if (ex > VEX_W) ex = VEX_W;
            uint32_t v = g_palette[run & 15];
            for (; sx < ex; sx++) dst[sx] = v;
        }
    }
    m3ApiSuccess();
}

m3ApiRawFunction(host_text) {
    m3ApiGetArgMem(const char*, s)
    m3ApiGetArg(int32_t, x)
    m3ApiGetArg(int32_t, y)
    m3ApiGetArg(int32_t, color)
    m3ApiCheckMem(s, 1);
    char buf[128];
    cart_cstr(runtime, _mem, s, buf, sizeof(buf));

    // Rasterize glyphs straight into the framebuffer from the unpacked font
    // rows. Unsupported characters advance a full 8px slot, matching the Go
    // and JS hosts.
    uint32_t c = g_palette[(unsigned)(color) & 15];
    for (const unsigned char* p = (const unsigned char*)buf; *p; ++p, x += 8) {
        int idx = *p - VEX_FONT_FIRST;
        if (idx < 0 || idx >= 96) continue;
        for (int row = 0; row < 8; row++) {
            unsigned bits = g_font_rows[idx * 8 + row];
            if (!bits) continue;
            int32_t py = y + row;
            if (py < 0 || py >= VEX_H) continue;
            uint32_t* dst = &g_fb[(size_t)py * VEX_W];
            for (int col = 0; col < 8; col++) {
                if (bits & (0x80u >> col)) {
                    int32_t px = x + col;
                    if ((uint32_t)px < VEX_W) dst[px] = c;
                }
            }
        }
    }
    m3ApiSuccess();
}

// title(s): set the window title from a cart string. No-op before the window
// exists (headless -n mode).
m3ApiRawFunction(host_title) {
    m3ApiGetArgMem(const char*, s)
    m3ApiCheckMem(s, 1);
    if (!g_window_open) m3ApiSuccess();
    char buf[128];
    cart_cstr(runtime, _mem, s, buf, sizeof(buf));
    SetWindowTitle(buf);
    m3ApiSuccess();
}

// btn(button) -> held? Buttons: 0 left, 1 right, 2 up, 3 down, 4 Z, 5 X.
// Without a window (headless -n mode) every input reads as released, which
// keeps runs deterministic and mirrors the Go host's uiReady gate.
m3ApiRawFunction(host_btn) {
    m3ApiReturnType(int32_t)
    m3ApiGetArg(int32_t, button)
    int held = g_window_open && button >= 0 && button < 6
        ? IsKeyDown(VEX_KEYS[button]) : 0;
    m3ApiReturn(held);
}

// btnp(button) -> just pressed this frame? Same buttons as btn().
m3ApiRawFunction(host_btnp) {
    m3ApiReturnType(int32_t)
    m3ApiGetArg(int32_t, button)
    int held = g_window_open && button >= 0 && button < 6
        ? IsKeyDown(VEX_KEYS[button]) : 0;
    int prev = (g_prev_btns >> button) & 1;
    m3ApiReturn(held && !prev);
}

// Mouse position, mapped from window space into logical screen coordinates
// and clamped to the framebuffer (0..W-1 / 0..H-1), matching the Go host and
// the documented API range.
static inline int32_t clampi32(int32_t v, int32_t lo, int32_t hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

m3ApiRawFunction(host_mx) {
    m3ApiReturnType(int32_t)
    if (!g_window_open) m3ApiReturn(0);
    int32_t mx = (int32_t)((GetMouseX() - g_view_ox) / g_view_scale);
    m3ApiReturn(clampi32(mx, 0, VEX_W - 1));
}

m3ApiRawFunction(host_my) {
    m3ApiReturnType(int32_t)
    if (!g_window_open) m3ApiReturn(0);
    int32_t my = (int32_t)((GetMouseY() - g_view_oy) / g_view_scale);
    m3ApiReturn(clampi32(my, 0, VEX_H - 1));
}

// mbtn(button) -> held? 0 left, 1 right, 2 middle (raylib button indices).
m3ApiRawFunction(host_mbtn) {
    m3ApiReturnType(int32_t)
    m3ApiGetArg(int32_t, button)
    // raylib only defines mouse buttons 0..2; anything else reads past its
    // internal bool[3] state array.
    int held = g_window_open && button >= 0 && button < 3
        ? IsMouseButtonDown(button) : 0;
    m3ApiReturn(held);
}

// pal(index, rgb): override one palette entry (index 0..15) with a packed
// 0xRRGGBB color.
m3ApiRawFunction(host_pal) {
    m3ApiGetArg(int32_t, index)
    m3ApiGetArg(int32_t, rgb)
    g_palette[(unsigned)index & 15] =
        pack_rgba((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
    m3ApiSuccess();
}

// palreset(): restore the default palette.
m3ApiRawFunction(host_palreset) {
    reset_palette();
    m3ApiSuccess();
}

// ---- audio (tone/noise/vol/apos) ------------------------------------------
// Four generic voices mixed continuously into one streaming AudioStream via
// raylib's audio-callback API. The old per-channel Sound-rewrite model could
// not express a sustained note (a pre-rendered buffer has no "until"), so
// events now publish small voice descriptors that the audio thread's mixer
// consumes; the cart thread never touches the mix path directly.
//
// Synchronization: cart thread and audio callback run on different threads.
// Each channel's event is published through a seqlock (an atomic sequence
// counter made odd while the plain-data event fields are written); the
// callback retries its read if it caught a half-published event. Volume is a
// single atomic int per channel, applied live so volume slides affect the
// currently sounding voice. g_apos -- the audio clock apos() returns -- is
// owned by the callback and only read elsewhere.
//
// Semantics shared with the Go/JS hosts:
//   channels clamp to 0..3; freq <= 0 silences, freq > 20000 clamps;
//   ms > 0   decaying tone (exponential to -48 dB) for min(ms, 5000) ms
//   ms == 0  sustain until the next event on the channel
//   ms < 0   legacy flat ~100 ms blip
// A new event replaces a busy voice at phase 0 (click-free).
#define VEX_TONE_CHANNELS 4 // keep in sync with vex.h / vex.zig / README
#define TONE_MAX_MS       5000
#define TONE_LEGACY_MS    100
#define TONE_SUSTAIN      UINT64_MAX // end of a sustained voice

typedef struct {
    _Atomic uint8_t  kind;       // 0 square, 1 noise
    _Atomic uint32_t half;       // samples per half-period / LFSR step
    _Atomic uint32_t decay_len;  // envelope length in frames, 0 = flat
    _Atomic uint64_t start;      // absolute frame of onset (event version)
    _Atomic uint64_t end;        // absolute frame of stop; TONE_SUSTAIN = never
} VoiceEvent;

typedef struct {
    // published event (seqlock-protected)
    VoiceEvent ev;
    // callback-private runtime state
    bool     seen;
    uint64_t last_start; // identifies the event the runtime state belongs to
    uint8_t  kind;       // snapshot of ev.kind for the running voice
    uint32_t half;       // snapshot of ev.half
    uint64_t end;        // snapshot of ev.end
    uint32_t phase;      // position within current half-period / LFSR interval
    uint16_t lfsr;       // 16-bit noise state
    float    env;        // envelope amplitude
    float    step;       // per-sample multiplier
} MixVoice;

static MixVoice g_mix[VEX_TONE_CHANNELS];
static _Atomic uint64_t g_seq;                // seqlock: odd while publishing
static _Atomic int      g_vol[VEX_TONE_CHANNELS]; // attenuation 0..VOL_MAX, 0 = unity
#define VOL_MAX 64 // tracker-native vol() range top; VOL_MAX == unity
static _Atomic uint64_t g_apos;               // frames produced by the mixer
static AudioStream g_stream;
static bool        g_stream_ready;
static bool        g_audio_ready;

// Discover the device sample rate via a 1-frame placeholder Sound (raylib
// doesn't expose a public getter). Any rate we pass to LoadSoundFromWave is
// resampled to the device rate anyway, so the placeholder's rate is irrelevant.
static int tone_device_rate(void) {
    static int cached = 0;
    if (cached > 0) return cached;
    float ph[2] = {0.0f, 0.0f};
    Wave pw = {
        .frameCount = 1,
        .sampleRate = 44100,
        .sampleSize = 32,
        .channels = 2,
        .data = ph,
    };
    Sound ps = LoadSoundFromWave(pw);
    int rate = (int)ps.stream.sampleRate;
    UnloadSound(ps);
    if (rate <= 0) rate = 44100;
    cached = rate;
    return rate;
}

static void mix_callback(void* buffer, unsigned int frames);

// ensure_stream lazily opens the streaming AudioStream on the first event so
// headless runs that never make sound don't touch the device.
static void ensure_audio_stream(void) {
    if (g_stream_ready || !g_audio_ready) return;
    int rate = tone_device_rate();
    g_stream = LoadAudioStream((unsigned int)rate, 32, 2);
    SetAudioStreamCallback(g_stream, mix_callback);
    PlayAudioStream(g_stream);
    g_stream_ready = true;
}

static void tone_cleanup(void) {
    if (g_stream_ready) {
        StopAudioStream(g_stream);
        UnloadAudioStream(g_stream);
        g_stream = (AudioStream){0};
        g_stream_ready = false;
    }
    if (g_audio_ready) {
        g_audio_ready = false;
        CloseAudioDevice();
    }
}

// publish installs an event for channel ch under the seqlock. Runs on the
// cart thread.
static void publish_event(int ch, const VoiceEvent* ev) {
    uint64_t s = atomic_load_explicit(&g_seq, memory_order_relaxed);
    atomic_store_explicit(&g_seq, s + 1, memory_order_relaxed); // odd: writing

    atomic_store_explicit(&g_mix[ch].ev.kind,      ev->kind,      memory_order_relaxed);
    atomic_store_explicit(&g_mix[ch].ev.half,      ev->half,      memory_order_relaxed);
    atomic_store_explicit(&g_mix[ch].ev.decay_len, ev->decay_len, memory_order_relaxed);
    atomic_store_explicit(&g_mix[ch].ev.start,     ev->start,     memory_order_relaxed);
    atomic_store_explicit(&g_mix[ch].ev.end,       ev->end,       memory_order_relaxed);

    atomic_store_explicit(&g_seq, s + 2, memory_order_release); // even: stable
}

// schedule is the shared body of tone() and noise(): clamp arguments and
// publish an event. Runs on the cart thread.
static void schedule_voice(int ch_in, bool is_noise, int32_t freq, int32_t ms) {
    if (!g_audio_ready) return;

    int ch = ch_in < 0 ? 0 : ch_in;
    if (ch >= VEX_TONE_CHANNELS) ch = VEX_TONE_CHANNELS - 1;

    ensure_audio_stream();

    VoiceEvent ev = {0};
    uint64_t now = atomic_load_explicit(&g_apos, memory_order_relaxed);
    ev.start = now;

    // freq <= 0 silences the channel right away.
    if (freq < 1) {
        ev.end = now;
        publish_event(ch, &ev);
        return;
    }

    ev.kind = is_noise ? 1 : 0;
    if (freq > 20000) freq = 20000;
    int rate = tone_device_rate();
    ev.half = (uint32_t)(rate / (2 * freq));
    if (ev.half < 1) ev.half = 1;

    if (ms > 0) {
        int dur_ms = ms > TONE_MAX_MS ? TONE_MAX_MS : ms;
        uint64_t len = (uint64_t)rate * (uint64_t)dur_ms / 1000;
        if (len < 1) len = 1;
        ev.decay_len = (uint32_t)len;
        ev.end = now + len;
    } else if (ms == 0) {
        ev.end = TONE_SUSTAIN;
    } else {
        ev.end = now + (uint64_t)rate * TONE_LEGACY_MS / 1000;
    }

    publish_event(ch, &ev);
}

m3ApiRawFunction(host_tone) {
    m3ApiGetArg(int32_t, channel)
    m3ApiGetArg(int32_t, freq)
    m3ApiGetArg(int32_t, ms)
    schedule_voice(channel, false, freq, ms);
    m3ApiSuccess();
}

m3ApiRawFunction(host_noise) {
    m3ApiGetArg(int32_t, channel)
    m3ApiGetArg(int32_t, freq)
    m3ApiGetArg(int32_t, ms)
    schedule_voice(channel, true, freq, ms);
    m3ApiSuccess();
}

// vol(channel, v): per-channel linear gain, v clamped to 0..VOL_MAX with
// VOL_MAX == unity (the zero value of the attenuation store means "no
// attenuation"). Applied live: scales whatever the channel is playing.
m3ApiRawFunction(host_vol) {
    m3ApiGetArg(int32_t, channel)
    m3ApiGetArg(int32_t, v)
    if (!g_audio_ready) m3ApiSuccess();

    int ch = channel < 0 ? 0 : channel;
    if (ch >= VEX_TONE_CHANNELS) ch = VEX_TONE_CHANNELS - 1;
    int att = VOL_MAX - (v < 0 ? 0 : v > VOL_MAX ? VOL_MAX : v);
    atomic_store_explicit(&g_vol[ch], att, memory_order_relaxed);
    m3ApiSuccess();
}

// apos(): sample frames produced by the output stream since console start
// (48 kHz count; wraps after ~12.4 hours). Safe in headless runs: returns 0.
m3ApiRawFunction(host_apos) {
    m3ApiReturnType(int32_t)
    uint64_t pos = atomic_load_explicit(&g_apos, memory_order_relaxed);
    m3ApiReturn((int32_t)(uint32_t)pos);
}

// mix_callback runs on the audio thread: pull the latest events, then render
// `frames` interleaved stereo f32 samples as the sum of all active voices,
// soft-clipped at the 16-bit range like the Go host's mixer.
static void mix_callback(void* buffer, unsigned int frames) {
    float* out = buffer;

    // Poll every channel's event slot; a changed start marks a new event.
    for (int c = 0; c < VEX_TONE_CHANNELS; c++) {
        MixVoice* v = &g_mix[c];
        uint64_t seq = atomic_load_explicit(&g_seq, memory_order_acquire);
        if (seq & 1) continue; // publisher mid-write: keep previous voice

        VoiceEvent ev;
        ev.kind      = atomic_load_explicit(&v->ev.kind,      memory_order_relaxed);
        ev.half      = atomic_load_explicit(&v->ev.half,      memory_order_relaxed);
        ev.decay_len = atomic_load_explicit(&v->ev.decay_len, memory_order_relaxed);
        ev.start     = atomic_load_explicit(&v->ev.start,     memory_order_relaxed);
        ev.end       = atomic_load_explicit(&v->ev.end,       memory_order_acquire);

        if (atomic_load_explicit(&g_seq, memory_order_acquire) != seq) continue;

        if (!v->seen || ev.start != v->last_start) {
            v->seen = true;
            v->last_start = ev.start;
            v->kind = ev.kind;
            v->half = ev.half < 1 ? 1 : ev.half;
            v->end = ev.end;
            v->phase = 0;
            v->lfsr = 0xACE1;
            v->env = 1.0f;
            v->step = ev.decay_len > 0
                ? expf(-5.545f / (float)ev.decay_len) // ~1/256 at the end
                : 1.0f;
        }
    }

    unsigned int f = 0;
    uint64_t pos = atomic_load_explicit(&g_apos, memory_order_relaxed);
    while (f < frames) {
        float acc = 0.0f;
        for (int c = 0; c < VEX_TONE_CHANNELS; c++) {
            MixVoice* v = &g_mix[c];
            if (!v->seen || pos >= v->end) continue;

            float gain = (float)(VOL_MAX - atomic_load_explicit(
                             &g_vol[c], memory_order_relaxed)) /
                         (float)VOL_MAX;
            if (gain <= 0.0f) {
                v->env *= v->step;
                v->phase++;
                continue;
            }

            float s;
            if (v->kind == 1) {
                if (v->phase >= v->half) {
                    v->phase = 0;
                    // XNOR feedback over taps 15/13 (Game Boy-style noise)
                    uint16_t fb = (uint16_t)(1u - (((v->lfsr >> 15)
                                                   ^ (v->lfsr >> 13)) & 1));
                    v->lfsr = (uint16_t)(v->lfsr << 1 | fb);
                }
                s = (v->lfsr & 1) ? 8000.0f : -8000.0f;
            } else {
                if ((v->phase / v->half) & 1) s = -8000.0f; else s = 8000.0f;
            }

            // Mix in the s16 domain so the soft-clip constants match the
            // Go host's exactly; normalize once at the write below.
            acc += s * v->env * gain;
            v->env *= v->step;
            v->phase++;
        }
        pos++;

        // Soft clip the sum into the 16-bit range (linear below the knee),
        // matching the Go host's clipKnee/top constants.
        const float knee = 24000.0f, top = 32767.0f;
        if (acc > knee) {
            acc = knee + (top - knee) * tanhf((acc - knee) / (top - knee));
        } else if (acc < -knee) {
            acc = -knee + (-top + knee) * tanhf((acc + knee) / (top - knee));
        }

        out[f * 2] = acc / 32768.0f;
        out[f * 2 + 1] = acc / 32768.0f;
        f++;
    }
    atomic_store_explicit(&g_apos, pos, memory_order_release);
}

static M3Result link_host(IM3Module mod) {
    const char* m = "env";
    // Linking a function the cart doesn't import returns functionLookupFailed,
    // which is harmless: carts only import the API they actually use. Any
    // other failure (e.g. a signature mismatch on an import we do have) is
    // reported to the caller instead of being silently masked.
    M3Result first_err = m3Err_none;
    #define LINK(name, sig, fn) do {                                      \
        M3Result r_ = m3_LinkRawFunction(mod, m, name, sig, fn);          \
        if (r_ && r_ != m3Err_functionLookupFailed && !first_err) first_err = r_; \
    } while (0)
    LINK("cls",   "v(i)",     &host_cls);
    LINK("pset",  "v(iii)",   &host_pset);
    LINK("rect",  "v(iiiii)", &host_rect);
    LINK("rectb", "v(iiiii)", &host_rectb);
    LINK("circ",  "v(iiii)",  &host_circ);
    LINK("circb", "v(iiii)",  &host_circb);
    LINK("line",  "v(iiiii)", &host_line);
    LINK("tri",   "v(iiiiiii)", &host_tri);
    LINK("trib",  "v(iiiiiii)", &host_trib);
    LINK("blit",  "v(*iiiii)", &host_blit);
    LINK("text",     "v(*iii)",  &host_text);
    LINK("title",    "v(*)",     &host_title);
    LINK("btn",      "i(i)",     &host_btn);
    LINK("btnp",     "i(i)",     &host_btnp);
    LINK("mx",       "i()",      &host_mx);
    LINK("my",       "i()",      &host_my);
    LINK("mbtn",     "i(i)",     &host_mbtn);
    LINK("pal",      "v(ii)",    &host_pal);
    LINK("palreset", "v()",      &host_palreset);
    LINK("tone",     "v(iii)",   &host_tone);
    LINK("noise",    "v(iii)",   &host_noise);
    LINK("vol",      "v(ii)",    &host_vol);
    LINK("apos",     "i()",      &host_apos);
    #undef LINK
    return first_err;
}

static void die(IM3Runtime rt, const char* what, M3Result err) {
    if (rt && err) {
        M3ErrorInfo info;
        m3_GetErrorInfo(rt, &info);
        fprintf(stderr, "vex: %s: %s (%s)\n", what, err, info.message ? info.message : "");
    } else {
        fprintf(stderr, "vex: %s%s%s\n", what, err ? ": " : "", err ? (const char*)err : "");
    }
    // Close raylib's window before exit so its GL teardown doesn't print a
    // noisy "context still bound" warning. Guarded because die() may be called
    // before InitWindow() (e.g. from a wasm load error in main).
    if (g_window_open) {
        g_window_open = false;
        tone_cleanup();
        CloseWindow();
    }
    exit(1);
}

// Read a whole file into a freshly malloc'd buffer. Returns NULL on failure
// (so a failed hot-reload can be reported without killing the program).
static uint8_t* read_file(const char* path, size_t* out_len) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n <= 0) { fclose(f); return NULL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    uint8_t* buf = malloc((size_t)n);
    if (!buf || fread(buf, 1, n, f) != (size_t)n) { free(buf); fclose(f); return NULL; }
    fclose(f);
    *out_len = (size_t)n;
    return buf;
}

// A loaded cart: its runtime, the entry points, and the wasm bytes wasm3
// references for the runtime's lifetime (so they must outlive it).
typedef struct {
    IM3Runtime  rt;
    IM3Function f_boot;   // optional, may be NULL
    IM3Function f_update;
    uint8_t*    wasm;
} Cart;

// Load a cart from disk into a fresh runtime: parse, link the host API, and
// resolve the entry points. Returns true on success (filling *out); on failure
// prints why, frees anything it allocated, and leaves *out untouched -- so the
// caller can keep running the previous cart.
static bool load_cart(IM3Environment env, const char* path, Cart* out) {
    size_t wasm_len;
    uint8_t* wasm = read_file(path, &wasm_len);
    if (!wasm) { fprintf(stderr, "vex: cannot read %s\n", path); return false; }

    IM3Runtime rt = m3_NewRuntime(env, 64 * 1024 /* stack */, NULL);
    if (!rt) { fprintf(stderr, "vex: cannot create runtime\n"); free(wasm); return false; }
    IM3Module mod;
    M3Result err = m3_ParseModule(env, &mod, wasm, wasm_len);
    if (err) { fprintf(stderr, "vex: parse: %s\n", err); m3_FreeRuntime(rt); free(wasm); return false; }
    err = m3_LoadModule(rt, mod);
    if (err) { fprintf(stderr, "vex: load: %s\n", err); m3_FreeModule(mod); m3_FreeRuntime(rt); free(wasm); return false; }
    err = link_host(mod);
    if (err) {
        M3ErrorInfo info;
        m3_GetErrorInfo(rt, &info);
        fprintf(stderr, "vex: link: %s (%s)\n", err, info.message ? info.message : "");
        m3_FreeRuntime(rt); free(wasm); return false;
    }

    // Resolving update() compiles it, which is where wasm3 first notices a
    // missing import -- a cart calling a host function this vex doesn't link
    // (e.g. an API that was removed/renamed, or is newer than this build).
    // Distinguish that from a cart that genuinely lacks an update() export, so
    // the message points at the real cause instead of blaming the export.
    // boot() is optional: missing export is fine, but a missing-import-style
    // error (which can happen for a malformed cart) is reported the same way as
    // for update() -- silently zeroing f_boot there would mask real bugs.
    IM3Function f_boot = NULL, f_update = NULL;
    err = m3_FindFunction(&f_boot, rt, "boot");
    if (err && err != m3Err_functionLookupFailed) {
        if (err == m3Err_functionImportMissing) {
            M3ErrorInfo info;
            m3_GetErrorInfo(rt, &info);
            fprintf(stderr, "vex: cart needs a host function this vex doesn't provide%s%s\n",
                    (info.message && info.message[0]) ? ": " : "",
                    (info.message && info.message[0]) ? info.message : "");
        } else {
            fprintf(stderr, "vex: cannot load cart: %s\n", err);
        }
        m3_FreeRuntime(rt); free(wasm); return false;
    }
    err = m3_FindFunction(&f_update, rt, "update");
    if (err == m3Err_functionImportMissing) {
        M3ErrorInfo info;
        m3_GetErrorInfo(rt, &info);
        fprintf(stderr, "vex: cart needs a host function this vex doesn't provide%s%s\n",
                (info.message && info.message[0]) ? ": " : "",
                (info.message && info.message[0]) ? info.message : "");
        m3_FreeRuntime(rt); free(wasm); return false;
    }
    if (err && err != m3Err_functionLookupFailed) {
        fprintf(stderr, "vex: cannot load cart: %s\n", err);
        m3_FreeRuntime(rt); free(wasm); return false;
    }
    if (!f_update) {
        fprintf(stderr, "vex: cart has no update() export\n");
        m3_FreeRuntime(rt); free(wasm); return false;
    }

    *out = (Cart){ .rt = rt, .f_boot = f_boot, .f_update = f_update, .wasm = wasm };
    return true;
}

// Reload the cart from disk into a fresh runtime, swapping it in only if it
// loads cleanly -- a bad or half-written file leaves the running cart untouched.
// Resets the palette and re-runs boot(), matching a fresh start. Returns true if
// the cart was replaced.
static bool reload_cart(IM3Environment env, const char* path, Cart* cart) {
    Cart fresh;
    if (!load_cart(env, path, &fresh)) return false;

    // Match the initial start order: reset the palette to defaults BEFORE the
    // new boot() runs, so boot()'s pal()/title() calls land on a known
    // baseline. Doing reset_palette() *after* boot() would erase any palette
    // overrides the cart's boot() made (e.g. vex.pal(0, ...)) -- which is the
    // original bug this comment now documents.
    uint32_t old_pal[16];
    memcpy(old_pal, g_palette, sizeof(g_palette));
    reset_palette();

    // Try boot() on the fresh cart BEFORE swapping it in: if it traps, the
    // old cart (which is still running) stays untouched. Doing it after the
    // swap would force die() on any boot-time runtime error and bring down
    // the host over a bad edit.
    if (fresh.f_boot) {
        M3Result err = m3_CallV(fresh.f_boot);
        if (err) {
            memcpy(g_palette, old_pal, sizeof(g_palette));
            M3ErrorInfo info;
            m3_GetErrorInfo(fresh.rt, &info);
            fprintf(stderr, "vex: boot: %s (%s)\n", err,
                    info.message ? info.message : "");
            m3_FreeRuntime(fresh.rt);
            free(fresh.wasm);
            return false;
        }
    }

    m3_FreeRuntime(cart->rt);
    free(cart->wasm);
    *cart = fresh;
    return true;
}

// Strict integer parse for -s/--scale: rejects trailing garbage and empty
// strings where atoi() would silently yield 0.
static bool parse_long(const char* s, long* out) {
    errno = 0;
    char* end;
    long v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') return false;
    *out = v;
    return true;
}

// FNV-1a 64-bit over the framebuffer -- cheap, deterministic, and identical
// to what the Go host's golden tests compute over their pixel buffer, so a
// headless C run can be diffed against them byte-for-byte.
static uint64_t fnv1a64(const void* data, size_t n) {
    const uint8_t* p = data;
    uint64_t h = 1469598103934665603ULL;
    while (n--) { h ^= *p++; h *= 1099511628211ULL; }
    return h;
}

// Create the GPU-side copy of the framebuffer. A plain texture (not a render
// texture): carts draw on the CPU now, so GL only needs to receive the
// finished frame once per present.
static Texture2D make_screen_texture(void) {
    Image img = GenImageColor(VEX_W, VEX_H, BLANK);
    Texture2D t = LoadTextureFromImage(img);
    SetTextureFilter(t, TEXTURE_FILTER_POINT);
    UnloadImage(img);
    return t;
}

int main(int argc, char** argv) {
    int scale = VEX_SCALE;
    bool watch = false;  // -w/--watch: auto-reload the cart when its file changes
    bool trace = false;  // -t/--trace: with -n, print a line per framebuffer change
    long frames = -1;    // -n/--frames N: run N frames headlessly, then report
    const char* dump_path = NULL; // --dump <file>: write raw framebuffer bytes
    const char* cart_path = NULL;
    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        if (strcmp(a, "-s") == 0 || strcmp(a, "--scale") == 0) {
            long v;
            if (i + 1 >= argc || !parse_long(argv[++i], &v)) {
                fprintf(stderr, "vex: %s requires an integer scale\n", a);
                fprintf(stderr, "usage: %s [-s scale] [-w] [-n frames] [-t] <cart.wasm>\n", argv[0]);
                return 1;
            }
            scale = (int)v;
        } else if (strcmp(a, "-n") == 0 || strcmp(a, "--frames") == 0) {
            if (i + 1 >= argc || !parse_long(argv[++i], &frames) || frames < 0) {
                fprintf(stderr, "vex: %s requires a frame count\n", a);
                fprintf(stderr, "usage: %s [-s scale] [-w] [-n frames] [-t] <cart.wasm>\n", argv[0]);
                return 1;
            }
        } else if (strcmp(a, "-t") == 0 || strcmp(a, "--trace") == 0) {
            trace = true;
        } else if (strcmp(a, "--dump") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "vex: %s requires a file path\n", a);
                return 1;
            }
            dump_path = argv[++i];
        } else if (strcmp(a, "-w") == 0 || strcmp(a, "--watch") == 0) {
            watch = true;
        } else if (a[0] == '-') {
            fprintf(stderr, "vex: unknown option %s\n", a);
            fprintf(stderr, "usage: %s [-s scale] [-w] [-n frames] [-t] <cart.wasm>\n", argv[0]);
            return 1;
        } else if (cart_path == NULL) {
            cart_path = a;
        }
    }
    if (!cart_path) {
        fprintf(stderr, "usage: %s [-s scale] [-w] [-n frames] [-t] <cart.wasm>\n", argv[0]);
        return 1;
    }
    if (scale < 1) scale = 1;
    if (scale > VEX_SCALE_MAX) scale = VEX_SCALE_MAX;

    // ---- load the cart into a wasm3 interpreter --------------------------
    IM3Environment env = m3_NewEnvironment();
    Cart cart;
    if (!load_cart(env, cart_path, &cart)) { m3_FreeEnvironment(env); return 1; }
    M3Result err;

    init_font();
    reset_palette();

    if (cart.f_boot) {
        err = m3_CallV(cart.f_boot);
        if (err) die(cart.rt, "boot", err);
    }

    // Prime the framebuffer with a clean clear before any update() runs, so
    // carts that skip cls() start from a known-dark-blue state (palette[0]
    // after boot()'s pal() overrides) instead of garbage.
    fb_fill(g_palette[0]);

    // ---- headless run (-n frames): no window, no audio --------------------
    // Inputs read as released, title() is ignored, tone() is silent -- which
    // makes runs fully deterministic and directly comparable against the Go
    // host's golden framebuffer hashes.
    if (frames >= 0) {
        struct timespec t0, t1;
        timespec_get(&t0, TIME_UTC);

        uint64_t prev = fnv1a64(g_fb, sizeof g_fb);
        long changes = 0;
        for (long i = 0; i < frames; i++) {
            err = m3_CallV(cart.f_update);
            if (err) die(cart.rt, "update", err);

            uint64_t h = fnv1a64(g_fb, sizeof g_fb);
            if (h != prev) {
                changes++;
                if (trace) printf("frame %ld fb=%016llx\n", i, (unsigned long long)h);
                prev = h;
            }
        }
        timespec_get(&t1, TIME_UTC);
        double ms = (double)(t1.tv_sec - t0.tv_sec) * 1000.0
                  + (double)(t1.tv_nsec - t0.tv_nsec) / 1e6;

        printf("cart=%s frames=%ld total-ms=%.2f ms/frame=%.3f\n",
               cart_path, frames, ms, frames > 0 ? ms / (double)frames : ms);
        printf("fb-changes=%ld fnv1a64=%016llx mem=%u\n",
               changes, (unsigned long long)fnv1a64(g_fb, sizeof g_fb),
               (unsigned)m3_GetMemorySize(cart.rt));
        for (int i = 0; i < 16; i++)
            printf("palette[%X]=%02X%02X%02X\n", i,
                   (unsigned)(g_palette[i] & 0xFF),
                   (unsigned)((g_palette[i] >> 8) & 0xFF),
                   (unsigned)((g_palette[i] >> 16) & 0xFF));

        if (dump_path) {
            FILE* f = fopen(dump_path, "wb");
            if (!f) { fprintf(stderr, "vex: cannot write %s\n", dump_path); return 1; }
            fwrite(g_fb, sizeof g_fb, 1, f);
            fclose(f);
        }

        m3_FreeRuntime(cart.rt);
        m3_FreeEnvironment(env);
        free(cart.wasm);
        return 0;
    }

    // ---- raylib window + audio --------------------------------------------
    // Errors only, like 4b: raylib's INFO chatter (GLFW hints, audio device
    // notes) is noise during normal play.
    SetTraceLogLevel(LOG_ERROR);
    InitWindow(VEX_W * scale, VEX_H * scale, "vex");
    g_window_open = true;
    SetTargetFPS(60);

    Texture2D screen = make_screen_texture();
    if (screen.id == 0) die(cart.rt, "cannot create framebuffer texture", NULL);

    // Audio for tone(). InitAudioDevice() is safe to call when no audio
    // device exists (headless/CI) -- it just fails and tone() stays silent.
    InitAudioDevice();
    g_audio_ready = IsAudioDeviceReady();

    bool integer_scale = false; // crisp integer scale vs. fractional fill;
                                // enabled automatically on entering fullscreen

    long last_mod = GetFileModTime(cart_path); // cart mtime, for -watch reloads
    int  poll = 0;                             // frames since the last mtime poll

    while (!WindowShouldClose()) {
        // Console controls (Super = Cmd on macOS, Super/Windows key on Linux):
        //   Super+Enter  toggle fullscreen
        //   Super+I      toggle integer scaling (crisp pixels vs. fill)
        //   Super+R      reload the cart from disk (also automatic with -watch)
        // Escape (raylib's default) closes the window.
        bool super = IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);

        // Reload on Super+R, and -- with -watch -- automatically when the cart
        // file's mtime changes (polled every ~0.5s). reload_cart keeps the
        // running cart if the new file is bad or half-written, so last_mod only
        // advances on a successful load.
        bool want_reload = super && IsKeyPressed(KEY_R);
        if (watch && ++poll >= VEX_WATCH_FRAMES) {
            poll = 0;
            // GetFileModTime returns 0 when the file is missing (e.g. the user
            // is in the middle of renaming or deleting it). Treat that as
            // "nothing to reload" instead of attempting one every poll, which
            // would spam "cannot read ..." to stderr twice a second.
            long m = GetFileModTime(cart_path);
            if (m != 0 && m != last_mod) want_reload = true;
        }
        if (want_reload && reload_cart(env, cart_path, &cart)) {
            last_mod = GetFileModTime(cart_path);
        }
        if (super && IsKeyPressed(KEY_ENTER)) {
            // True fullscreen (a macOS fullscreen Space) fits the display
            // exactly and hides the menu bar, so the picture isn't pushed off
            // the bottom of a notched screen as borderless-windowed would.
            // Toggling fullscreen can recreate the GL context (macOS), so the
            // framebuffer texture must be re-created afterwards.
            UnloadTexture(screen);
            int mon = GetCurrentMonitor();
            if (!IsWindowFullscreen()) {
                SetWindowSize(GetMonitorWidth(mon), GetMonitorHeight(mon));
                ToggleFullscreen();
                integer_scale = true; // crisp by default in fullscreen
            } else {
                ToggleFullscreen();
                SetWindowSize(VEX_W * scale, VEX_H * scale);
            }
            screen = make_screen_texture();
            if (screen.id == 0) die(cart.rt, "cannot re-create framebuffer texture after fullscreen toggle", NULL);
        }
        if (super && IsKeyPressed(KEY_I)) integer_scale = !integer_scale;

        // Compute this frame's framebuffer->surface mapping: fit the VEX_W x
        // VEX_H screen into the visible surface preserving aspect ratio and
        // centering (letterboxed). Normally the surface is the render (drawable)
        // size divided by the DPI scale -- GetScreenWidth/Height can't be used,
        // they go stale when toggling fullscreen on macOS.
        //
        // On scaled/HiDPI displays raylib's render size can disagree with the
        // real GL surface in fullscreen: a 4K monitor shown at a 1920x1080
        // "looks like" mode reports render=3840 while the surface raylib draws
        // onto is the monitor's logical 1920x1080 -- so scaling against render
        // puts the picture in a corner. GetMonitorWidth/Height matches the
        // surface; when (and only when) it disagrees with render, map against
        // the monitor size and take over the GL viewport below. Where they
        // agree (a 1:1 display, typical X11, etc.) this is a no-op and the
        // render-size path is used unchanged. The mapping is stored in
        // g_view_* so the mouse maps back to logical coords.
        int mon = GetCurrentMonitor();
        bool surface_mismatch = IsWindowFullscreen() &&
            (GetMonitorWidth(mon)  != GetRenderWidth() ||
             GetMonitorHeight(mon) != GetRenderHeight());

        Vector2 dpi = GetWindowScaleDPI();
        float sw, sh;
        if (surface_mismatch) {
            sw = (float)GetMonitorWidth(mon);
            sh = (float)GetMonitorHeight(mon);
        } else {
            sw = GetRenderWidth()  / dpi.x;
            sh = GetRenderHeight() / dpi.y;
        }
        // raylib can briefly report 0 from these if a monitor is unplugged
        // between the fullscreen toggle and this read. Guard so view_scale
        // and the viewport below never see a zero dimension.
        if (sw < 1.0f) sw = 1.0f;
        if (sh < 1.0f) sh = 1.0f;
        float view_scale = (sw / VEX_W < sh / VEX_H) ? sw / VEX_W : sh / VEX_H;
        if (integer_scale) {
            int s = (int)view_scale;
            view_scale = (float)(s < 1 ? 1 : s);
        }
        float dw = VEX_W * view_scale, dh = VEX_H * view_scale;
        float ox = (sw - dw) / 2.0f, oy = (sh - dh) / 2.0f;
        g_view_scale = view_scale; g_view_ox = ox; g_view_oy = oy;

        // Run the cart: pure CPU rasterization into g_fb.
        err = m3_CallV(cart.f_update);
        if (err) die(cart.rt, "update", err);

        // Upload the finished frame to the GPU (one small 320x180 transfer
        // per frame) and capture button state for next frame's btnp().
        UpdateTexture(screen, g_fb);
        g_prev_btns = 0;
        for (int i = 0; i < 6; i++) {
            if (IsKeyDown(VEX_KEYS[i])) g_prev_btns |= (1u << i);
        }

        // Blit the framebuffer to the screen. The texture is top-down, so no
        // negative-height source flip is needed (render textures stored
        // bottom-up; this one doesn't).
        Rectangle src = { 0, 0, (float)VEX_W, (float)VEX_H };
        BeginDrawing();
            ClearBackground(BLACK);
            if (surface_mismatch) {
                // raylib's auto viewport tracks its (wrong) render size, so
                // drive the viewport directly: set it to the letterboxed
                // destination in real surface pixels and draw the framebuffer
                // 1:1 into it. GL's viewport origin is the bottom-left.
                rlDrawRenderBatchActive();
                rlViewport((int)ox, (int)(sh - dh - oy), (int)dw, (int)dh);
                rlMatrixMode(RL_PROJECTION); rlPushMatrix(); rlLoadIdentity();
                rlOrtho(0, VEX_W, VEX_H, 0, -1.0, 1.0);
                rlMatrixMode(RL_MODELVIEW); rlLoadIdentity();
                DrawTexturePro(screen, src,
                    (Rectangle){ 0, 0, (float)VEX_W, (float)VEX_H },
                    (Vector2){ 0, 0 }, 0.0f, WHITE);
                rlDrawRenderBatchActive();
                rlMatrixMode(RL_PROJECTION); rlPopMatrix();
                rlMatrixMode(RL_MODELVIEW); rlLoadIdentity();
                rlViewport(0, 0, (int)sw, (int)sh); // restore for next frame
            } else {
                Rectangle dst = { ox, oy, dw, dh };
                DrawTexturePro(screen, src, dst, (Vector2){0, 0}, 0.0f, WHITE);
            }
        EndDrawing();
    }

    UnloadTexture(screen);
    g_window_open = false;
    tone_cleanup();
    CloseWindow();
    m3_FreeRuntime(cart.rt);
    m3_FreeEnvironment(env);
    free(cart.wasm);
    return 0;
}
