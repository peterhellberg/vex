// Benchmark cart: deterministic scenes exercising every drawing API.
// Designed to be visually compared across vex host implementations
// (C console vs web console) to spot rendering differences or bugs.
//
// Each scene displays for BENCH_FRAMES frames, then advances.
// The output is fully deterministic — no PRNG, no frame-dependent positions.
#include "vex.h"

#define BENCH_FRAMES 90
#define NUM_SCENES   11

static int frame;
static int scene;

// 8x8 smiley face sprite used in the blit scene
static const unsigned char SMILE[64] = {
  0,0,3,3,3,3,0,0,
  0,3,3,3,3,3,3,0,
  3,3,0,3,3,0,3,3,
  3,3,3,3,3,3,3,3,
  3,0,3,3,3,3,0,3,
  3,3,0,0,0,0,3,3,
  0,3,3,3,3,3,3,0,
  0,0,3,3,3,3,0,0,
};

// 4x4 checkerboard tile
static const unsigned char CHECK4[16] = {
  1,2,1,2,
  2,1,2,1,
  1,2,1,2,
  2,1,2,1,
};

static void scene_color_bars(void) {
  // 16 vertical color bars filling the screen
  for (int i = 0; i < 16; i++) {
    int x = i * (VEX_WIDTH / 16);
    int w = (i == 15) ? VEX_WIDTH - x : VEX_WIDTH / 16;
    rect(x, 0, w, VEX_HEIGHT, i);
  }
  // Overlay: white frame + palette index labels
  rectb(0, 0, VEX_WIDTH - 1, VEX_HEIGHT - 1, 12);
  for (int i = 0; i < 16; i += 2) {
    int x = i * (VEX_WIDTH / 16) + 2;
    text("X", x, 2, i == 0 ? 12 : 0);
  }
}

static void scene_pset_grid(void) {
  cls(1);
  // Fine grid: every 4th pixel
  for (int y = 0; y < VEX_HEIGHT; y += 4)
    for (int x = 0; x < VEX_WIDTH; x += 4)
      pset(x, y, 12);

  // Coarse grid: every 16th pixel in a different color
  for (int y = 0; y < VEX_HEIGHT; y += 16)
    for (int x = 0; x < VEX_WIDTH; x += 16)
      pset(x, y, 7);

  // Diagonal pset line
  for (int i = 0; i < VEX_WIDTH && i < VEX_HEIGHT; i += 2)
    pset(i, i, 6);

  // Color bars at the top
  for (int i = 0; i < 16; i++)
    for (int y = 0; y < 8; y++)
      for (int x = i * 20; x < i * 20 + 20 && x < VEX_WIDTH; x++)
        pset(x, y, i);
}

static void scene_rects(void) {
  cls(1);
  // Nested concentric rectangles
  for (int i = 0; i < 16; i++) {
    int s = i * 10;
    rect(s, s, VEX_WIDTH - s * 2, VEX_HEIGHT - s * 2, i);
  }
  // Overlay thin outlined boxes
  for (int i = 0; i < 8; i++) {
    int s = 30 + i * 15;
    rectb(s, s, 40, 40, (i + 8) & 15);
  }
  // Row of small squares at the bottom
  for (int i = 0; i < 16; i++)
    rect(i * 20, VEX_HEIGHT - 24, 18, 18, i);
}

static void scene_circles(void) {
  cls(1);
  // Concentric circles at center
  for (int r = 80; r >= 5; r -= 5)
    circb(VEX_WIDTH / 2, VEX_HEIGHT / 2, r, (r / 5) & 15);

  // Filled circles at corners
  circ(30, 30, 25, 4);
  circ(VEX_WIDTH - 30, 30, 25, 6);
  circ(30, VEX_HEIGHT - 30, 25, 8);
  circ(VEX_WIDTH - 30, VEX_HEIGHT - 30, 25, 10);

  // Outlined circles overlapping each corner circle
  circb(30, 30, 30, 12);
  circb(VEX_WIDTH - 30, 30, 30, 12);
  circb(30, VEX_HEIGHT - 30, 30, 12);
  circb(VEX_WIDTH - 30, VEX_HEIGHT - 30, 30, 12);

  // Small dots along the center horizontal
  for (int x = 0; x < VEX_WIDTH; x += 8) {
    int r = (x % 16 < 8) ? 2 : 1;
    circ(x, VEX_HEIGHT / 2, r, (x / 8) & 15);
  }
}

static void scene_triangles(void) {
  cls(1);
  // Fan from top-center
  for (int i = 0; i < 20; i++) {
    int a1 = i * 18;
    int a2 = (i + 1) * 18;
    tri(160, 20,
        160 + (int)(130 * (a1 < 180 ? 1 : -1)),
        90 + (int)(80 * (a1 < 180 ? 1 : -1)),
        160 + (int)(130 * (a2 < 180 ? 1 : -1)),
        90 + (int)(80 * (a2 < 180 ? 1 : -1)),
        (i * 3) & 15);
  }
  // Outlined triangles in the lower area
  trib(20, 100, 100, 150, 60, 170, 12);
  trib(200, 100, 300, 150, 260, 170, 13);
  trib(100, 140, 220, 140, 160, 175, 14);
  // Degenerate: collinear
  tri(140, 160, 180, 160, 220, 160, 8);
  // Degenerate: zero-area (two points identical)
  tri(250, 160, 250, 160, 280, 175, 9);
}

static void scene_lines(void) {
  cls(1);
  // Star pattern from center
  for (int i = 0; i < 36; i++) {
    int a = i * 10;
    int len = (i & 1) ? 160 : 80;
    int dx = (int)(len * (a < 180 ? 1 : -1));
    int dy = (int)(len * (a < 180 ? 0 : 0));
    // approximate with simple 45-degree lines
    int x2 = 160;
    int y2 = 90;
    line(160, 90, x2 + (i % 4 == 0 ? 150 : i % 4 == 1 ? -150 : i % 4 == 2 ? 80 : -80),
         y2 + (i % 4 == 0 ? 30 : i % 4 == 1 ? -30 : i % 4 == 2 ? 150 : -150),
         (i * 4) & 15);
  }
  // Grid at the bottom
  for (int x = 0; x < VEX_WIDTH; x += 20)
    line(x, VEX_HEIGHT - 40, x, VEX_HEIGHT - 1, 4);
  for (int y = VEX_HEIGHT - 40; y < VEX_HEIGHT; y += 10)
    line(0, y, VEX_WIDTH - 1, y, 4);
  // Single-pixel lines (horizontal and vertical)
  line(0, VEX_HEIGHT - 42, VEX_WIDTH - 1, VEX_HEIGHT - 42, 12);
  line(0, VEX_HEIGHT - 2, VEX_WIDTH - 1, VEX_HEIGHT - 2, 12);
}

static void scene_text(void) {
  cls(1);
  // All printable ASCII
  text(" !\"#$%&'()*+,-./", 0, 0, 12);
  text("0123456789:;<=>?", 0, 10, 11);
  text("@ABCDEFGHIJKLMNO", 0, 20, 10);
  text("PQRSTUVWXYZ[\\]^_", 0, 30, 9);
  text("`abcdefghijklmno", 0, 40, 8);
  text("pqrstuvwxyz{|}~", 0, 50, 7);

  // Color test: each letter in a different palette color
  for (int i = 0; i < 16; i++) {
    char buf[2] = {(char)('A' + i), 0};
    text(buf, i * 20, 70, i);
  }

  // Long string across the screen
  text("The quick brown fox jumps over the lazy dog. 0123456789", 0, 90, 13);
  text("ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz", 0, 100, 14);

  // Single characters at extreme positions
  text("<", 0, 120, 2);
  text(">", VEX_WIDTH - 8, 120, 3);
  text("^", 80, VEX_HEIGHT - 8, 4);
  text("v", 100, 0, 5);
  text("X", 160, 90, 12);

  // Character at every screen position (via pset-like single-char text)
  for (int y = 130; y < 150; y += 10)
    for (int x = 0; x < VEX_WIDTH; x += 16)
      text(".", x, y, (x / 16 + y / 10) & 15);
}

static void scene_blit(void) {
  cls(1);
  // Tile the 4x4 checkerboard across the bottom-left
  for (int ty = 0; ty < 8; ty++)
    for (int tx = 0; tx < 12; tx++)
      blit(CHECK4, tx * 4, ty * 4, 4, 4, 0);

  // Smiley faces across the screen with different keys
  int keys[] = {0, 1, 2, 15, 16};
  for (int ki = 0; ki < 5; ki++) {
    blit(SMILE, 100 + ki * 40, 40, 8, 8, keys[ki]);
  }

  // Sprite stretched via w/h parameters
  blit(SMILE, 20, 80, 16, 16, 0);
  blit(SMILE, 60, 80, 8, 16, 1);
  blit(SMILE, 80, 80, 16, 8, 2);

  // Row of single-pixel blits at every palette index
  for (int i = 0; i < 16; i++) {
    unsigned char p = (unsigned char)i;
    blit(&p, i * 8, 130, 1, 1, 16);
  }

  // Full-width horizontal stripe via blit
  {
    unsigned char stripe[320];
    for (int i = 0; i < 320; i++) stripe[i] = (i / 8) & 15;
    blit(stripe, 0, 150, 320, 1, 16);
  }

  // Transparency test: overlapping blits with different keys
  unsigned char overlap_a[4] = {3, 3, 3, 3};
  unsigned char overlap_b[4] = {7, 7, 7, 7};
  blit(overlap_a, 50, 160, 2, 2, 0);
  blit(overlap_b, 51, 161, 2, 2, 0);
}

static void scene_palette(void) {
  cls(15);
  // Scene that exercises palette: draw, change, draw again
  // Phase 1: draw with default palette
  for (int i = 0; i < 16; i++)
    rect(i * 20, 0, 20, 60, i);

  // Phase 2: invert palette
  for (int i = 0; i < 16; i++)
    pal(i, (i * 0x111111) & 0xFFFFFF);

  for (int i = 0; i < 16; i++)
    rect(i * 20, 70, 20, 60, i);

  // Phase 3: reset and draw again
  palreset();
  for (int i = 0; i < 16; i++)
    rect(i * 20, 140, 20, 40, i);

  // Labels
  text("default", 0, 62, 12);
  text("edited", 0, 132, 12);
  text("reset", 0, 175, 12);
}

static void scene_tri_blit_mix(void) {
  // Mixed: triangles, blits, lines, circles all overlapping
  cls(1);

  // Background: gradient-style rectangles
  for (int i = 0; i < 16; i++)
    rect(0, i * 11, VEX_WIDTH, 11, i);

  // Overlapping filled triangles
  tri(0, 0, 160, 90, 0, 179, 4);
  tri(319, 0, 160, 90, 319, 179, 6);
  tri(0, 0, 319, 0, 160, 90, 8);
  tri(0, 179, 319, 179, 160, 90, 10);

  // Outlined triangles on top
  trib(40, 20, 120, 70, 80, 40, 12);
  trib(200, 120, 280, 70, 240, 150, 13);

  // Circles
  circ(80, 130, 30, 3);
  circb(80, 130, 35, 12);
  circ(240, 50, 25, 7);
  circb(240, 50, 30, 12);

  // Blit smiley at center
  blit(SMILE, 156, 86, 8, 8, 0);
  blit(SMILE, 152, 82, 16, 16, 1);
  blit(SMILE, 148, 78, 24, 24, 2);

  // Lines connecting corners to center
  line(0, 0, 160, 90, 12);
  line(319, 0, 160, 90, 12);
  line(0, 179, 160, 90, 12);
  line(319, 179, 160, 90, 12);

  // Text overlay
  text("STRESS", 120, 40, 14);
  text("TEST", 132, 52, 10);
}

static void scene_pset_fill(void) {
  // Fill every pixel individually with pset, cycling through palette
  cls(1);
  for (int y = 0; y < VEX_HEIGHT; y++)
    for (int x = 0; x < VEX_WIDTH; x++)
      pset(x, y, (x * 3 + y * 7) & 15);
  // Overdraw a cross in white
  for (int y = 0; y < VEX_HEIGHT; y++) pset(160, y, 12);
  for (int x = 0; x < VEX_WIDTH; x++) pset(x, 90, 12);
}

static void scene_lines_dense(void) {
  // Dense line grid: every combination of 8x8 lattice points
  cls(1);
  int step = 40;
  for (int y1 = 0; y1 < VEX_HEIGHT; y1 += step) {
    for (int x1 = 0; x1 < VEX_WIDTH; x1 += step) {
      for (int y2 = 0; y2 < VEX_HEIGHT; y2 += step) {
        for (int x2 = 0; x2 < VEX_WIDTH; x2 += step) {
          int c = ((x1 / step) + (y1 / step) + (x2 / step) + (y2 / step)) & 15;
          line(x1, y1, x2, y2, c);
        }
      }
    }
  }
}

VEX_EXPORT("boot") void boot(void) {
  frame = 0;
  scene = 0;
  title("vex - test_bench");
}

static void dispatch_scene(int s) {
  switch (s) {
  case 0:  scene_color_bars();  break;
  case 1:  scene_pset_grid();   break;
  case 2:  scene_rects();       break;
  case 3:  scene_circles();     break;
  case 4:  scene_triangles();   break;
  case 5:  scene_lines();       break;
  case 6:  scene_text();        break;
  case 7:  scene_blit();        break;
  case 8:  scene_palette();     break;
  case 9:  scene_tri_blit_mix(); break;
  case 10: scene_pset_fill();   break;
  }
}

VEX_EXPORT("update") void update(void) {
  if (scene >= NUM_SCENES) {
    cls(0);
    text("DONE", VEX_WIDTH / 2 - 12, VEX_HEIGHT / 2 - 4, 12);
    return;
  }

  if (frame == 0) {
    dispatch_scene(scene);
  }

  pset(0, 0, 15);
  pset(1, 0, 15);
  pset(0, 1, 15);
  pset(1, 1, 15);

  frame++;
  if (frame >= BENCH_FRAMES) {
    frame = 0;
    scene++;
  }
}
