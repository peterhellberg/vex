// vex.h - cart SDK. Include this in a cart and compile to wasm32:
//
//   zig cc --target=wasm32-freestanding -nostdlib -Os \
//          -Wl,--no-entry -I. -o cart.wasm main.c
//
// A cart must export update() (called every frame at 60fps) and may export
// boot() (called once at start). It draws by calling the functions below.
#ifndef VEX_H
#define VEX_H

#define VEX_IMPORT(name) __attribute__((import_module("env"), import_name(name)))
#define VEX_EXPORT(name) __attribute__((export_name(name)))

// Screen is 320x180. Colors are SWEETIE-16 palette indices 0..15.
#define VEX_WIDTH  320
#define VEX_HEIGHT 180

// Buttons, as passed to btn().
#define VEX_LEFT  0
#define VEX_RIGHT 1
#define VEX_UP    2
#define VEX_DOWN  3
#define VEX_Z     4
#define VEX_X     5

// Mouse buttons, as passed to mbtn().
#define VEX_MOUSE_LEFT   0
#define VEX_MOUSE_RIGHT  1
#define VEX_MOUSE_MIDDLE 2

VEX_IMPORT("cls")   void cls(int color);                              // clear screen
VEX_IMPORT("pset")  void pset(int x, int y, int color);              // set one pixel
VEX_IMPORT("rect")  void rect(int x, int y, int w, int h, int color);  // filled rect
VEX_IMPORT("rectb") void rectb(int x, int y, int w, int h, int color); // rect outline
VEX_IMPORT("circ")  void circ(int x, int y, int r, int color);       // filled circle
VEX_IMPORT("circb") void circb(int x, int y, int r, int color);      // circle outline
VEX_IMPORT("line")  void line(int x0, int y0, int x1, int y1, int color);
VEX_IMPORT("tri")   void tri(int x1, int y1, int x2, int y2, int x3, int y3, int color);  // filled triangle
VEX_IMPORT("trib")  void trib(int x1, int y1, int x2, int y2, int x3, int y3, int color); // triangle outline
VEX_IMPORT("blit")  void blit(const void* data, int x, int y, int w, int h, int key); // w*h palette-index bitmap; pixels == key are skipped
VEX_IMPORT("text")  void text(const char* s, int x, int y, int color);
VEX_IMPORT("title") void title(const char* s);                       // set window title
VEX_IMPORT("btn")   int  btn(int button);                            // 1 if held, else 0
VEX_IMPORT("btnp")  int  btnp(int button);                           // 1 if just pressed this frame

VEX_IMPORT("mx")   int mx(void);             // mouse x in screen pixels
VEX_IMPORT("my")   int my(void);             // mouse y in screen pixels
VEX_IMPORT("mbtn") int mbtn(int button);     // 1 if mouse button held

VEX_IMPORT("pal")      void pal(int index, int rgb); // override palette entry (0xRRGGBB)
VEX_IMPORT("palreset") void palreset(void);          // restore default palette

#endif // VEX_H
