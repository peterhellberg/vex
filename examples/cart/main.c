// Example vex cart: move a player square with the arrow keys, press A (Z) to
// change its fill color. Demonstrates the whole drawing/input API.
#include "vex.h"

#define PLAYER 18 // player square size, in pixels

static int px, py;

VEX_EXPORT("boot") void boot(void) {
  title("vex - C cart");
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

  cls(0); // dark background

  // Subtle guide line, behind everything: player center -> bottom center.
  line(px + PLAYER / 2, py + PLAYER / 2, VEX_WIDTH / 2, VEX_HEIGHT - 1, 15);

  text("VEX", 6, 6, 12);          // white
  text("ARROWS + Z", 6, 18, 13);  // muted blue-grey

  circ(264, 40, 14, 4);     // yellow sun (upper right)
  ring(264, 40, 17, 19, 3); // orange ring around it
  circb(60, 44, 9, 13);     // outlined moon

  // Mountain range across the width: alternating filled and outlined peaks.
  tri(0, VEX_HEIGHT - 1, 48, VEX_HEIGHT - 60, 96, VEX_HEIGHT - 1, 14);
  trib(72, VEX_HEIGHT - 1, 132, VEX_HEIGHT - 84, 192, VEX_HEIGHT - 1, 12);
  tri(150, VEX_HEIGHT - 1, 210, VEX_HEIGHT - 52, 270, VEX_HEIGHT - 1, 14);
  trib(248, VEX_HEIGHT - 1, 296, VEX_HEIGHT - 72, 319, VEX_HEIGHT - 1, 12);

  // Player: a filled square (red while A held, otherwise green) with a white
  // border drawn around the same square.
  int fill = btn(VEX_A) ? 2 : 5;
  rect(px, py, PLAYER, PLAYER, fill);
  rectb(px, py, PLAYER, PLAYER, 12);

  // Mouse cursor: white dot, red while the left button is held.
  circ(mx(), my(), 3, mbtn(VEX_MOUSE_LEFT) ? 2 : 12);
}
