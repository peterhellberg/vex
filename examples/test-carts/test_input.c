// Input tape test cart: draws a pixel whose colour reports the held button,
// btnp() edge and pointer state, so a headless -i run's framebuffer hash
// changes with the tape. Kept minimal and deterministic.
#include "vex.h"

VEX_EXPORT("update") void update(void) {
  int c = 0;
  for (int b = 0; b < 6; b++) {
    if (btn(b)) c += b + 1;
    if (btnp(b)) c += 16;
  }
  if (mbtn(0)) c += 32;
  c += (mx() + my()) % 7;

  cls(0);
  rect(0, 0, VEX_WIDTH, VEX_HEIGHT, c & 15);
}
