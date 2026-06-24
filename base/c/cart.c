#include "vex.h"

VEX_EXPORT("update") void update(void) {
  cls(8);                  // clear to dark blue
  text("VEX C", 8, 8, 12); // white text
  rect(60, 60, 8, 8, 6);   // green square
}
