// vex - a minimal WASM fantasy console.
//
// The console is the host: it opens a raylib window, loads a .wasm "cart",
// links a tiny drawing/input API the cart imports, and calls the cart's
// exported update() once per frame. Carts draw into a 320x180 framebuffer
// that is scaled up to the window with nearest-neighbour filtering.
//
//   usage: ./vex <cart.wasm>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "raylib.h"
#include "rlgl.h"
#include "wasm3.h"

#define VEX_W      320   // logical screen width  (keep in sync with vex.h)
#define VEX_H      180   // logical screen height (keep in sync with vex.h)
#define VEX_SCALE    3   // default window pixels per logical pixel (override with -s)
#define VEX_WATCH_FRAMES 30 // -w/--watch: poll the cart's mtime every ~0.5s (at 60fps)

// Default SWEETIE-16 palette: 16 colors, indexed 0..15. Carts can override
// entries at runtime via pal()/palreset(); `palette` holds the live colors.
static const Color DEFAULT_PALETTE[16] = {
    { 26,  28,  44, 255}, { 93,  39,  93, 255}, {177,  62,  83, 255}, {239, 125,  87, 255},
    {255, 205, 117, 255}, {167, 240, 112, 255}, { 56, 183, 100, 255}, { 37, 113, 121, 255},
    { 41,  54, 111, 255}, { 59,  93, 201, 255}, { 65, 166, 246, 255}, {115, 239, 247, 255},
    {244, 244, 244, 255}, {148, 176, 194, 255}, { 86, 108, 134, 255}, { 51,  60,  87, 255},
};
static Color palette[16];
#define PAL(c) (palette[(unsigned)(c) & 15])

// 8x8 bitmap font, shared byte-for-byte with the web host (cmd/vex-web/assets/vex.js)
// so text() looks identical in both. Glyphs cover ASCII 32..127; FONT8[c - 32]
// packs one glyph as a 64-bit value where the most-significant byte is the top
// row and the most-significant bit of each byte is the left pixel.
#define VEX_FONT_FIRST 32
static const uint64_t FONT8[96] = {
    0x0000000000000000ULL, // 32  space
    0x1010101010001000ULL, // 33  !
    0x2828000000000000ULL, // 34  "
    0x28287C287C282800ULL, // 35  #
    0x103C5038147C1000ULL, // 36  $
    0x6264081020460600ULL, // 37  %
    0x304848304A443A00ULL, // 38  &
    0x1010000000000000ULL, // 39  '
    0x1020404040201000ULL, // 40  (
    0x1008040404081000ULL, // 41  )
    0x0008143C14080000ULL, // 42  *
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
    0x0000100000100000ULL, // 58  :
    0x0000100000101020ULL, // 59  ;
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
    0x1818181818181800ULL, // 108 l
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

// Draw a string with the 8x8 font: one framebuffer pixel per set bit, advancing
// 8 px per glyph (unsupported chars leave an 8 px gap) -- matching vex.js text().
static void draw_text(const char* s, int x, int y, Color c) {
    for (; *s; s++, x += 8) {
        int idx = (unsigned char)*s - VEX_FONT_FIRST;
        if (idx < 0 || idx >= 96) continue;

        uint64_t glyph = FONT8[idx];
        for (int row = 0; row < 8; row++) {
            unsigned bits = (unsigned)(glyph >> ((7 - row) * 8)) & 0xFF;
            for (int col = 0; col < 8; col++) {
                if (bits & (1u << (7 - col))) DrawPixel(x + col, y + row, c);
            }
        }
    }
}

// Current framebuffer->window mapping (logical points), used to map raylib's
// window-space mouse position back into the cart's logical coordinates.
static float g_view_scale = 1.0f, g_view_ox = 0.0f, g_view_oy = 0.0f;

// Button key mappings and previous-frame state for btnp() edge detection.
static const int VEX_KEYS[6] = { KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN, KEY_Z, KEY_X };
static uint8_t g_prev_btns = 0;

static void reset_palette(void) {
    for (int i = 0; i < 16; i++) palette[i] = DEFAULT_PALETTE[i];
}

// Copy a bounded, NUL-terminated string out of a cart's linear memory.
static void cart_cstr(IM3Runtime rt, const void* mem, const char* s, char* buf, int size) {
    uintptr_t end = (uintptr_t)mem + m3_GetMemorySize(rt);
    int n = 0;
    while (n < size - 1 && (uintptr_t)(s + n) < end && s[n]) { buf[n] = s[n]; n++; }
    buf[n] = '\0';
}

// ---- host API: functions the cart imports from module "env" --------------
// These run inside BeginTextureMode(), so raylib draws into the framebuffer.

m3ApiRawFunction(host_cls) {
    m3ApiGetArg(int32_t, color)
    ClearBackground(PAL(color));
    m3ApiSuccess();
}

m3ApiRawFunction(host_pset) {
    m3ApiGetArg(int32_t, x)
    m3ApiGetArg(int32_t, y)
    m3ApiGetArg(int32_t, color)
    DrawPixel(x, y, PAL(color));
    m3ApiSuccess();
}

m3ApiRawFunction(host_rect) {
    m3ApiGetArg(int32_t, x)
    m3ApiGetArg(int32_t, y)
    m3ApiGetArg(int32_t, w)
    m3ApiGetArg(int32_t, h)
    m3ApiGetArg(int32_t, color)
    if (w <= 0 || h <= 0) m3ApiSuccess();
    DrawRectangle(x, y, w, h, PAL(color));
    m3ApiSuccess();
}

m3ApiRawFunction(host_rectb) {
    m3ApiGetArg(int32_t, x)
    m3ApiGetArg(int32_t, y)
    m3ApiGetArg(int32_t, w)
    m3ApiGetArg(int32_t, h)
    m3ApiGetArg(int32_t, color)
    if (w <= 0 || h <= 0) m3ApiSuccess();
    // A 1px outline drawn as four filled edges inside the w*h box, so corners
    // are solid and pixel-aligned (DrawRectangleLines leaves corner gaps).
    Color c = PAL(color);
    DrawRectangle(x, y, w, 1, c);              // top
    DrawRectangle(x, y + h - 1, w, 1, c);      // bottom
    DrawRectangle(x, y, 1, h, c);              // left
    DrawRectangle(x + w - 1, y, 1, h, c);      // right
    m3ApiSuccess();
}

m3ApiRawFunction(host_circ) {
    m3ApiGetArg(int32_t, x)
    m3ApiGetArg(int32_t, y)
    m3ApiGetArg(int32_t, r)
    m3ApiGetArg(int32_t, color)
    if (r < 0) r = 0;
    DrawCircle(x, y, (float)r, PAL(color));
    m3ApiSuccess();
}

m3ApiRawFunction(host_circb) {
    m3ApiGetArg(int32_t, x)
    m3ApiGetArg(int32_t, y)
    m3ApiGetArg(int32_t, r)
    m3ApiGetArg(int32_t, color)
    if (r < 0) r = 0;
    DrawCircleLines(x, y, (float)r, PAL(color));
    m3ApiSuccess();
}

m3ApiRawFunction(host_line) {
    m3ApiGetArg(int32_t, x0)
    m3ApiGetArg(int32_t, y0)
    m3ApiGetArg(int32_t, x1)
    m3ApiGetArg(int32_t, y1)
    m3ApiGetArg(int32_t, color)
    DrawLine(x0, y0, x1, y1, PAL(color));
    m3ApiSuccess();
}

m3ApiRawFunction(host_tri) {
    m3ApiGetArg(int32_t, x1)
    m3ApiGetArg(int32_t, y1)
    m3ApiGetArg(int32_t, x2)
    m3ApiGetArg(int32_t, y2)
    m3ApiGetArg(int32_t, x3)
    m3ApiGetArg(int32_t, y3)
    m3ApiGetArg(int32_t, color)
    // raylib keeps backface culling on, so a back-facing winding would be
    // dropped. Normalize to the front-facing order so any vertex order draws.
    // Cast before subtraction to avoid int32_t overflow UB.
    int64_t cross = ((int64_t)x2 - x1) * ((int64_t)y3 - y1) - ((int64_t)y2 - y1) * ((int64_t)x3 - x1);
    if (cross > 0) {
        int32_t tx = x2, ty = y2;
        x2 = x3; y2 = y3;
        x3 = tx; y3 = ty;
    }
    DrawTriangle((Vector2){x1, y1}, (Vector2){x2, y2}, (Vector2){x3, y3}, PAL(color));
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
    // DrawTriangleLines isn't subject to backface culling, but normalize the
    // winding the same way host_tri does so filled and outlined versions of
    // the same triangle share an edge order and carts don't have to remember
    // which entry point normalizes.
    int64_t cross = (int64_t)(x2 - x1) * (y3 - y1) - (int64_t)(y2 - y1) * (x3 - x1);
    if (cross > 0) {
        int32_t tx = x2, ty = y2;
        x2 = x3; y2 = y3;
        x3 = tx; y3 = ty;
    }
    DrawTriangleLines((Vector2){x1, y1}, (Vector2){x2, y2}, (Vector2){x3, y3}, PAL(color));
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
    // Cap w and h to the framebuffer so a malformed cart can't make wasm3 walk
    // a multi-gigabyte memory range inside m3ApiCheckMem (DoS via stalled
    // validation) or stall the host drawing millions of pixels per frame.
    if (w > VEX_W) w = VEX_W;
    if (h > VEX_H) h = VEX_H;
    if ((size_t)w > (size_t)-1 / (size_t)h) m3ApiSuccess();
    m3ApiCheckMem(data, (size_t)w * (size_t)h);
    for (int32_t row = 0; row < h; row++) {
        for (int32_t col = 0; col < w; col++) {
            int32_t c = data[(size_t)row * w + col];
            if (c != key) DrawPixel(x + col, y + row, PAL(c));
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
    draw_text(buf, x, y, PAL(color));
    m3ApiSuccess();
}

// title(s): set the window title from a cart string.
m3ApiRawFunction(host_title) {
    m3ApiGetArgMem(const char*, s)
    m3ApiCheckMem(s, 1);
    char buf[128];
    cart_cstr(runtime, _mem, s, buf, sizeof(buf));
    SetWindowTitle(buf);
    m3ApiSuccess();
}

// btn(button) -> held? Buttons: 0 left, 1 right, 2 up, 3 down, 4 A, 5 B.
m3ApiRawFunction(host_btn) {
    m3ApiReturnType(int32_t)
    m3ApiGetArg(int32_t, button)
    int held = (button >= 0 && button < 6) ? IsKeyDown(VEX_KEYS[button]) : 0;
    m3ApiReturn(held);
}

// btnp(button) -> just pressed this frame? Same buttons as btn().
m3ApiRawFunction(host_btnp) {
    m3ApiReturnType(int32_t)
    m3ApiGetArg(int32_t, button)
    int held = (button >= 0 && button < 6) ? IsKeyDown(VEX_KEYS[button]) : 0;
    int prev = (button >= 0 && button < 6) ? ((g_prev_btns >> button) & 1) : 0;
    m3ApiReturn(held && !prev);
}

// Mouse position, mapped from window space into logical screen coordinates.
m3ApiRawFunction(host_mx) {
    m3ApiReturnType(int32_t)
    m3ApiReturn((int32_t)((GetMouseX() - g_view_ox) / g_view_scale));
}

m3ApiRawFunction(host_my) {
    m3ApiReturnType(int32_t)
    m3ApiReturn((int32_t)((GetMouseY() - g_view_oy) / g_view_scale));
}

// mbtn(button) -> held? 0 left, 1 right, 2 middle (raylib button indices).
m3ApiRawFunction(host_mbtn) {
    m3ApiReturnType(int32_t)
    m3ApiGetArg(int32_t, button)
    int held = (button >= 0 && button <= 6) ? IsMouseButtonDown(button) : 0;
    m3ApiReturn(held);
}

// pal(index, rgb): override one palette entry (index 0..15) with a packed
// 0xRRGGBB color.
m3ApiRawFunction(host_pal) {
    m3ApiGetArg(int32_t, index)
    m3ApiGetArg(int32_t, rgb)
    palette[(unsigned)index & 15] = (Color){
        (unsigned char)((rgb >> 16) & 0xFF), (unsigned char)((rgb >> 8) & 0xFF),
        (unsigned char)(rgb & 0xFF), 255,
    };
    m3ApiSuccess();
}

// palreset(): restore the default palette.
m3ApiRawFunction(host_palreset) {
    reset_palette();
    m3ApiSuccess();
}

static M3Result link_host(IM3Module mod) {
    const char* m = "env";
    // Linking a function the cart doesn't import returns functionLookupFailed,
    // which is harmless: carts only import the API they actually use.
    m3_LinkRawFunction(mod, m, "cls",   "v(i)",     &host_cls);
    m3_LinkRawFunction(mod, m, "pset",  "v(iii)",   &host_pset);
    m3_LinkRawFunction(mod, m, "rect",  "v(iiiii)", &host_rect);
    m3_LinkRawFunction(mod, m, "rectb", "v(iiiii)", &host_rectb);
    m3_LinkRawFunction(mod, m, "circ",  "v(iiii)",  &host_circ);
    m3_LinkRawFunction(mod, m, "circb", "v(iiii)",  &host_circb);
    m3_LinkRawFunction(mod, m, "line",  "v(iiiii)", &host_line);
    m3_LinkRawFunction(mod, m, "tri",   "v(iiiiiii)", &host_tri);
    m3_LinkRawFunction(mod, m, "trib",  "v(iiiiiii)", &host_trib);
    m3_LinkRawFunction(mod, m, "blit",  "v(*iiiii)", &host_blit);
    m3_LinkRawFunction(mod, m, "text",     "v(*iii)",  &host_text);
    m3_LinkRawFunction(mod, m, "title",    "v(*)",     &host_title);
    m3_LinkRawFunction(mod, m, "btn",      "i(i)",     &host_btn);
    m3_LinkRawFunction(mod, m, "btnp",     "i(i)",     &host_btnp);
    m3_LinkRawFunction(mod, m, "mx",       "i()",      &host_mx);
    m3_LinkRawFunction(mod, m, "my",       "i()",      &host_my);
    m3_LinkRawFunction(mod, m, "mbtn",     "i(i)",     &host_mbtn);
    m3_LinkRawFunction(mod, m, "pal",      "v(ii)",    &host_pal);
    m3_LinkRawFunction(mod, m, "palreset", "v()",      &host_palreset);
    return m3Err_none;
}

static void die(IM3Runtime rt, const char* what, M3Result err) {
    if (rt) {
        M3ErrorInfo info;
        m3_GetErrorInfo(rt, &info);
        fprintf(stderr, "vex: %s: %s (%s)\n", what, err, info.message ? info.message : "");
    } else {
        fprintf(stderr, "vex: %s: %s\n", what, err);
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
    link_host(mod);

    // Resolving update() compiles it, which is where wasm3 first notices a
    // missing import -- a cart calling a host function this vex doesn't link
    // (e.g. an API that was removed/renamed, or is newer than this build).
    // Distinguish that from a cart that genuinely lacks an update() export, so
    // the message points at the real cause instead of blaming the export.
    IM3Function f_boot = NULL, f_update = NULL;
    m3_FindFunction(&f_boot, rt, "boot");                 // optional
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
    m3_FreeRuntime(cart->rt);
    free(cart->wasm);
    *cart = fresh;
    reset_palette();
    if (cart->f_boot) {
        M3Result err = m3_CallV(cart->f_boot);
        if (err) die(cart->rt, "boot", err);
    }
    return true;
}

int main(int argc, char** argv) {
    int scale = VEX_SCALE;
    bool watch = false; // -w/--watch: auto-reload the cart when its file changes
    const char* cart_path = NULL;
    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        if ((strcmp(a, "-s") == 0 || strcmp(a, "--scale") == 0) && i + 1 < argc) {
            scale = atoi(argv[++i]);
        } else if (strcmp(a, "-w") == 0 || strcmp(a, "--watch") == 0) {
            watch = true;
        } else if (cart_path == NULL) {
            cart_path = a;
        }
    }
    if (!cart_path) {
        fprintf(stderr, "usage: %s [-s scale] [-w] <cart.wasm>\n", argv[0]);
        return 1;
    }
    if (scale < 1) scale = 1;

    // ---- load the cart into a wasm3 interpreter --------------------------
    IM3Environment env = m3_NewEnvironment();
    Cart cart;
    if (!load_cart(env, cart_path, &cart)) { m3_FreeEnvironment(env); return 1; }
    M3Result err;

    // ---- raylib window + framebuffer -------------------------------------
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(VEX_W * scale, VEX_H * scale, "vex");
    SetTargetFPS(60);

    RenderTexture2D screen = LoadRenderTexture(VEX_W, VEX_H);
    SetTextureFilter(screen.texture, TEXTURE_FILTER_POINT);

    reset_palette();

    if (cart.f_boot) {
        err = m3_CallV(cart.f_boot);
        if (err) die(cart.rt, "boot", err);
    }

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
            // render texture must be re-created afterwards.
            UnloadRenderTexture(screen);
            int mon = GetCurrentMonitor();
            if (!IsWindowFullscreen()) {
                SetWindowSize(GetMonitorWidth(mon), GetMonitorHeight(mon));
                ToggleFullscreen();
                integer_scale = true; // crisp by default in fullscreen
            } else {
                ToggleFullscreen();
                SetWindowSize(VEX_W * scale, VEX_H * scale);
            }
            screen = LoadRenderTexture(VEX_W, VEX_H);
            SetTextureFilter(screen.texture, TEXTURE_FILTER_POINT);
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
        float view_scale = (sw / VEX_W < sh / VEX_H) ? sw / VEX_W : sh / VEX_H;
        if (integer_scale) {
            int s = (int)view_scale;
            view_scale = (float)(s < 1 ? 1 : s);
        }
        float dw = VEX_W * view_scale, dh = VEX_H * view_scale;
        float ox = (sw - dw) / 2.0f, oy = (sh - dh) / 2.0f;
        g_view_scale = view_scale; g_view_ox = ox; g_view_oy = oy;

        // Run the cart, drawing into the framebuffer.
        BeginTextureMode(screen);
            err = m3_CallV(cart.f_update);
        EndTextureMode();
        if (err) die(cart.rt, "update", err);

        // Capture button state for next frame's btnp() edge detection.
        g_prev_btns = 0;
        for (int i = 0; i < 6; i++) {
            if (IsKeyDown(VEX_KEYS[i])) g_prev_btns |= (1u << i);
        }

        // Blit the framebuffer to the screen (src height negative: render
        // textures are stored bottom-up).
        Rectangle src = { 0, 0, (float)VEX_W, -(float)VEX_H };
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
                DrawTexturePro(screen.texture, src,
                    (Rectangle){ 0, 0, (float)VEX_W, (float)VEX_H },
                    (Vector2){ 0, 0 }, 0.0f, WHITE);
                rlDrawRenderBatchActive();
                rlMatrixMode(RL_PROJECTION); rlPopMatrix();
                rlMatrixMode(RL_MODELVIEW); rlLoadIdentity();
                rlViewport(0, 0, (int)sw, (int)sh); // restore for next frame
            } else {
                Rectangle dst = { ox, oy, dw, dh };
                DrawTexturePro(screen.texture, src, dst, (Vector2){0, 0}, 0.0f, WHITE);
            }
        EndDrawing();
    }

    UnloadRenderTexture(screen);
    CloseWindow();
    m3_FreeRuntime(cart.rt);
    m3_FreeEnvironment(env);
    free(cart.wasm);
    return 0;
}
