// Stress test: flood all host API functions with varied argument patterns
// and edge cases. Exercises argument validation, state cleanliness across
// rapid calls, and the overall call-interface path.
#include "vex.h"

#define FRAMES_PER_PHASE 20
#define NUM_PHASES 8

static int frame;
static int phase;

VEX_EXPORT("boot") void boot(void) {
  frame = 0;
  phase = 0;
  title("vex - test_api");
}

VEX_EXPORT("update") void update(void) {
  int p = phase % NUM_PHASES;

  switch (p) {
  case 0: // pset: every pixel on screen, then every other pixel white
    for (int y = 0; y < VEX_HEIGHT; y++)
      for (int x = 0; x < VEX_WIDTH; x++)
        pset(x, y, (x + y) & 15);
    for (int y = 0; y < VEX_HEIGHT; y += 2)
      for (int x = 0; x < VEX_WIDTH; x += 2)
        pset(x, y, 12);
    break;

  case 1: // rect: overlapping rectangles with varying sizes
    for (int i = 0; i < 80; i++) {
      int x = (i * 13) % VEX_WIDTH;
      int y = (i * 7) % VEX_HEIGHT;
      int w = 10 + (i * 3) % 80;
      int h = 10 + (i * 5) % 60;
      int c = i & 15;
      rect(x, y, w, h, c);
    }
    break;

  case 2: // rectb: outlined rectangles
    for (int i = 0; i < 40; i++) {
      int x = (i * 11) % VEX_WIDTH;
      int y = (i * 17) % VEX_HEIGHT;
      int w = 5 + (i * 7) % 50;
      int h = 5 + (i * 9) % 50;
      rectb(x, y, w, h, i & 15);
    }
    break;

  case 3: // interleaved circ/circb
    for (int i = 0; i < 30; i++) {
      int x = (i * 23) % VEX_WIDTH;
      int y = (i * 19) % VEX_HEIGHT;
      int r = 3 + (i * 5) % 25;
      if (i & 1) circ(x, y, r, i & 15);
      else       circb(x, y, r, i & 15);
    }
    break;

  case 4: // line: criss-cross every direction
    for (int i = 0; i < 100; i++) {
      int x0 = (i * 31) % VEX_WIDTH;
      int y0 = (i * 37) % VEX_HEIGHT;
      int x1 = (i * 41) % VEX_WIDTH;
      int y1 = (i * 43) % VEX_HEIGHT;
      line(x0, y0, x1, y1, i & 15);
    }
    break;

  case 5: // text: all printable ASCII at various positions
    text(" !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~", 0, 0, 12);
    text("Hello, VEX! 123", 10, 20, 10);
    text("ABCDEFGHIJKLMNOPQRSTUVWXYZ", 10, 30, 11);
    text("abcdefghijklmnopqrstuvwxyz", 10, 40, 13);
    text("0123456789", 10, 50, 14);
    text("x", VEX_WIDTH - 8, 0, 2);
    text("x", 0, VEX_HEIGHT - 8, 3);
    text("x", VEX_WIDTH - 8, VEX_HEIGHT - 8, 4);
    break;

  case 6: // tri/trib: triangles at various winding orders
    // Clockwise, counter-clockwise, degenerate, thin slivers
    tri(50, 10, 100, 10, 75, 60, 2);
    trib(50, 10, 100, 10, 75, 60, 12);
    tri(150, 10, 100, 60, 200, 60, 4);
    trib(150, 10, 100, 60, 200, 60, 12);
    tri(50, 80, 100, 80, 50, 130, 5);
    trib(50, 80, 100, 80, 50, 130, 12);
    tri(150, 80, 200, 130, 100, 130, 6);
    trib(150, 80, 200, 130, 100, 130, 12);
    // Sliver: very thin triangle
    tri(10, 150, 12, 150, 300, 155, 7);
    trib(10, 150, 12, 150, 300, 155, 12);
    break;

  case 7: // blit: various sizes and data patterns
    {
      unsigned char d[256];
      for (int i = 0; i < 256; i++) d[i] = (i / 16 + i % 16) & 15;
      blit(d, 0, 0, 16, 16, 15);
      blit(d, 50, 0, 16, 16, 0);
      blit(d, 100, 0, 16, 8, 15);
      blit(d, 150, 0, 8, 16, 15);
      // Rectangular patterns
      unsigned char r[4] = {1, 2, 3, 4};
      blit(r, 200, 0, 2, 2, 4);
      blit(r, 220, 0, 2, 2, 5);
      blit(r, 240, 0, 2, 2, 0);
    }
    break;
  }

  // Test input functions every frame: call btn/btnp/mx/my/mbtn repeatedly
  // to ensure they don't corrupt state or crash.
  for (int b = -1; b <= 7; b++) {
    int h = btn(b);
    int p = btnp(b);
  }
  int mxv = mx();
  int myv = my();
  for (int mb = -1; mb <= 4; mb++) {
    int mbh = mbtn(mb);
  }

  text("P", 0, VEX_HEIGHT - 8, (phase & 1) ? 12 : 13);

  frame++;
  if (frame >= FRAMES_PER_PHASE) {
    frame = 0;
    phase++;
  }

  if (phase >= NUM_PHASES * 4) {
    cls(0);
    text("DONE", VEX_WIDTH / 2 - 12, VEX_HEIGHT / 2 - 4, 12);
  }
}
