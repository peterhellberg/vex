// Stress test: blit() with varied dimensions, keys, and data patterns.
// Exercises memory bounds checks, run-length encoding, and edge cases.
#include "vex.h"

#define FRAMES_PER_CASE 15
#define NUM_CASES 8

static int frame;
static int phase;
static unsigned char big_data[320 * 180];

static void fill_data(unsigned char* d, int w, int h, int pat) {
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      switch (pat) {
      case 0: d[y * w + x] = (x + y) & 15; break;        // checkerboard
      case 1: d[y * w + x] = (x * 7 + y * 13) & 15; break; // noise
      case 2: d[y * w + x] = 5; break;                     // solid
      case 3: d[y * w + x] = x < w / 2 ? 0 : 7; break;    // half transparent
      case 4: d[y * w + x] = y < h / 2 ? 0 : 8; break;    // half transparent
      case 5: d[y * w + x] = 0; break;                     // all transparent
      }
    }
}

static void draw_case(int c) {
  switch (c) {
  case 0: // 1x1 pixel blits at every screen position (corner case)
    for (int i = 0; i < VEX_WIDTH * VEX_HEIGHT; i += 997) {
      int x = i % VEX_WIDTH;
      int y = (i / VEX_WIDTH) % VEX_HEIGHT;
      unsigned char p = (unsigned char)((x + y) & 15);
      blit(&p, x, y, 1, 1, 16);
    }
    break;
  case 1: // full-screen blit, every pixel, key outside 0..15
    fill_data(big_data, VEX_WIDTH, VEX_HEIGHT, 2);
    blit(big_data, 0, 0, VEX_WIDTH, VEX_HEIGHT, 16);
    break;
  case 2: // full-screen blit with transparency key
    fill_data(big_data, VEX_WIDTH, VEX_HEIGHT, 3);
    blit(big_data, 0, 0, VEX_WIDTH, VEX_HEIGHT, 0);
    break;
  case 3: // all-transparent blit (every pixel is key)
    fill_data(big_data, VEX_WIDTH, VEX_HEIGHT, 5);
    blit(big_data, 0, 0, VEX_WIDTH, VEX_HEIGHT, 0);
    break;
  case 4: // tiny blit at many positions
    {
      unsigned char d[4] = {1, 2, 3, 4};
      for (int y = 0; y < VEX_HEIGHT; y += 10)
        for (int x = 0; x < VEX_WIDTH; x += 10)
          blit(d, x, y, 2, 2, 4);
    }
    break;
  case 5: // blit with key = every palette index
    {
      unsigned char d[16];
      for (int i = 0; i < 16; i++) d[i] = i;
      for (int k = 0; k < 17; k++) // key 0..16 (16 = no transparency)
        blit(d, k * 18, 0, 16, 1, k);
    }
    break;
  case 6: // blit at fractional/negative positions w/ clipping
    {
      fill_data(big_data, 50, 50, 0);
      blit(big_data, -25, -25, 50, 50, 15);
      blit(big_data, VEX_WIDTH - 25, -25, 50, 50, 15);
      blit(big_data, -25, VEX_HEIGHT - 25, 50, 50, 15);
      blit(big_data, VEX_WIDTH - 25, VEX_HEIGHT - 25, 50, 50, 15);
    }
    break;
  case 7: // zero/negative dimension blits
    {
      unsigned char d[1] = {3};
      blit(d, 100, 100, 0, 0, 0);
      blit(d, 120, 100, -1, -1, 0);
      blit(d, 140, 100, 10, 0, 0);
      blit(d, 160, 100, 0, 10, 0);
      rect(100, 100, 80, 10, 4); // mark where tests ran
    }
    break;
  }
}

VEX_EXPORT("boot") void boot(void) {
  frame = 0;
  phase = 0;
  title("vex - test_blit");
}

VEX_EXPORT("update") void update(void) {
  if (phase >= NUM_CASES) {
    cls(0);
    text("DONE", VEX_WIDTH / 2 - 12, VEX_HEIGHT / 2 - 4, 12);
    return;
  }

  if (frame == 0) {
    cls(0);
    draw_case(phase);
  }

  frame++;
  if (frame >= FRAMES_PER_CASE) {
    frame = 0;
    phase++;
  }
}
