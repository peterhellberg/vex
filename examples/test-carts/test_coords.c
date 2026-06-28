#include "vex.h"

#define FRAMES_PER_CASE 18
#define NUM_CASES 16

static const char* CASE_NAMES[NUM_CASES] = {
  "off-screen: negative coords",
  "off-screen: beyond dimensions",
  "zero dimensions",
  "negative dimensions (clamped)",
  "at VEX_COORD_MAX boundary",
  "at -VEX_COORD_MAX boundary",
  "full-screen fills",
  "many overlapping small rects",
  "triangles sharing edges (winding)",
  "degenerate triangles (collinear)",
  "circles at edges",
  "lines at edges and diagonals",
  "blit at boundaries",
  "text at boundaries",
  "line: single-pixel, off-screen, axis",
  "circ: negative and huge radius",
};

static int frame;
static int phase;

static void draw_case(int c) {
  switch (c) {
  case 0:
    rect(-10, -10, 20, 20, 1);
    rectb(-5, -5, 15, 15, 2);
    circ(-5, -5, 10, 3);
    circb(-5, -5, 10, 4);
    line(-10, 0, 0, -10, 5);
    tri(-10, -10, -5, 0, 0, -5, 6);
    trib(-10, -10, -5, 0, 0, -5, 7);
    break;
  case 1:
    rect(400, 0, 20, 20, 1);
    rectb(0, 250, 20, 20, 2);
    circ(400, 200, 10, 3);
    line(0, 300, 350, 0, 4);
    tri(0, 400, 300, 400, 150, 500, 5);
    break;
  case 2:
    rect(100, 50, 0, 0, 2);
    rectb(100, 60, 0, 0, 3);
    circ(100, 70, 0, 4);
    circb(100, 80, 0, 5);
    rect(100, 90, 10, 0, 6);
    rect(100, 100, 0, 10, 7);
    break;
  case 3:
    rect(150, 50, -10, -10, 2);
    rectb(150, 60, -10, -10, 3);
    rect(150, 70, -10, 20, 4);
    rect(150, 80, 20, -10, 5);
    break;
  case 4:
    rect(5110, 5110, 20, 20, 6);
    rectb(5000, 5000, 15, 15, 7);
    circ(5100, 5100, 10, 8);
    line(5000, 5000, 5100, 5100, 9);
    break;
  case 5:
    rect(-5110, -5110, 20, 20, 6);
    rectb(-5000, -5000, 15, 15, 7);
    circ(-5100, -5100, 10, 8);
    break;
  case 6:
    rect(0, 0, VEX_WIDTH, VEX_HEIGHT, 1);
    rect(4, 4, VEX_WIDTH - 8, VEX_HEIGHT - 8, 2);
    rect(8, 8, VEX_WIDTH - 16, VEX_HEIGHT - 16, 3);
    break;
  case 7:
    for (int i = 0; i < 100; i++)
      rect(i * 3 % VEX_WIDTH, i * 2 % VEX_HEIGHT, 8, 8, i & 15);
    break;
  case 8:
    tri(20, 10, 80, 10, 50, 60, 5);
    trib(20, 10, 80, 10, 50, 60, 12);
    tri(100, 10, 160, 10, 130, 60, 7);
    trib(100, 10, 160, 10, 130, 60, 12);
    break;
  case 9:
    tri(50, 100, 80, 100, 120, 100, 8);
    trib(50, 100, 80, 100, 120, 100, 12);
    tri(50, 100, 50, 100, 80, 140, 9);
    trib(50, 100, 50, 100, 80, 140, 12);
    break;
  case 10:
    circ(0, 0, 20, 2);
    circb(0, 0, 25, 3);
    circ(VEX_WIDTH - 1, 0, 20, 4);
    circb(VEX_WIDTH - 1, 0, 25, 5);
    circ(0, VEX_HEIGHT - 1, 20, 6);
    circb(0, VEX_HEIGHT - 1, 25, 7);
    circ(VEX_WIDTH - 1, VEX_HEIGHT - 1, 20, 8);
    circb(VEX_WIDTH - 1, VEX_HEIGHT - 1, 25, 9);
    break;
  case 11:
    line(0, 0, VEX_WIDTH - 1, VEX_HEIGHT - 1, 2);
    line(VEX_WIDTH - 1, 0, 0, VEX_HEIGHT - 1, 3);
    line(0, 0, VEX_WIDTH - 1, 0, 4);
    line(0, 0, 0, VEX_HEIGHT - 1, 5);
    line(VEX_WIDTH - 1, 0, VEX_WIDTH - 1, VEX_HEIGHT - 1, 6);
    line(0, VEX_HEIGHT - 1, VEX_WIDTH - 1, VEX_HEIGHT - 1, 7);
    break;
  case 12:
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
  case 13:
    text("x", -5, 50, 2);
    text("x", VEX_WIDTH - 1, 50, 3);
    text("x", 50, -5, 4);
    text("x", 50, VEX_HEIGHT - 1, 5);
    text("x", VEX_WIDTH + 10, VEX_HEIGHT + 10, 6);
    text("x", -10, -10, 7);
    break;
  case 14:
    line(50, 10, 50, 10, 12);
    line(-5000, -5000, 6000, 6000, 6);
    line(0, 0, VEX_WIDTH - 1, 0, 4);
    line(0, 0, 0, VEX_HEIGHT - 1, 5);
    break;
  case 15:
    circ(100, 20, -5, 4);
    circb(100, 30, -10, 5);
    circ(200, 20, 10000, 6);
    circb(200, 30, 10000, 7);
    break;
  }
}

static void draw_header(void) {
  rect(0, 0, VEX_WIDTH, 8, 1);
  text("C", 2, 0, 12);
  text("OORD", 10, 0, 12);
  int dx = 6;
  for (int i = 0; i < NUM_CASES; i++) {
    int x = VEX_WIDTH - NUM_CASES * dx + i * dx;
    rect(x, 1, 4, 6, i < phase ? 12 : (i == phase ? 14 : 5));
  }
  text(CASE_NAMES[phase], 2, 10, 14);
}

VEX_EXPORT("boot") void boot(void) {
  frame = 0;
  phase = 0;
  title("vex - test_coords");
}

VEX_EXPORT("update") void update(void) {
  if (phase >= NUM_CASES) {
    cls(0);
    text("DONE - test_coords", 80, VEX_HEIGHT / 2 - 4, 12);
    return;
  }

  if (frame == 0) {
    cls(0);
    draw_header();
    draw_case(phase);
  }

  frame++;
  if (frame >= FRAMES_PER_CASE) {
    frame = 0;
    phase++;
  }
}
