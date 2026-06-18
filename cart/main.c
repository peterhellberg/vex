// Example vex cart: move a player square with the arrow keys, press A (Z) to
// change its fill color. Demonstrates the whole drawing/input API.
#include "vex.h"

#define PLAYER 15 // player square size, in pixels

static int px, py;

VEX_EXPORT("boot") void boot(void) {
  px = (VEX_WIDTH - PLAYER) / 2;
  py = (VEX_HEIGHT - PLAYER) / 2;
}

VEX_EXPORT("update") void update(void) {
  if (btn(VEX_LEFT) && px > 0)
    px--;
  if (btn(VEX_RIGHT) && px < VEX_WIDTH - PLAYER)
    px++;
  if (btn(VEX_UP) && py > 0)
    py--;
  if (btn(VEX_DOWN) && py < VEX_HEIGHT - PLAYER)
    py++;

  cls(1); // dark blue background
  text("VEX", 4, 4, 7);
  text("ARROWS + Z", 4, 14, 13);

  circ(VEX_WIDTH / 2, 40, 12, 9); // orange sun
  line(0, VEX_HEIGHT - 1, VEX_WIDTH - 1, VEX_HEIGHT - 1, 3);

  // Player: a filled square (red while A held, otherwise green) with a white
  // border drawn around the same square.
  int fill = btn(VEX_A) ? 8 : 11;
  rect(px, py, PLAYER, PLAYER, fill);
  rectb(px, py, PLAYER, PLAYER, 7);
}
