// Stress test: drawing functions with edge-case coordinates.
// Exercises boundary conditions, off-screen draws, zero/negative dimensions,
// and values that probe the VEX_COORD_MAX guard in the host.
#include "vex.h"

#define FRAMES_PER_CASE 20
#define NUM_CASES 14

static int frame;
static int phase;

static void draw_case(int c) {
  switch (c) {
  case 0: // off-screen: negative coordinates
    rect(-10, -10, 20, 20, 1);
    rectb(-5, -5, 15, 15, 2);
    circ(-5, -5, 10, 3);
    circb(-5, -5, 10, 4);
    line(-10, 0, 0, -10, 5);
    tri(-10, -10, -5, 0, 0, -5, 6);
    trib(-10, -10, -5, 0, 0, -5, 7);
    break;
  case 1: // off-screen: beyond dimensions
    rect(400, 0, 20, 20, 1);
    rectb(0, 250, 20, 20, 2);
    circ(400, 200, 10, 3);
    line(0, 300, 350, 0, 4);
    tri(0, 400, 300, 400, 150, 500, 5);
    break;
  case 2: // zero dimensions
    rect(100, 50, 0, 0, 2);
    rectb(100, 60, 0, 0, 3);
    circ(100, 70, 0, 4);
    circb(100, 80, 0, 5);
    rect(100, 90, 10, 0, 6);
    rect(100, 100, 0, 10, 7);
    break;
  case 3: // negative dimensions (should be clamped by host)
    rect(150, 50, -10, -10, 2);
    rectb(150, 60, -10, -10, 3);
    rect(150, 70, -10, 20, 4);
    rect(150, 80, 20, -10, 5);
    break;
  case 4: // at VEX_COORD_MAX boundary (~5120)
    rect(5110, 5110, 20, 20, 6);
    rectb(5000, 5000, 15, 15, 7);
    circ(5100, 5100, 10, 8);
    line(5000, 5000, 5100, 5100, 9);
    break;
  case 5: // at -VEX_COORD_MAX boundary
    rect(-5110, -5110, 20, 20, 6);
    rectb(-5000, -5000, 15, 15, 7);
    circ(-5100, -5100, 10, 8);
    break;
  case 6: // full-screen fills
    rect(0, 0, VEX_WIDTH, VEX_HEIGHT, 1);
    rect(4, 4, VEX_WIDTH - 8, VEX_HEIGHT - 8, 2);
    rect(8, 8, VEX_WIDTH - 16, VEX_HEIGHT - 16, 3);
    break;
  case 7: // many overlapping small rects
    for (int i = 0; i < 100; i++)
      rect(i * 3 % VEX_WIDTH, i * 2 % VEX_HEIGHT, 8, 8, i & 15);
    break;
  case 8: // triangles sharing edges (the tri/trib winding normalization)
    tri(20, 10, 80, 10, 50, 60, 5);
    trib(20, 10, 80, 10, 50, 60, 12);
    tri(100, 10, 160, 10, 130, 60, 7);
    trib(100, 10, 160, 10, 130, 60, 12);
    break;
  case 9: // degenerate triangles (collinear points)
    tri(50, 100, 80, 100, 120, 100, 8);
    trib(50, 100, 80, 100, 120, 100, 12);
    tri(50, 100, 50, 100, 80, 140, 9);
    trib(50, 100, 50, 100, 80, 140, 12);
    break;
  case 10: // circles at edges
    circ(0, 0, 20, 2);
    circb(0, 0, 25, 3);
    circ(VEX_WIDTH - 1, 0, 20, 4);
    circb(VEX_WIDTH - 1, 0, 25, 5);
    circ(0, VEX_HEIGHT - 1, 20, 6);
    circb(0, VEX_HEIGHT - 1, 25, 7);
    circ(VEX_WIDTH - 1, VEX_HEIGHT - 1, 20, 8);
    circb(VEX_WIDTH - 1, VEX_HEIGHT - 1, 25, 9);
    break;
  case 11: // lines at edges and diagonals
    line(0, 0, VEX_WIDTH - 1, VEX_HEIGHT - 1, 2);
    line(VEX_WIDTH - 1, 0, 0, VEX_HEIGHT - 1, 3);
    line(0, 0, VEX_WIDTH - 1, 0, 4);
    line(0, 0, 0, VEX_HEIGHT - 1, 5);
    line(VEX_WIDTH - 1, 0, VEX_WIDTH - 1, VEX_HEIGHT - 1, 6);
    line(0, VEX_HEIGHT - 1, VEX_WIDTH - 1, VEX_HEIGHT - 1, 7);
    break;
  case 12: // blit with various positions and sizes
    {
      unsigned char data[64];
      for (int i = 0; i < 64; i++)
        data[i] = (i / 8 + i % 8) & 15;
      blit(data, -4, -4, 8, 8, 15);
      blit(data, VEX_WIDTH - 4, VEX_HEIGHT - 4, 8, 8, 15);
      blit(data, 0, 80, 8, 8, 15);
      blit(data, 0, 0, 0, 0, 0);
      blit(data, 150, 80, 1, 1, 0);
    }
    break;
  case 13: // text at boundaries
    text("x", -5, 50, 2);
    text("x", VEX_WIDTH - 1, 50, 3);
    text("x", 50, -5, 4);
    text("x", 50, VEX_HEIGHT - 1, 5);
    text("x", VEX_WIDTH + 10, VEX_HEIGHT + 10, 6);
    text("x", -10, -10, 7);
    break;
  }
}

VEX_EXPORT("boot") void boot(void) {
  frame = 0;
  phase = 0;
  title("vex - test_coords");
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
