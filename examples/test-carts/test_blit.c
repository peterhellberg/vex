#include "vex.h"

#define FRAMES_PER_CASE 300
#define NUM_CASES 16

static const char *CASE_NAMES[NUM_CASES] = {
    "1x1 blits, every 997th pixel",
    "full-screen solid (key outside palette)",
    "full-screen with transparency key",
    "all-transparent blit",
    "tiny 2x2 blits across screen",
    "blit with every palette index as key",
    "clipped at negative coords",
    "zero/negative dimensions",
    "key = -1 (unsigned wraparound)",
    "key = INT32_MIN / INT32_MAX",
    "w/h exceeding VEX_W/VEX_H",
    "1px tall full-width, 1px wide full-height",
    "blitm identity remap",
    "blitm swap indices 1<->2",
    "blitm invert all indices",
    "blitm remap with transparency key",
};

static int frame;
static int phase;
static unsigned char big_data[320 * 180];

static void fill_data(unsigned char *d, int w, int h, int pat) {
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      switch (pat) {
      case 0:
        d[y * w + x] = (x + y) & 15;
        break;
      case 1:
        d[y * w + x] = (x * 7 + y * 13) & 15;
        break;
      case 2:
        d[y * w + x] = 5;
        break;
      case 3:
        d[y * w + x] = x < w / 2 ? 0 : 7;
        break;
      case 4:
        d[y * w + x] = y < h / 2 ? 0 : 8;
        break;
      case 5:
        d[y * w + x] = 0;
        break;
      }
    }
}

static void fill_sprite_shape(unsigned char *d, int w, int h) {
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      int dx, dy, dist;
      d[y * w + x] = 0;
      dx = x - w / 2;
      dy = y - h / 2;
      dist = dx * dx + dy * dy;
      if (dist < (w / 3) * (w / 3))
        d[y * w + x] = 4;
      if (x >= w / 3 && x < w * 2 / 3 && y >= h / 3 && y < h * 2 / 3)
        d[y * w + x] = 2;
      if (x >= 2 && x < w - 2 && y == x)
        d[y * w + x] = 12;
    }
}

static void draw_case(int c) {
  switch (c) {
  case 0:
    for (int i = 0; i < VEX_WIDTH * VEX_HEIGHT; i += 997) {
      int x = i % VEX_WIDTH;
      int y = (i / VEX_WIDTH) % VEX_HEIGHT;
      unsigned char p = (unsigned char)((x + y) & 15);
      blit(&p, x, y, 1, 1, 16);
    }
    break;
  case 1:
    fill_data(big_data, VEX_WIDTH, VEX_HEIGHT, 2);
    blit(big_data, 0, 0, VEX_WIDTH, VEX_HEIGHT, 16);
    break;
  case 2:
    fill_data(big_data, VEX_WIDTH, VEX_HEIGHT, 3);
    blit(big_data, 0, 0, VEX_WIDTH, VEX_HEIGHT, 0);
    break;
  case 3:
    fill_data(big_data, VEX_WIDTH, VEX_HEIGHT, 5);
    blit(big_data, 0, 0, VEX_WIDTH, VEX_HEIGHT, 0);
    break;
  case 4: {
    unsigned char d[4] = {1, 2, 3, 4};
    for (int y = 0; y < VEX_HEIGHT; y += 10)
      for (int x = 0; x < VEX_WIDTH; x += 10)
        blit(d, x, y, 2, 2, 4);
  } break;
  case 5: {
    unsigned char d[16];
    for (int i = 0; i < 16; i++)
      d[i] = i;
    for (int k = 0; k < 17; k++)
      blit(d, k * 18, 0, 16, 1, k);
  } break;
  case 6: {
    fill_data(big_data, 50, 50, 0);
    blit(big_data, -25, -25, 50, 50, 15);
    blit(big_data, VEX_WIDTH - 25, -25, 50, 50, 15);
    blit(big_data, -25, VEX_HEIGHT - 25, 50, 50, 15);
    blit(big_data, VEX_WIDTH - 25, VEX_HEIGHT - 25, 50, 50, 15);
  } break;
  case 7: {
    unsigned char d[1] = {3};
    blit(d, 100, 100, 0, 0, 0);
    blit(d, 120, 100, -1, -1, 0);
    blit(d, 140, 100, 10, 0, 0);
    blit(d, 160, 100, 0, 10, 0);
    rect(100, 100, 80, 10, 4);
  } break;
  case 8: {
    unsigned char d[25];
    for (int i = 0; i < 25; i++)
      d[i] = i & 15;
    blit(d, 0, 0, 5, 5, -1);
    blit(d, 50, 0, 5, 5, 255);
  } break;
  case 9: {
    unsigned char d[4] = {5, 6, 7, 8};
    blit(d, 110, 0, 2, 2, -2147483648);
    blit(d, 130, 0, 2, 2, 2147483647);
  } break;
  case 10: {
    fill_data(big_data, VEX_WIDTH, VEX_HEIGHT, 0);
    blit(big_data, 0, 40, 9999, 9999, 16);
    blit(big_data, 0, 50, 9999, 5, 16);
    blit(big_data, 0, 60, 5, 9999, 16);
  } break;
  case 11: {
    unsigned char row[320];
    for (int i = 0; i < 320; i++)
      row[i] = (i / 20) & 15;
    blit(row, 0, 80, 320, 1, 16);
    unsigned char col[180];
    for (int i = 0; i < 180; i++)
      col[i] = (i / 12) & 15;
    blit(col, 310, 0, 1, 180, 16);
  } break;
  case 12: {
    unsigned char spr[32 * 32];
    unsigned char id[16];
    int i;
    fill_sprite_shape(spr, 32, 32);
    for (i = 0; i < 16; i++) id[i] = i;
    text("blit", 24, 22, 12);
    blit(spr, 32, 30, 32, 32, 16);
    text("blitm id", 116, 22, 12);
    blitm(spr, 128, 30, 32, 32, 16, id);
    text("blitm +3", 224, 22, 12);
    unsigned char sh[16];
    for (i = 0; i < 16; i++) sh[i] = (i + 3) & 15;
    blitm(spr, 224, 30, 32, 32, 16, sh);
  } break;
  case 13: {
    unsigned char spr[32 * 32];
    unsigned char id[16];
    unsigned char sw[16];
    int i;
    fill_sprite_shape(spr, 32, 32);
    for (i = 0; i < 16; i++) { id[i] = i; sw[i] = i; }
    sw[2] = 12;
    sw[12] = 2;
    text("blit", 24, 22, 12);
    blit(spr, 32, 30, 32, 32, 16);
    text("blitm id", 116, 22, 12);
    blitm(spr, 128, 30, 32, 32, 16, id);
    text("blitm 2<->12", 206, 22, 12);
    blitm(spr, 224, 30, 32, 32, 16, sw);
  } break;
  case 14: {
    unsigned char spr[32 * 32];
    unsigned char id[16];
    unsigned char inv[16];
    int i;
    fill_sprite_shape(spr, 32, 32);
    for (i = 0; i < 16; i++) { id[i] = i; inv[i] = 15 - i; }
    text("blit", 24, 22, 12);
    blit(spr, 32, 30, 32, 32, 16);
    text("blitm id", 116, 22, 12);
    blitm(spr, 128, 30, 32, 32, 16, id);
    text("blitm inv", 222, 22, 12);
    blitm(spr, 224, 30, 32, 32, 16, inv);
  } break;
  case 15: {
    unsigned char spr[32 * 32];
    unsigned char inv[16];
    int i;
    fill_sprite_shape(spr, 32, 32);
    for (i = 0; i < 16; i++) inv[i] = 15 - i;
    text("blit k=0", 18, 22, 12);
    blit(spr, 32, 30, 32, 32, 0);
    text("inv k=0", 128, 22, 12);
    blitm(spr, 128, 30, 32, 32, 0, inv);
    text("inv k=15", 222, 22, 12);
    blitm(spr, 224, 30, 32, 32, 15, inv);
  } break;
  }
}

static void draw_header(void) {
  rect(0, 0, VEX_WIDTH, 8, 1);
  text("B", 2, 0, 12);
  text("LIT", 10, 0, 12);
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
  title("vex - test_blit");
}

VEX_EXPORT("update") void update(void) {
  if (btnp(VEX_RIGHT) && phase < NUM_CASES - 1) {
    phase++;
    frame = 0;
    cls(0);
    draw_case(phase);
    draw_header();
    return;
  }
  if (btnp(VEX_LEFT) && phase > 0) {
    phase--;
    frame = 0;
    cls(0);
    draw_case(phase);
    draw_header();
    return;
  }

  if (phase >= NUM_CASES) {
    cls(0);
    text("DONE - test_blit", 88, VEX_HEIGHT / 2 - 4, 12);
    return;
  }

  if (frame == 0) {
    cls(0);
    draw_case(phase);
    draw_header();
  }

  frame++;
  if (frame >= FRAMES_PER_CASE) {
    frame = 0;
    phase++;
  }
}
