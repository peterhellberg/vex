# vex

A minimal WASM-based fantasy console in a single `main.c`.

The console is the **host**: it opens a [raylib](https://www.raylib.com/)
window, loads a `.wasm` *cart*, links a tiny drawing/input API the cart
imports, and calls the cart's exported `update()` once per frame. Carts draw
into a fixed **128×128** framebuffer with a fixed **16-color** (PICO-8)
palette, scaled up to the window with nearest-neighbour filtering.

- **Runtime:** [wasm3](https://github.com/wasm3/wasm3) — the simplest
  embeddable WASM interpreter (pure C, MIT). Fetched automatically on first
  build; only its core files are compiled.
- **Compiler:** `zig cc` (for both the host and the carts).
- **Graphics/input:** raylib.

## Build & run

Requires `zig`, `git`, and raylib (`brew install raylib zig` on macOS).

```sh
make run      # build everything, then run the C example cart (cart.wasm)
make runz     # build everything, then run the Zig example cart (zcart.wasm)
make          # just build (./vex + cart.wasm + zcart.wasm)
make clean    # remove build artifacts
```

There are two example carts: [`cart/main.c`](cart/main.c) (C) and
[`zcart/main.zig`](zcart/main.zig) (Zig). Both compile to `wasm32` and use the
same console API.

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
| `line(x0, y0, x1, y1, color)` | line |
| `text(s, x, y, color)` | draw a string |
| `btn(button) -> int` | `1` if a button is held, else `0` |

`color` is a palette index `0..15`. Buttons: `0` left, `1` right, `2` up,
`3` down, `4` A (`Z` key), `5` B (`X` key) — mapped to the arrow keys.
