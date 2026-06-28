// Stress test: heavy i64 arithmetic (PRNG, multiply chains, division).
// Directly exercises the op_i64_Multiply_ss path that triggered the
// original signed-overflow crash. Also tests division by edge cases.
#include "vex.h"

// PCG family constants — same values that triggered the overflow:
//   state * 6364136223846793005 + 1442695040888963407
// These produce wrapping i64.mul which wasm3 must handle correctly.
#define PCG_MUL 6364136223846793005ULL
#define PCG_INC 1442695040888963407ULL

static unsigned long long state;

static unsigned long long prng(void) {
  state = state * PCG_MUL + PCG_INC;
  return state;
}

static int rnd(int n) {
  if (n <= 0) return 0;
  return (int)((prng() >> 32) % (unsigned long long)n);
}

static int rand_range(int lo, int hi) {
  if (lo >= hi) return lo;
  return lo + rnd(hi - lo + 1);
}

// Exercise i64 multiply chains that the compiler cannot constant-fold
// because results depend on frame counter and cumulative state.
static long long mul_chain(long long a, long long b, int depth) {
  long long r = a;
  for (int i = 0; i < depth; i++) {
    r = r * b + a;
    b = b * a + r;
    a = a * b - r;
    // rotate so next iteration has fresh values
    long long t = a;
    if (r < 0) a = -r; else a = r;
    if (t < 0) b = -t; else b = t;
  }
  return r;
}

static int mul_stress_iter;

VEX_EXPORT("boot") void boot(void) {
  state = 42;
  mul_stress_iter = 0;
  title("vex - test_arith");
}

VEX_EXPORT("update") void update(void) {
  // Phase 1: PRNG stress — run the PCG generator many times per frame,
  // using results for drawing. This is the exact pattern that crashed.
  cls(0);

  // Draw 200 randomly-placed pixels using PRNG
  for (int i = 0; i < 200; i++) {
    int x = rnd(VEX_WIDTH);
    int y = rnd(VEX_HEIGHT);
    int c = rnd(16);
    pset(x, y, c);
  }

  // Draw 50 random small rects
  for (int i = 0; i < 50; i++) {
    int x = rnd(VEX_WIDTH - 10);
    int y = rnd(VEX_HEIGHT - 10);
    int w = rnd(10) + 1;
    int h = rnd(10) + 1;
    int c = rnd(16);
    rect(x, y, w, h, c);
  }

  // Draw 30 random circles
  for (int i = 0; i < 30; i++) {
    int x = rnd(VEX_WIDTH);
    int y = rnd(VEX_HEIGHT);
    int r = rnd(12);
    int c = rnd(16);
    circ(x, y, r, c);
  }

  // Draw 20 random triangles
  for (int i = 0; i < 20; i++) {
    int x1 = rnd(VEX_WIDTH), y1 = rnd(VEX_HEIGHT);
    int x2 = rnd(VEX_WIDTH), y2 = rnd(VEX_HEIGHT);
    int x3 = rnd(VEX_WIDTH), y3 = rnd(VEX_HEIGHT);
    int c = rnd(16);
    if (i & 1) tri(x1, y1, x2, y2, x3, y3, c);
    else       trib(x1, y1, x2, y2, x3, y3, c);
  }

  // Phase 2: i64 multiply chain stress (runs across frames).
  // Uses values derived from frame counter so the chain isn't constant.
  long long a = (long long)mul_stress_iter * 6364136223846793005LL;
  long long b = (long long)(mul_stress_iter + 1) * 1442695040888963407LL;
  long long r = mul_chain(a ^ 12345, b ^ 67890, 30);
  // Use the result to draw something so it can't be optimized away
  int px = (int)((r >> 32) % VEX_WIDTH);
  int py = (int)((r >> 16) % VEX_HEIGHT);
  pset(px, py, 15);
  text("*", px, py, 15);

  // Phase 3: edge-case i64 arithmetic — division by edge values
  {
    // i64.div_s and i64.rem_s with edge-case divisors
    long long edge_divs[] = {1, -1, 2, -2, 7, -7};
    for (int i = 0; i < 6; i++) {
      long long d = edge_divs[i];
      long long q = (long long)mul_stress_iter / d;
      long long rem = (long long)mul_stress_iter % d;
      int sx = (int)(q % VEX_WIDTH);
      int sy = (int)((rem + mul_stress_iter * 7) % VEX_HEIGHT);
      if (sx >= 0 && sy >= 0)
        pset(sx, sy, i + 1);
    }
  }

  // Phase 4: edge-case division with large signed values
  {
    long long vals[] = {
      9223372036854775807LL,      // INT64_MAX
      -9223372036854775807LL - 1, // INT64_MIN
      0x7FFFFFFFFFFFFFFFLL,
    };
    // INT64_MIN / -1 and INT64_MIN % -1 trap in wasm — skip those
    // Test division of large values by small values
    long long d2 = mul_stress_iter + 1; // non-zero, positive
    for (int i = 0; i < 3; i++) {
      long long q2 = vals[i] / d2;
      long long r2 = vals[i] % d2;
      int sx = (int)(q2 % VEX_WIDTH);
      int sy = (int)((r2 + mul_stress_iter) % VEX_HEIGHT);
      if (sx >= 0 && sy >= 0)
        pset(sx, sy, 9 + i);
    }
  }

  // Phase 5: i64 shift operations
  {
    unsigned long long sh = state;
    for (int i = 0; i < 64; i++) {
      sh = (sh << 1) | (sh >> 63);  // rotate left
      sh ^= PCG_MUL;
      sh = sh * PCG_MUL + PCG_INC;
    }
    state = sh; // preserve across frames
    int sx = (int)((sh >> 32) % VEX_WIDTH);
    int sy = (int)((sh >> 16) % VEX_HEIGHT);
    pset(sx, sy, 14);
  }

  // Phase 6: i64 shifts by large immediate values (unsigned, to avoid UB)
  {
    unsigned long long uv = (unsigned long long)(mul_stress_iter * 0x12345678LL);
    // Mask shift amounts to avoid C UB (wasm defines v >> (amount % 64))
    unsigned long long s1 = uv >> 63;    // wasm: uv >> (127 % 64)
    unsigned long long s2 = uv >> 8;     // wasm: uv >> (200 % 64)
    unsigned long long s3 = uv << 8;     // wasm: uv << (200 % 64)
    int sx = (int)(s1 % 20) + 150;
    int sy = (int)(s2 % 20) + 100;
    if (sx >= 0 && sy >= 0) pset(sx, sy, 11);
    int sx3 = (int)(s3 % 20) + 200;
    if (sx3 >= 0) pset(sx3, 100, 12);
  }

  // Show frame counter
  text("F", 0, 0, 12);
  text("R", 8, 0, 13);
  text("M", 16, 0, 14);

  mul_stress_iter++;
  if (mul_stress_iter >= 500) {
    // After 500 frames, park on a "done" screen
    cls(0);
    text("DONE", VEX_WIDTH / 2 - 12, VEX_HEIGHT / 2 - 4, 12);
  }
}
