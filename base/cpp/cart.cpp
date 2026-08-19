#include "vex.h"

struct Ball {
    float x, y, vx, vy;
    int r, color;

    Ball(float x, float y, float vx, float vy, int r, int color)
        : x(x), y(y), vx(vx), vy(vy), r(r), color(color) {}

    void update() {
        x += vx;
        y += vy;
        if (x < r || x > VEX_WIDTH - r)  vx = -vx;
        if (y < r || y > VEX_HEIGHT - r) vy = -vy;
    }

    void draw() const { circ((int)x, (int)y, r, color); }
};

static Ball balls[] = {
    { 80,  90, 0.7f,  1.1f, 6,  2},
    {200,  50, 1.3f,  0.8f, 4,  4},
    {160, 140, -1.0f, -1.2f, 5,  6},
    { 40, 160, 0.9f, -0.7f, 3, 14},
};

VEX_EXPORT("boot") void boot(void) { title("vex - C++"); }

VEX_EXPORT("update") void update(void) {
    cls(0);

    for (auto& b : balls) {
        b.update();
        b.draw();
    }

    text("VEX C++", 8, 8, 12);
}
