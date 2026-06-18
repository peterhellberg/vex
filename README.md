# vex

A minimal WASM-based fantasy console in a single `main.c`.

The console is the **host**: it opens a [raylib](https://www.raylib.com/)
window, loads a `.wasm` *cart*, links a tiny drawing/input API the cart
imports, and calls the cart's exported `update()` once per frame (60 fps).
Carts draw
into a fixed **320×180** framebuffer with a **16-color** [SWEETIE-16](https://lospec.com/palette-list/sweetie-16)
palette (overridable at runtime), scaled up to the window with
nearest-neighbour filtering.

- **Runtime:** [wasm3](https://github.com/wasm3/wasm3) — the simplest
  embeddable WASM interpreter (pure C, MIT). Only its core files are compiled.
- **Build:** the Zig toolchain. [`build.zig`](build.zig) builds the host, the
  `vex-init` scaffolder, and both example carts.
- **Graphics/input:** [raylib](https://www.raylib.com/).

Both dependencies are pulled in via [`build.zig.zon`](build.zig.zon) and built
from source, so the only requirement is `zig` — no system packages. They are
lazy: only the host needs them, so a cart-only build fetches neither.

## Build & run

Requires only `zig` (0.17-dev). Dependencies are fetched on first build.

```sh
zig build               # build ./vex + ./vex-init + cart.wasm + zcart.wasm
zig build run           # build, then run the C example cart
zig build runz          # build, then run the Zig example cart
zig build run -- -s 5   # forward flags to vex (here: window scale 5)
zig build -Dhost=false  # build only the carts + SDK (skip the raylib host)
```

A `Makefile` wraps these as `make`, `make run`, `make runz`, and `make clean`.
`make install` copies the `vex` and `vex-init` binaries to `~/.local/bin`
(override with `make install PREFIX=/usr/local`).

`vex` is invoked as `vex [-s scale] <cart.wasm>`. The window is the 320×180
framebuffer times `scale` (default 3, i.e. 960×540); `-s`/`--scale` overrides it.

There are two example carts: [`cart/main.c`](cart/main.c) (C) and
[`zcart/main.zig`](zcart/main.zig) (Zig). Both compile to `wasm32` and use the
same console API.

## Controls

| Key | Action |
|-----|--------|
| `Super`+`Enter` | toggle fullscreen |
| `Super`+`I` | toggle integer scaling (crisp pixels vs. fill the screen; on by default in fullscreen) |
| `Super`+`R` | reload the cart from disk (hot reload) |
| `Esc` | quit |

`Super` is the Cmd key on macOS and the Super/Windows key on Linux. Arrow
keys, `Z`, and `X` are passed to the cart via `btn()`.

**Tip:** for a fast edit loop, leave `vex` running, rebuild the cart
(`zig build`), and press `Super`+`R` to reload it in place.

## Starting a new cart

`vex-init` scaffolds a standalone Zig cart project that depends on the `vex`
SDK published at <https://github.com/peterhellberg/vex>. With the binaries
installed (`make install`, so `vex` and `vex-init` are on your `PATH`):

```sh
vex-init mygame      # creates mygame/ (src/cart.zig, build.zig, build.zig.zon)
                     # and runs `zig fetch --save` to pin the vex dependency
cd mygame
zig build            # builds zig-out/bin/mygame.wasm
vex zig-out/bin/mygame.wasm
```

`vex-init` fetches the vex dependency for you (`zig fetch --save`); if that
step can't run it prints the command to finish manually. The generated
`build.zig` depends on `vex` with `.{ .host = false }`, so only the
[`vex.zig`](vex.zig) SDK module is pulled in — not the raylib/wasm3 host.

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

Or in Zig — import the [`vex.zig`](vex.zig) SDK and `export` the entry points:

```zig
const vex = @import("vex");

export fn update() void {
    vex.cls(1);                 // clear to dark blue
    vex.text("HELLO", 4, 4, 12); // white text
}
```

```sh
zig build-exe -target wasm32-freestanding -O ReleaseSmall -fno-entry -rdynamic \
    -femit-bin=mycart.wasm --dep vex -Mroot=mycart.zig -Mvex=vex.zig
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
| `title(s)` | set the window title |
| `btn(button) -> int` | `1` if a button is held, else `0` |
| `mx() -> int` / `my() -> int` | mouse position in framebuffer pixels (`0..319` / `0..179`) |
| `mbtn(button) -> int` | `1` if a mouse button is held (0 left, 1 right, 2 middle) |
| `pal(index, rgb)` | override palette entry `index` (0..15) with a packed `0xRRGGBB` color |
| `palreset()` | restore the default palette |

`color` is a palette index `0..15`. Buttons: `0` left, `1` right, `2` up,
`3` down, `4` A (`Z` key), `5` B (`X` key) — mapped to the arrow keys.
