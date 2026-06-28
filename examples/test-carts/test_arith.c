#include "vex.h"

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

static long long mul_chain(long long a, long long b, int depth) {
  long long r = a;
  for (int i = 0; i < depth; i++) {
    r = r * b + a;
    b = b * a + r;
    a = a * b - r;
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
  cls(0);

  rect(0, 0, VEX_WIDTH, 8, 1);
  text("A", 2, 0, 12);
  text("RITH", 10, 0, 12);

  text("PRNG shapes", 2, 10, 14);
  for (int i = 0; i < 200; i++) {
    int x = rnd(VEX_WIDTH);
    int y = rnd(VEX_HEIGHT);
    int c = rnd(16);
    pset(x, y, c);
  }

  for (int i = 0; i < 50; i++) {
    int x = rnd(VEX_WIDTH - 10);
    int y = rnd(VEX_HEIGHT - 10);
    int w = rnd(10) + 1;
    int h = rnd(10) + 1;
    int c = rnd(16);
    rect(x, y, w, h, c);
  }

  for (int i = 0; i < 30; i++) {
    int x = rnd(VEX_WIDTH);
    int y = rnd(VEX_HEIGHT);
    int r = rnd(12);
    int c = rnd(16);
    circ(x, y, r, c);
  }

  for (int i = 0; i < 20; i++) {
    int x1 = rnd(VEX_WIDTH), y1 = rnd(VEX_HEIGHT);
    int x2 = rnd(VEX_WIDTH), y2 = rnd(VEX_HEIGHT);
    int x3 = rnd(VEX_WIDTH), y3 = rnd(VEX_HEIGHT);
    int c = rnd(16);
    if (i & 1) tri(x1, y1, x2, y2, x3, y3, c);
    else       trib(x1, y1, x2, y2, x3, y3, c);
  }

  rect(150, 50, 20, 20, 1);
  text("i64 mul chain", 152, 52, 14);

  long long a = (long long)mul_stress_iter * 6364136223846793005LL;
  long long b = (long long)(mul_stress_iter + 1) * 1442695040888963407LL;
  long long r = mul_chain(a ^ 12345, b ^ 67890, 30);
  int px = (int)((r >> 32) % VEX_WIDTH);
  int py = (int)((r >> 16) % VEX_HEIGHT);
  pset(px, py, 15);
  text("*", px, py, 15);

  {
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

  {
    long long vals[] = {
      9223372036854775807LL,
      -9223372036854775807LL - 1,
      0x7FFFFFFFFFFFFFFFLL,
    };
    long long d2 = mul_stress_iter + 1;
    for (int i = 0; i < 3; i++) {
      long long q2 = vals[i] / d2;
      long long r2 = vals[i] % d2;
      int sx = (int)(q2 % VEX_WIDTH);
      int sy = (int)((r2 + mul_stress_iter) % VEX_HEIGHT);
      if (sx >= 0 && sy >= 0)
        pset(sx, sy, 9 + i);
    }
  }

  {
    unsigned long long sh = state;
    for (int i = 0; i < 64; i++) {
      sh = (sh << 1) | (sh >> 63);
      sh ^= PCG_MUL;
      sh = sh * PCG_MUL + PCG_INC;
    }
    state = sh;
    int sx = (int)((sh >> 32) % VEX_WIDTH);
    int sy = (int)((sh >> 16) % VEX_HEIGHT);
    pset(sx, sy, 14);
  }

  {
    unsigned long long uv = (unsigned long long)(mul_stress_iter * 0x12345678LL);
    unsigned long long s1 = uv >> 63;
    unsigned long long s2 = uv >> 8;
    unsigned long long s3 = uv << 8;
    int sx = (int)(s1 % 20) + 150;
    int sy = (int)(s2 % 20) + 100;
    if (sx >= 0 && sy >= 0) pset(sx, sy, 11);
    int sx3 = (int)(s3 % 20) + 200;
    if (sx3 >= 0) pset(sx3, 100, 12);
  }

  text("F", VEX_WIDTH - 48, 0, 12);
  text("R", VEX_WIDTH - 40, 0, 13);
  text("M", VEX_WIDTH - 32, 0, 14);

  mul_stress_iter++;
  if (mul_stress_iter >= 500) {
    cls(0);
    text("DONE - test_arith", 80, VEX_HEIGHT / 2 - 4, 12);
  }
}
