# vex

A minimal WASM-based fantasy console in a single `main.c`.

The console is the **host**: it opens a [raylib](https://www.raylib.com/)
window, loads a `.wasm` *cart*, links a tiny drawing/input API the cart
imports, and calls the cart's exported `update()` once per frame. Carts draw
into a fixed **128×128** framebuffer with a **16-color** [SWEETIE-16](https://lospec.com/palette-list/sweetie-16)
palette (overridable at runtime), scaled up to the window with
nearest-neighbour filtering.

- **Runtime:** [wasm3](https://github.com/wasm3/wasm3) — the simplest
  embeddable WASM interpreter (pure C, MIT). Only its core files are compiled.
- **Build:** the Zig toolchain. [`build.zig`](build.zig) builds the C host,
  the C cart, and the Zig cart.
- **Graphics/input:** [raylib](https://www.raylib.com/).

Both dependencies are pulled in via [`build.zig.zon`](build.zig.zon) and built
from source, so the only requirement is `zig` — no system packages.

## Build & run

Requires only `zig` (0.17-dev). Dependencies are fetched on first build.

```sh
zig build           # build ./vex + cart.wasm + zcart.wasm (into zig-out/bin)
zig build run       # build, then run the C example cart
zig build runz      # build, then run the Zig example cart
```

A `Makefile` wraps these as `make`, `make run`, `make runz`, and `make clean`.

There are two example carts: [`cart/main.c`](cart/main.c) (C) and
[`zcart/main.zig`](zcart/main.zig) (Zig). Both compile to `wasm32` and use the
same console API.

## Controls

| Key | Action |
|-----|--------|
| `Super`+`Enter` | toggle fullscreen |
| `Super`+`I` | toggle integer scaling (crisp pixels vs. fill the screen; on by default in fullscreen) |
| `Esc` | quit |

`Super` is the Cmd key on macOS and the Super/Windows key on Linux. Arrow
keys, `Z`, and `X` are passed to the cart via `btn()`.

## Writing a cart

A cart is any `wasm32` module that exports `update()` (and optionally
`boot()`) and imports the API from module `env`. In C, include
[`vex.h`](vex.h):

```c
#include "vex.h"

VEX_EXPORT("update") void update(void) {
    cls(1);                 // clear to dark blue
    text("HELLO", 4, 4, 7); // white text
    rect(60, 60, 8, 8, 11); // green square
}
```

Compile it to wasm:

```sh
zig cc --target=wasm32-freestanding -nostdlib -O2 -Wl,--no-entry -I. \
       -o mycart.wasm mycart.c
./vex mycart.wasm
```

Or in Zig — declare the API as `extern "env"` imports and `export` the entry
points:

```zig
extern "env" fn cls(color: i32) void;
extern "env" fn text(s: [*:0]const u8, x: i32, y: i32, color: i32) void;

export fn update() void {
    cls(1);
    text("HELLO", 4, 4, 7);
}
```

```sh
zig build-exe mycart.zig -target wasm32-freestanding -O ReleaseSmall \
    -fno-entry -rdynamic -femit-bin=mycart.wasm
./vex mycart.wasm
```

## API

| Function | Description |
|----------|-------------|
| `cls(color)` | clear the screen |
| `pset(x, y, color)` | set one pixel |
| `rect(x, y, w, h, color)` | filled rectangle |
| `rectb(x, y, w, h, color)` | rectangle outline |
| `circ(x, y, r, color)` | filled circle |
| `circb(x, y, r, color)` | circle outline |
| `ring(x, y, inner, outer, color)` | filled ring (annulus) |
| `line(x0, y0, x1, y1, color)` | line |
| `tri(x1, y1, x2, y2, x3, y3, color)` | filled triangle |
| `trib(x1, y1, x2, y2, x3, y3, color)` | triangle outline |
| `text(s, x, y, color)` | draw a string |
| `btn(button) -> int` | `1` if a button is held, else `0` |
| `pal(index, rgb)` | override palette entry `index` (0..15) with a packed `0xRRGGBB` color |
| `palreset()` | restore the default palette |

`color` is a palette index `0..15`. Buttons: `0` left, `1` right, `2` up,
`3` down, `4` A (`Z` key), `5` B (`X` key) — mapped to the arrow keys.
