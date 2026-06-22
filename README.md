# vex

A minimal WASM-based fantasy console in a single `main.c`

The console is the **host**: it opens a [raylib](https://www.raylib.com/)
window, loads a `.wasm` *cart*, links a tiny drawing/input API the cart
imports, and calls the cart's exported `update()` once per frame _(60 fps)_.

Carts draw into a fixed **320×180** framebuffer with a **16-color** 
[SWEETIE-16](https://lospec.com/palette-list/sweetie-16) palette 
_(overridable at runtime)_, scaled up to the window with nearest-neighbour filtering.

- **Runtime:** [wasm3](https://github.com/wasm3/wasm3) — the simplest
  embeddable WASM interpreter _(pure C, MIT)_. 
  Only its core files are compiled.
- **Build:** the Zig toolchain. [`build.zig`](build.zig) builds the host, the
  `vex-init` scaffolder, and both example carts.
- **Graphics/input:** [raylib](https://www.raylib.com/).

Both dependencies are pulled in via [`build.zig.zon`](build.zig.zon) and built
from source. On macOS and Windows the only requirement is `zig`. On Linux the
host links the system X11/OpenGL libraries, so a handful of `-dev` packages are
needed too _(see [Linux prerequisites](#linux-prerequisites))_. The deps are
lazy: only the host needs them, so a cart-only build (`-Dhost=false`) fetches
neither and needs no system packages on any platform.

## Build & run

Needs the pinned `zig` (`0.17.0-dev.305+bdfbf432d`, see
[`build.zig.zon`](build.zig.zon)). Dependencies are fetched on first build.

> [!Important]
> Use the pinned dev build, not `master`. `std.Build`'s API churns between
> 0.17 nightlies, and a newer/older `zig` will fail to compile this project's
> *and* raylib's `build.zig` (e.g. `no field named 'args' in struct 'Build'`).
> Download the exact build from a
> [community mirror](https://ziglang.org/download/community-mirrors.txt), e.g.
> `https://pkg.hexops.org/zig/zig-<arch>-linux-0.17.0-dev.305+bdfbf432d.tar.xz`.

```sh
zig build               # build ./vex + ./vex-init + cart.wasm + zcart.wasm
zig build run           # build, then run the C example cart
zig build runz          # build, then run the Zig example cart
zig build run -- -s 5   # forward flags to vex (here: window scale 5)
zig build -Dhost=false  # build only the carts + SDK (skip the raylib host)
```

A `Makefile` wraps these as `make`, `make run`, `make runz`, 
and `make clean`. `make install` copies the `vex` and `vex-init` 
binaries to `~/.local/bin`
_(override with `make install PREFIX=/usr/local`)_.

### Linux prerequisites

The host links raylib's default X11 backend, so the matching system libraries
must be present. On Debian/Ubuntu _(22.04 and newer)_:

```sh
sudo apt install \
    libgl1-mesa-dev libx11-dev libxrandr-dev \
    libxinerama-dev libxi-dev libxcursor-dev pkg-config
```

Equivalents: Fedora `mesa-libGL-devel libX11-devel libXrandr-devel
libXinerama-devel libXi-devel libXcursor-devel`; Arch `mesa libx11 libxrandr
libxinerama libxi libxcursor`. Without them the build stops at
`unable to find dynamic system library 'GL'`.

The resulting `vex` is statically linked against raylib and wasm3; only the
system X11/GL libraries (present on any desktop) are needed at runtime. To
target Wayland directly instead of X11, pass
`-Dlinux_display_backend=Wayland` _(also needs `libwayland-dev`,
`libxkbcommon-dev`, and `wayland-protocols`; these pull in `wayland-scanner`)_.

> [!Note]
> On Linux the link step prints `warning(link): unexpected LLD stderr` and a
> few `archive member '…/libGL.so' is neither ET_REL nor LLVM bitcode`
> warnings. These are harmless — raylib's static archive references the system
> `.so`s by path — and `zig build` still exits `0` with a working `vex` binary.

`vex` is invoked as `vex [-s scale] <cart.wasm>`. 

The window is the 320×180 framebuffer times `scale` 
_(default 3, i.e. 960×540)_; `-s`/`--scale` overrides it.

There are two example carts: 

- [`cart/main.c`](cart/main.c) (C)
- [`zcart/main.zig`](zcart/main.zig) (Zig)

Both compile to `wasm32` and use the same console API.

## Controls

| Key | Action |
|-----|--------|
| `Super`+`Enter` | toggle fullscreen |
| `Super`+`I` | toggle integer scaling _(crisp pixels vs. fill the screen; on by default in fullscreen)_ |
| `Super`+`R` | reload the cart from disk _(hot reload)_ |
| `Esc` | quit |

`Super` is the Cmd key on macOS and the Super/Windows key on Linux. 

Arrow keys, `Z`, and `X` are passed to the cart via `btn()`.

> [!Tip]
> For a fast edit loop, leave `vex` running, rebuild the cart
> (`zig build`), and press `Super`+`R` to reload it.

## Web version

The same carts run unchanged in the browser. `vex-web`
_([`tools/vex-web.go`](tools/vex-web.go))_ is a small self-contained Go server
that serves a `<canvas>`-based host — [`vex.js`](tools/assets/vex.js)
reimplements the console API _(framebuffer, SWEETIE-16 palette, drawing, input,
and the shared **8×8 bitmap font**)_ in JavaScript and draws into the same fixed
**320×180** framebuffer, scaled up to fill the window while keeping the aspect
ratio. `index.html` and `vex.js` are embedded into the binary _(via
`//go:embed`)_, so a built `vex-web` needs nothing beside it but a cart.

```sh
make web                              # build, then serve cart.wasm on :8383
make web CART=zig-out/bin/zcart.wasm  # serve a different cart
go run tools/vex-web.go mycart.wasm   # or run the server directly
```

It serves the page on <http://localhost:8383/> and opens your browser there
_(`--no-open` skips that; `-addr host:port` changes the address)_. The cart is
served on `/cart.wasm`, **read from disk on every request** — so rebuilding the
cart and refreshing the page loads the new bytes, no restart needed.

Arrow keys, `Z`, and `X` map to `btn()`, and the mouse maps to
`mx()`/`my()`/`mbtn()`, just like the native host.

> [!Tip]
> **Drag and drop** any `.wasm` onto the page to load it in place of the
> default cart — handy for trying a build without restarting the server.

`make install` puts `vex-web` on your `PATH` alongside `vex` and `vex-init`.

## Starting a new cart

`vex-init` scaffolds a standalone Zig cart project that depends on the `vex`
SDK which is published at <https://github.com/peterhellberg/vex>. 

With the binaries installed (`make install`, so `vex` and `vex-init` are on your `PATH`):

```sh
vex-init mygame      # creates mygame/ (src/cart.zig, build.zig, build.zig.zon)
                     # and runs `zig fetch --save` to pin the vex dependency
cd mygame
zig build            # builds zig-out/bin/mygame.wasm
vex zig-out/bin/mygame.wasm
```

`vex-init` fetches the vex dependency for you (`zig fetch --save`); if that
step can't run it prints the command to finish manually. 

The generated `build.zig` depends on `vex` with `.{ .host = false }`, 
so only the [`vex.zig`](vex.zig) SDK module is pulled in — not the raylib/wasm3 host.

## Writing a cart

A cart is any `wasm32` module that exports `update()` _(and optionally
`boot()`)_ and imports the API from `env`. 

In C, include [`vex.h`](vex.h):

```c
#include "vex.h"

VEX_EXPORT("update") void update(void) {
    cls(1);                 // clear to dark blue
    text("HELLO", 4, 4, 7); // white text
    rect(60, 60, 8, 8, 11); // green square
}
```

Compile it to WASM:

```sh
zig cc --target=wasm32-freestanding -nostdlib -O2 -Wl,--no-entry -I. \
       -o mycart.wasm mycart.c
vex mycart.wasm
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
vex mycart.wasm
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

`color` is a palette index `0..15`

Buttons: 
 - `0` left 
 - `1` right 
 - `2` up
 - `3` down 
 - `4` A (`Z` key)
 - `5` B (`X` key)
