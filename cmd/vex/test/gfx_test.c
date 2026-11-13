// Framebuffer tests for the C console: the real FONT8 table and drawing
// section are #included verbatim (font_section.inc and gfx_section.inc,
// extracted from cmd/vex/main.c by `make test-hosts`) against minimal wasm3
// stand-ins, then driven through the same host_* entry points carts call --
// so cart-memory bounds checks are exercised too.
//
// Regenerate the extracted sections after editing main.c's drawing code (the
// make target does this automatically).

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define VEX_W 320
#define VEX_H 180

// Distinct, nonzero colors per palette index; 0 means "never written".
static uint32_t g_palette[16];
static uint32_t g_fb[VEX_W * VEX_H];

// ---- wasm3 stand-ins --------------------------------------------------------
// One linear memory; arguments are 64-bit slots holding i32 values, exactly
// as wasm3 lays them out for raw functions.
typedef const char *M3Result;
static const M3Result m3Err_trapOutOfBoundsMemoryAccess = "[trap] out of bounds memory access";

#define MEM_SIZE 65536
static uint8_t g_mem[MEM_SIZE];

static size_t m3_GetMemorySizeAt(const void *mem) {
  (void)mem;
  return MEM_SIZE;
}

#define m3ApiRawFunction(NAME) static M3Result NAME(uint64_t *_sp, void *_mem)
#define m3ApiGetArg(TYPE, NAME) TYPE NAME = (TYPE)(*(int32_t *)(_sp++));
#define m3ApiGetArgMem(TYPE, NAME) \
  TYPE NAME = (TYPE)((uint8_t *)_mem + *(uint32_t *)(_sp++));
#define m3ApiSuccess() return NULL
// Same arithmetic as wasm3's m3ApiCheckMem: no addition that can wrap.
#define m3ApiCheckMem(addr, len)                                             \
  {                                                                          \
    uintptr_t _b = (uintptr_t)(_mem), _a = (uintptr_t)(const void *)(addr);  \
    size_t _s = m3_GetMemorySizeAt(_mem);                                    \
    if (_a < _b || (size_t)(_a - _b) > _s ||                                 \
        (uint64_t)(len) > (uint64_t)(_s - (size_t)(_a - _b)))                \
      return m3Err_trapOutOfBoundsMemoryAccess;                              \
  }

#include "font_section.inc"
#include "gfx_section.inc"

// ---- harness ----------------------------------------------------------------

static int failures = 0;

#define CHECK(desc, cond)                                                    \
  do {                                                                       \
    if (!(cond)) {                                                           \
      fprintf(stderr, "FAIL: %s (%s:%d)\n", desc, __FILE__, __LINE__);       \
      failures++;                                                            \
    }                                                                        \
  } while (0)

static void reset(void) {
  for (int i = 0; i < 16; i++)
    g_palette[i] = 0x100u + (uint32_t)i;
  memset(g_fb, 0, sizeof g_fb);
  memset(g_mem, 0, sizeof g_mem);
}

static uint32_t at(int x, int y) { return g_fb[y * VEX_W + x]; }
static uint32_t pal(int i) { return g_palette[i]; }

static bool fb_untouched(void) {
  for (size_t i = 0; i < VEX_W * VEX_H; i++)
    if (g_fb[i])
      return false;
  return true;
}

// Call a raw host function with up to 8 i32 arguments.
typedef M3Result (*RawFn)(uint64_t *, void *);
static M3Result call(RawFn fn, int n, const int32_t *args) {
  uint64_t sp[8] = {0};
  for (int i = 0; i < n; i++)
    sp[i] = (uint32_t)args[i];
  return fn(sp, g_mem);
}
#define CALL(fn, ...)                                                        \
  call(fn, (int)(sizeof((int32_t[]){__VA_ARGS__}) / sizeof(int32_t)),        \
       (int32_t[]){__VA_ARGS__})

// ---- blit -------------------------------------------------------------------

enum { BMP = 0x100, BMP_W = 400, BMP_H = 3 };

// A 400x3 bitmap: row r is all color r+2, except a key-colored (0) pixel at
// row 1, column 41.
static void put_wide_bitmap(void) {
  for (int r = 0; r < BMP_H; r++)
    memset(&g_mem[BMP + r * BMP_W], r + 2, BMP_W);
  g_mem[BMP + 1 * BMP_W + 41] = 0;
}

static void test_wide_blit_keeps_stride(void) {
  reset();
  put_wide_bitmap();
  // Clamping the stride to 320 would start row 1 inside row 0.
  CHECK("wide blit succeeds", CALL(host_blit, BMP, -40, 10, BMP_W, BMP_H, 0) == NULL);
  CHECK("row 0 left edge", at(0, 10) == pal(2));
  CHECK("row 0 right edge", at(VEX_W - 1, 10) == pal(2));
  CHECK("row 1 left edge", at(0, 11) == pal(3));
  CHECK("row 1 key pixel skipped", at(1, 11) == 0);
  CHECK("row 1 after key", at(2, 11) == pal(3));
  CHECK("row 1 right edge", at(VEX_W - 1, 11) == pal(3));
  CHECK("row 2 right edge", at(VEX_W - 1, 12) == pal(4));
  CHECK("nothing above", at(0, 9) == 0);
  CHECK("nothing below", at(0, 13) == 0);
}

static void test_blit_clips_bottom_right(void) {
  reset();
  put_wide_bitmap();
  CHECK("corner blit succeeds", CALL(host_blit, BMP, 300, 170, BMP_W, BMP_H, -1) == NULL);
  CHECK("first visible pixel", at(300, 170) == pal(2));
  CHECK("last visible pixel", at(VEX_W - 1, 172) == pal(4));
  CHECK("left of the blit", at(299, 170) == 0);
  CHECK("above the blit", at(300, 169) == 0);
}

static void test_blitm_clips_top_left_and_remaps(void) {
  reset();
  // 2x2 bitmap {3,5 / 3,5}; map 3 -> 7. Key 5.
  const uint8_t bmp[4] = {3, 5, 3, 5};
  memcpy(&g_mem[BMP], bmp, sizeof bmp);
  enum { MAP = 0x200 };
  g_mem[MAP + 3] = 7;
  // Only source (1,1) lands on screen at (0,0), and it is the key.
  CHECK("blitm succeeds", CALL(host_blitm, BMP, -1, -1, 2, 2, 5, MAP) == NULL);
  CHECK("key pixel at origin skipped", fb_untouched());
  // Only source (0,0) lands on screen at the bottom-right corner.
  CALL(host_blitm, BMP, VEX_W - 1, VEX_H - 1, 2, 2, 5, MAP);
  CHECK("remapped through the map", at(VEX_W - 1, VEX_H - 1) == pal(7));
}

static void test_blit_needs_whole_bitmap_in_memory(void) {
  reset();
  // Only 400 bytes remain: enough for the visible part of one row, not for
  // the 400x2 bitmap the cart claims.
  int32_t ptr = MEM_SIZE - BMP_W;
  CHECK("short bitmap traps", CALL(host_blit, ptr, 0, 0, BMP_W, 2, -1) == m3Err_trapOutOfBoundsMemoryAccess);
  CHECK("short blitm traps", CALL(host_blitm, ptr, 0, 0, BMP_W, 2, -1, 0) == m3Err_trapOutOfBoundsMemoryAccess);
  CHECK("huge bitmap traps", CALL(host_blit, BMP, 0, 0, INT32_MAX, INT32_MAX, -1) == m3Err_trapOutOfBoundsMemoryAccess);
  CHECK("bad map traps", CALL(host_blitm, BMP, 0, 0, 1, 1, -1, MEM_SIZE - 8) == m3Err_trapOutOfBoundsMemoryAccess);
  CHECK("trapped blits draw nothing", fb_untouched());
  // Calls that draw nothing never touch memory, so they never trap.
  CHECK("zero width is a no-op", CALL(host_blit, ptr, 0, 0, 0, 2, -1) == NULL);
  CHECK("absurd coords are a no-op", CALL(host_blit, ptr, INT32_MIN, 0, BMP_W, 2, -1) == NULL);
}

// ---- text -------------------------------------------------------------------

static void test_tilde_glyph(void) {
  reset();
  enum { STR = 0x300 };
  memcpy(&g_mem[STR], "~", 2);
  CHECK("text succeeds", CALL(host_text, STR, 0, 0, 12) == NULL);
  // 0x000000324C000000: row 3 = 0x32 (columns 2, 3, 6), row 4 = 0x4C
  // (columns 1, 4, 5) -- a wave level with '-'.
  static const int lit[][2] = {{2, 3}, {3, 3}, {6, 3}, {1, 4}, {4, 4}, {5, 4}};
  int count = 0;
  for (int y = 0; y < 8; y++)
    for (int x = 0; x < 8; x++)
      if (at(x, y) == pal(12))
        count++;
  CHECK("tilde lights exactly six pixels", count == 6);
  for (size_t i = 0; i < sizeof lit / sizeof lit[0]; i++)
    CHECK("tilde pixel", at(lit[i][0], lit[i][1]) == pal(12));
}

int main(void) {
  test_wide_blit_keeps_stride();
  test_blit_clips_bottom_right();
  test_blitm_clips_top_left_and_remaps();
  test_blit_needs_whole_bitmap_in_memory();
  test_tilde_glyph();
  if (failures) {
    fprintf(stderr, "gfx_test: %d failure(s)\n", failures);
    return 1;
  }
  printf("gfx_test: all ok\n");
  return 0;
}
