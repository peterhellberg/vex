// Hostile-input cart: hammers every host API with extreme or out-of-range
// arguments. No host may trap, panic, read out of bounds, or stall -- calls
// must either clamp, clip, wrap, or be dropped. Run it for a few hundred
// frames on each host as a regression guard for the input-hardening guards.
#include "vex.h"

static unsigned char pix[64]; // 8x8 blit source

static const char LONG_TEXT[] =
    "0123456789012345678901234567890123456789012345678901234567890123456789"
    "0123456789012345678901234567890123456789012345678901234567890123456789"
    "01234567890123456789"; // 190 chars: text() truncates at 127

static const char LONG_TITLE[] =
    "t-01234567890123456789012345678901234567890123456789012345678901234567"
    "8901234567890123456789012345678901234567890123456789012345678901234567"
    "89"; // > 127 chars: title() truncates at 127

VEX_EXPORT("boot") void boot(void) {
  pal(-1, 0xFF00FF); // negative index wraps & 15
  pal(16, 0x123456); // out-of-range index wraps & 15
  pal(3, -1);        // bogus color value
  palreset();
  title(LONG_TITLE);
}

VEX_EXPORT("update") void update(void) {
  static int frame;
  int f = ++frame;

  cls(f & 15);

  // Out-of-range buttons and mouse buttons.
  btn(-1);
  btn(6);
  btn(1 << 30);
  btnp(-1);
  btnp(7);
  mbtn(-1);
  mbtn(3);
  mbtn(255);

  mx();
  my();

  // Hostile geometry: absurd sizes and coordinates.
  rect(-32000, -32000, 64000, 64000, 1);
  rectb(160, 90, 1 << 30, 1 << 30, 2);
  pset(1 << 20, 1 << 20, 15);
  circ(0, 0, 1 << 24, 3);
  circb(-5000, -5000, 9999, 4);
  line(-9999, 9999, 9999, -9999, 5);
  tri(0, -6000, 6000, 6000, -6000, 6000, 6);
  trib(7000, 7000, -7000, -7000, 7000, -7000, 7);

  // blit: zero dims, out-of-range key (>255 must draw every pixel), and
  // far-off-screen placement.
  blit(pix, 0, 0, 0, 0, 0);
  blit(pix, 0, 0, 8, 8, 256);
  blit(pix, 99999, 99999, 300, 300, 999);

  // Strings longer than the documented truncation point.
  text(LONG_TEXT, 0, 170, 8);

  // Hostile audio arguments: clamps, degenerate slides, absurd envelopes.
  tone(0, 0, 0, 0);
  tone(-42, -1, -1, -1);
  tone(100000, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF);
  tone(262 | (999999 << 16), 0, 0, TONE_FLAGS(9, 99, ~0));
  palreset();
}
