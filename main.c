// vex - a minimal WASM fantasy console.
//
// The console is the host: it opens a raylib window, loads a .wasm "cart",
// links a tiny drawing/input API the cart imports, and calls the cart's
// exported update() once per frame. Carts draw into a 128x128 framebuffer
// that is scaled up to the window with nearest-neighbour filtering.
//
//   usage: ./vex <cart.wasm>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "raylib.h"
#include "wasm3.h"

#define VEX_W      128   // logical screen width
#define VEX_H      128   // logical screen height
#define VEX_SCALE    4   // window pixels per logical pixel
#define VEX_FONT     8   // text size in logical pixels

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

static void reset_palette(void) {
    for (int i = 0; i < 16; i++) palette[i] = DEFAULT_PALETTE[i];
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
    DrawRectangle(x, y, w, h, PAL(color));
    m3ApiSuccess();
}

m3ApiRawFunction(host_rectb) {
    m3ApiGetArg(int32_t, x)
    m3ApiGetArg(int32_t, y)
    m3ApiGetArg(int32_t, w)
    m3ApiGetArg(int32_t, h)
    m3ApiGetArg(int32_t, color)
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
    DrawCircle(x, y, (float)r, PAL(color));
    m3ApiSuccess();
}

m3ApiRawFunction(host_circb) {
    m3ApiGetArg(int32_t, x)
    m3ApiGetArg(int32_t, y)
    m3ApiGetArg(int32_t, r)
    m3ApiGetArg(int32_t, color)
    DrawCircleLines(x, y, (float)r, PAL(color));
    m3ApiSuccess();
}

// ring(x, y, inner, outer, color): filled annulus. segments=0 lets raylib pick
// a smooth segment count automatically.
m3ApiRawFunction(host_ring) {
    m3ApiGetArg(int32_t, x)
    m3ApiGetArg(int32_t, y)
    m3ApiGetArg(int32_t, inner)
    m3ApiGetArg(int32_t, outer)
    m3ApiGetArg(int32_t, color)
    DrawRing((Vector2){x, y}, (float)inner, (float)outer, 0.0f, 360.0f, 0, PAL(color));
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
    int32_t cross = (x2 - x1) * (y3 - y1) - (y2 - y1) * (x3 - x1);
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
    DrawTriangleLines((Vector2){x1, y1}, (Vector2){x2, y2}, (Vector2){x3, y3}, PAL(color));
    m3ApiSuccess();
}

m3ApiRawFunction(host_text) {
    m3ApiGetArgMem(const char*, s)
    m3ApiGetArg(int32_t, x)
    m3ApiGetArg(int32_t, y)
    m3ApiGetArg(int32_t, color)
    m3ApiCheckMem(s, 1);
    // Copy a bounded, NUL-terminated string out of the cart's linear memory.
    char buf[128];
    uintptr_t end = (uintptr_t)_mem + m3_GetMemorySize(runtime);
    int n = 0;
    while (n < (int)sizeof(buf) - 1 && (uintptr_t)(s + n) < end && s[n]) {
        buf[n] = s[n];
        n++;
    }
    buf[n] = '\0';
    DrawText(buf, x, y, VEX_FONT, PAL(color));
    m3ApiSuccess();
}

// btn(button) -> held? Buttons: 0 left, 1 right, 2 up, 3 down, 4 A, 5 B.
m3ApiRawFunction(host_btn) {
    m3ApiReturnType(int32_t)
    m3ApiGetArg(int32_t, button)
    static const int keys[6] = { KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN, KEY_Z, KEY_X };
    int held = (button >= 0 && button < 6) ? IsKeyDown(keys[button]) : 0;
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
    m3_LinkRawFunction(mod, m, "ring",  "v(iiiii)", &host_ring);
    m3_LinkRawFunction(mod, m, "line",  "v(iiiii)", &host_line);
    m3_LinkRawFunction(mod, m, "tri",   "v(iiiiiii)", &host_tri);
    m3_LinkRawFunction(mod, m, "trib",  "v(iiiiiii)", &host_trib);
    m3_LinkRawFunction(mod, m, "text",     "v(*iii)",  &host_text);
    m3_LinkRawFunction(mod, m, "btn",      "i(i)",     &host_btn);
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

static uint8_t* read_file(const char* path, size_t* out_len) {
    FILE* f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "vex: cannot open %s\n", path); exit(1); }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* buf = malloc(n);
    if (fread(buf, 1, n, f) != (size_t)n) { fprintf(stderr, "vex: read error\n"); exit(1); }
    fclose(f);
    *out_len = (size_t)n;
    return buf;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <cart.wasm>\n", argv[0]);
        return 1;
    }
    const char* cart_path = argv[1];

    // ---- load the cart into a wasm3 interpreter --------------------------
    size_t wasm_len;
    uint8_t* wasm = read_file(cart_path, &wasm_len); // kept alive for program lifetime

    IM3Environment env = m3_NewEnvironment();
    IM3Runtime rt = m3_NewRuntime(env, 64 * 1024 /* stack */, NULL);

    IM3Module mod;
    M3Result err = m3_ParseModule(env, &mod, wasm, wasm_len);
    if (err) die(rt, "parse", err);
    err = m3_LoadModule(rt, mod);
    if (err) die(rt, "load", err);

    link_host(mod);

    IM3Function f_boot = NULL, f_update = NULL;
    m3_FindFunction(&f_boot, rt, "boot");       // optional
    err = m3_FindFunction(&f_update, rt, "update");
    if (err || !f_update) die(rt, "cart has no update() export", err ? err : "missing");

    // ---- raylib window + framebuffer -------------------------------------
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(VEX_W * VEX_SCALE, VEX_H * VEX_SCALE, "vex");
    SetTargetFPS(60);

    RenderTexture2D screen = LoadRenderTexture(VEX_W, VEX_H);
    SetTextureFilter(screen.texture, TEXTURE_FILTER_POINT);

    reset_palette();

    if (f_boot) {
        err = m3_CallV(f_boot);
        if (err) die(rt, "boot", err);
    }

    bool integer_scale = false; // crisp integer scale vs. fractional fill;
                                // enabled automatically on entering fullscreen

    while (!WindowShouldClose()) {
        // Console controls (Super = Cmd on macOS, Super/Windows key on Linux):
        //   Super+Enter  toggle fullscreen
        //   Super+I      toggle integer scaling (crisp pixels vs. fill)
        // Escape (raylib's default) closes the window.
        bool super = IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);
        if (super && IsKeyPressed(KEY_ENTER)) {
            // True fullscreen (a macOS fullscreen Space) fits the display
            // exactly and hides the menu bar, so the picture isn't pushed off
            // the bottom of a notched screen as borderless-windowed would.
            int mon = GetCurrentMonitor();
            if (!IsWindowFullscreen()) {
                SetWindowSize(GetMonitorWidth(mon), GetMonitorHeight(mon));
                ToggleFullscreen();
                integer_scale = true; // crisp by default in fullscreen
            } else {
                ToggleFullscreen();
                SetWindowSize(VEX_W * VEX_SCALE, VEX_H * VEX_SCALE);
            }
        }
        if (super && IsKeyPressed(KEY_I)) integer_scale = !integer_scale;

        // Run the cart, drawing into the 128x128 framebuffer.
        BeginTextureMode(screen);
            err = m3_CallV(f_update);
        EndTextureMode();
        if (err) die(rt, "update", err);

        // Scale the framebuffer to fit the window, preserving aspect ratio and
        // centering (letterboxed). For a square 128x128 screen in a wider
        // window this fills the height and centers horizontally (y-flipped:
        // render textures are stored bottom-up).
        //
        // The drawing coordinate space is the render (drawable) size divided by
        // the DPI scale. GetScreenWidth/Height can't be used: they go stale
        // when toggling fullscreen on macOS, while render size + DPI stay
        // correct in every mode.
        Vector2 dpi = GetWindowScaleDPI();
        float sw = GetRenderWidth()  / dpi.x;
        float sh = GetRenderHeight() / dpi.y;
        float scale = (sw / VEX_W < sh / VEX_H) ? sw / VEX_W : sh / VEX_H;
        if (integer_scale) {
            // Floor to a whole multiple so every source pixel maps to the same
            // number of screen pixels (crisp, at the cost of more border).
            int s = (int)scale;
            scale = (float)(s < 1 ? 1 : s);
        }
        float dw = VEX_W * scale, dh = VEX_H * scale;
        BeginDrawing();
            ClearBackground(BLACK);
            Rectangle src = { 0, 0, (float)VEX_W, -(float)VEX_H };
            Rectangle dst = { (sw - dw) / 2, (sh - dh) / 2, dw, dh };
            DrawTexturePro(screen.texture, src, dst, (Vector2){0, 0}, 0.0f, WHITE);
        EndDrawing();
    }

    UnloadRenderTexture(screen);
    CloseWindow();
    m3_FreeRuntime(rt);
    return 0;
}
