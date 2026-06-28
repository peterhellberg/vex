// Stress test: palette operations — pal() and palreset().
// Exercises rapid palette changes interleaved with drawing, index
// clamping, and concurrent boot()/reload palette initialization.
#include "vex.h"

#define FRAMES_PER_PHASE 30
#define NUM_PHASES 5

static int frame;
static int phase;

VEX_EXPORT("boot") void boot(void) {
  frame = 0;
  phase = 0;
  title("vex - test_palette");
}

VEX_EXPORT("update") void update(void) {
  cls(0);

  int p = phase % NUM_PHASES;

  switch (p) {
  case 0: // cycle every palette entry through all 24-bit colors every frame
    for (int i = 0; i < 16; i++) {
      int rgb = (i * 17) << 16 | (i * 33) << 8 | (i * 7);
      pal(i, rgb);
    }
    break;
  case 1: // set all entries to the same color, then draw
    for (int i = 0; i < 16; i++) pal(i, 0xFF0000);
    rect(0, 0, VEX_WIDTH, VEX_HEIGHT, 7);
    for (int i = 0; i < 16; i++) pal(i, 0x00FF00);
    rect(10, 10, VEX_WIDTH - 20, VEX_HEIGHT - 20, 7);
    for (int i = 0; i < 16; i++) pal(i, 0x0000FF);
    rect(20, 20, VEX_WIDTH - 40, VEX_HEIGHT - 40, 7);
    break;
  case 2: // pal() with edge-case index values (beyond 0..15)
    pal(0, 0xFFFFFF);
    pal(15, 0x000000);
    pal(-1, 0xFF00FF);  // clamped to 15 by (unsigned)&15
    pal(16, 0x00FFFF);  // clamped to 0
    pal(255, 0xFFFF00); // clamped to 15
    pal(-128, 0xFF0000); // clamped to 0
    // Draw stripes using every index — the clamping above should only
    // affect entries 0 and 15.
    for (int i = 0; i < 16; i++) {
      rect(i * 20, 0, 20, VEX_HEIGHT / 2, i);
    }
    // Restore defaults for indices we care about
    pal(0, 0x1A1C2C);
    pal(15, 0x333C57);
    break;
  case 3: // rapid interleaved pal/palreset
    for (int i = 0; i < 20; i++) {
      pal(i & 15, 0xFF8800);
      palreset();
      pal(i & 15, 0x00AAFF);
    }
    // Draw with index 0 — should be default (dark blue) after the final palreset
    rect(0, 0, VEX_WIDTH, VEX_HEIGHT, 0);
    break;
  case 4: // pal between every drawing operation
    for (int i = 0; i < 16; i++) {
      pal(i, 0x101010 * (i + 1) & 0xFFFFFF);
    }
    for (int y = 0; y < VEX_HEIGHT; y += 12) {
      pal(0, 0xFF0000);
      rect(0, y, VEX_WIDTH, 1, 1);
      pal(0, 0x00FF00);
      rect(0, y + 2, VEX_WIDTH, 1, 2);
      pal(0, 0x0000FF);
      rect(0, y + 4, VEX_WIDTH, 1, 3);
    }
    break;
  }

  text("P", 0, VEX_HEIGHT - 8, 12);

  frame++;
  if (frame >= FRAMES_PER_PHASE) {
    frame = 0;
    phase++;
    palreset(); // reset before next phase
  }

  if (phase >= NUM_PHASES * 3) { // repeat 3x, then done
    cls(0);
    text("DONE", VEX_WIDTH / 2 - 12, VEX_HEIGHT / 2 - 4, 12);
  }
}
