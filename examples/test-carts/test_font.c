#include "vex.h"

VEX_EXPORT("boot") void boot(void) {
  title("vex - font gallery");
}

VEX_EXPORT("update") void update(void) {
  cls(0);

  text("ascii:", 0, 0, 14);
  for (int col = 0; col < 6; col++) {
    char buf[] = {' ', "234567"[col], 'x', 0};
    text(buf, 36 + col * 44, 0, 14);
  }

  for (int i = 32; i < 128; i++) {
    int col = (i >> 4) - 2;
    int row = i & 15;
    char c[2] = {(char)i, 0};
    text(c, 36 + col * 44, 12 + row * 10, 12);
  }

  for (int r = 0; r < 16; r++) {
    char buf[] = {"0123456789ABCDEF"[r], 'x', ':', 0};
    text(buf, 0, 12 + r * 10, 14);
  }
}
