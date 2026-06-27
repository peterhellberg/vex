# vex

A minimal WASM-based fantasy console.

A *cart* is any `wasm32` module that exports `update()` and imports a tiny
drawing/input API from the host. The host opens a window, runs the cart at
**60 fps**, and blits the fixed **320×180**, **16-color** framebuffer to the
screen.

There are three interchangeable hosts:

 - [`vex`](main.c) — the reference native host, in C _(raylib + wasm3)_
 - [`vex-run`](cmd/vex-run/main.go) — a native host in Go _(ebitengine + wazero)_
 - [`vex-web`](cmd/vex-web/main.go) — a browser host _(a `<canvas>` in a small Go server)_

For the specs and internals, see [How vex works](#how-vex-works).

## Build & run

Needs the pinned `zig` (`0.17.0-dev.305+bdfbf432d`, see [`build.zig.zon`](build.zig.zon)).

Dependencies are fetched on first build.

> [!Important]
> Use the pinned dev build, not `master`. `std.Build`'s API churns between
> 0.17 nightlies, and a newer/older `zig` will fail to compile this project's
> *and* raylib's `build.zig` (e.g. `no field named 'args' in struct 'Build'`).
> Download the exact build from a
> [community mirror](https://ziglang.org/download/community-mirrors.txt), e.g.
> `https://pkg.hexops.org/zig/zig-<arch>-<os>-0.17.0-dev.305+bdfbf432d.tar.xz`.

```sh
zig build --prefix .    # build vex + vex-init + cart.wasm + zcart.wasm into ./bin
zig build run           # build, then run the C example cart
zig build runz          # build, then run the Zig example cart
zig build run -- -s 5   # forward flags to vex (here: window scale 5)
zig build --prefix . -Dhost=false   # only the carts + SDK (skip the raylib host)
```

`--prefix .` installs into `./bin`; a plain `zig build` uses Zig's default
`zig-out/`. The `run`/`runz` steps execute straight from the build cache, so
they need no prefix.

A `Makefile` wraps these as `make`, `make run`, `make runz`, `make web`,
`make test-web`, and `make clean`, passing `--prefix .` for you so all
binaries — including the Go `vex-web` and `vex-run` — land in `./bin`.

`make install` copies `vex`, `vex-init`, `vex-web`, and `vex-run` from there to
`~/.local/bin` _(override with `make install PREFIX=/usr/local`)_.

`vex` is invoked as `vex [-s scale] [-w] <cart.wasm>`. For a dependency-free
alternative written in Go, see [Native Go version](#native-go-version). 

The window is the 320×180 framebuffer times `scale` 
_(default 3, i.e. 960×540)_; `-s`/`--scale` overrides it. 

With `-w`/`--watch`, vex polls the cart file and reloads it 
automatically whenever it changes — the native counterpart 
to vex-web's live-reload (see [Web version](#web-version)).

There are two example carts: 

- [`examples/cart/main.c`](examples/cart/main.c) (C)
- [`examples/zcart/main.zig`](examples/zcart/main.zig) (Zig)

Both compile to `wasm32` and use the same console API.

### Linux prerequisites

The host links raylib's default X11 backend, so 
the matching system libraries must be present. 

On Debian/Ubuntu _(22.04 and newer)_:

```sh
sudo apt install \
    libgl1-mesa-dev libx11-dev libxrandr-dev \
    libxinerama-dev libxi-dev libxcursor-dev pkg-config
```

Without them the build stops at `unable to find dynamic system library 'GL'`.

The resulting `vex` is statically linked against raylib and wasm3; only the
system X11/GL libraries (present on any desktop) are needed at runtime.

> [!Note]
> On Linux the link step prints `warning(link): unexpected LLD stderr` and a
> few `archive member '…/libGL.so' is neither ET_REL nor LLVM bitcode`
> warnings. These are harmless — raylib's static archive references the system
> `.so`s by path — and `zig build` still exits `0` with a working `vex` binary.

## Controls

| Key | Action |
|-----|--------|
| `Super`+`Enter` | toggle fullscreen |
| `Super`+`I` | toggle integer scaling _(crisp pixels vs. fill the screen; on by default in fullscreen)_ |
| `Super`+`R` | reload the cart from disk _(also automatic with `-w`/`--watch`)_ |
| `Esc` | quit |

`Super` is the Cmd key on macOS and the Super/Windows key on Linux. 

Arrow keys, `Z`, and `X` are passed to the cart via `btn()` _(held)_ and
`btnp()` _(just pressed this frame)_.

> [!Note]
> These shortcuts apply to the C host `vex`. The Go host `vex-run` only
> supports `Esc` (quit) and `Cmd`+`Enter` (fullscreen); the C host's
> `Super`+`I`/`Super`+`R` reload and integer-scale toggles are not
> implemented there. The web host has no global shortcuts — see the
> [Web version](#web-version) section.

## Native Go version

The same carts also run on a native host written entirely in Go —
[`vex-run`](cmd/vex-run/main.go) uses [wazero](https://wazero.io/) to run the
cart and [ebitengine](https://ebitengine.org/) to draw into a window, so it
ships as a single self-contained binary with **fewer system dependencies**
than the C host — no `libgl1-mesa-dev`, no Zig toolchain, and no `pkg-config`.

The CLI matches the C host:

```sh
# straight from GitHub, no checkout needed:
go run github.com/peterhellberg/vex/cmd/vex-run@latest mycart.wasm

# or from a checkout of this repo:
make                                            # builds bin/vex-run via `go build`
go run ./cmd/vex-run -s 5 mycart.wasm           # -s/--scale and -w/--watch work too
go run ./cmd/vex-run -w mycart.wasm             # auto-reload on cart changes
```

`-s`/`--scale` sets the window scale (default 3, i.e. **960×540**) and
`-w`/`--watch` polls the cart file and reloads it whenever it changes —
the same live-reload workflow as `vex` and `vex-web`.

> [!Tip]
> On Linux, `vex-run` still needs the X11 development headers at build time
> (ebitengine's vendored GLFW links against them), but neither `libgl1-mesa-dev`
> nor `pkg-config` are required. You also need **Go ≥ 1.25.0** — a tarball from
> [go.dev](https://go.dev/dl/) works on any release; check your
> distro's `golang-go` before relying on `apt install` alone.

The cart is loaded by `wazero`, and the same console API _(framebuffer,
SWEETIE-16 palette, drawing, input, **8×8 bitmap font**)_ is reimplemented in
Go and linked into the cart's `env` imports — so the cart source is
identical to what you'd write for `vex` or `vex-web`.

`make install` puts `vex-run` on your `PATH` alongside `vex`, `vex-init`,
and `vex-web`.

> [!Note]
> On Linux, ebitengine's X11 backend ([xgb](https://github.com/jezek/xgb))
> prints two lines on startup if it can't read a valid `~/.Xauthority`:
>
> ```
> XGB: conn.go:69: Could not get authority info: EOF
> XGB: conn.go:70: Trying connection without authority info...
> ```
>
> The second line means it falls back to an unauthenticated X11 connection,
> which works on any normal desktop session; the messages are harmless
> startup output and the run continues normally.

## Web version

The same carts run unchanged in the browser. 

`vex-web` _([`cmd/vex-web/main.go`](cmd/vex-web/main.go))_ is a small self-contained 
Go server that serves a `<canvas>`-based host — [`vex.js`](cmd/vex-web/assets/vex.js) 
reimplements the console API _(framebuffer, SWEETIE-16 palette, drawing, input,
and the shared **8×8 bitmap font**)_ in JavaScript and draws into the same fixed
**320×180** framebuffer, scaled up to fill the window while keeping the aspect
ratio. 

`index.html` and `vex.js` are embedded into the binary _(via
`//go:embed`)_, so `vex-web` needs nothing beside it but a vex cart.

```sh
# straight from GitHub, no checkout needed:
go run github.com/peterhellberg/vex/cmd/vex-web@latest mycart.wasm

# or from a checkout of this repo:
make web                          # build, then serve cart.wasm on :8383
make web CART=bin/zcart.wasm      # serve a different cart
go run ./cmd/vex-web mycart.wasm  # run the server directly
```

It serves the page on <http://localhost:8383/> and opens your browser there
_(`-no-open` skips that; `-addr host:port` changes the address)_. 

The cart is served on `/cart.wasm`, **read from disk on every request**, 
and the page watches it over Server-Sent Events _(`/reload`)_ — so rebuilding 
the cart **live-reloads** it in the browser, no refresh or restart needed.

> [!Tip]
> **Live-reload workflow:** run a watching build in one terminal and the server
> in another — every rebuild reloads the cart in the browser automatically.
>
> ```sh
> zig build --watch                 # terminal 1: rebuild carts on every change
> go run ./cmd/vex-web zig-out/bin/zcart.wasm  # terminal 2: serve + auto-reload
> ```

Arrow keys, `Z`, and `X` map to `btn()` and `btnp()`, and the mouse maps to
`mx()`/`my()`/`mbtn()`, just like the native host.

> [!Tip]
> **Drag and drop** any `.wasm` onto the page to load it in place of the
> default cart — handy for trying a build without restarting the server.

> [!Tip]
> **Virtual gamepad in portrait.** When the viewport is meaningfully
> taller than it is wide _(roughly 5:6 or narrower, e.g. a phone held
> upright)_, the page adds a touch-friendly gamepad below the canvas.
> All six buttons sit inside one square 3×3 grid: the d-pad arrows
> _(up/left/right/down)_ on the cross arms, `Z` in the centre, and `X`
> in the bottom-right corner. Every cell is the same size, so the six
> buttons are identical squares — comfortable to hit with a thumb.
> Multi-touch works (you can hold, say, left + up for a diagonal),
> the gamepad disables the browser's pinch-zoom gesture so two-finger
> input doesn't move the page, and keys pressed on a physical keyboard
> also highlight the matching on-screen button.

> [!Tip]
> **Browser tests:** `make test-web` runs the Playwright-driven test
> suite under [`cmd/vex-web/test/`](cmd/vex-web/test/) — it rebuilds the
> cart bundle, spins up a tiny static server, and drives the page at
> several viewport sizes (iPhone SE / 12 / Pro Max, plus near-square
> and landscape to verify the gamepad hides). On first run it downloads
> Chromium and the `playwright` npm package; subsequent runs reuse the
> install. Override the cart with `make test-web CART=path/to/cart.wasm`.

`make install` puts `vex-web` on your `PATH` alongside `vex` and `vex-init`.

## Creating a cart

Two ways to start: scaffold a Zig project with `vex-init`, or write a cart by
hand in C or Zig. Either way a cart is a `wasm32` module — see the [API](#api).

### Scaffolding a cart with vex-init

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

`vex-init` fetches the vex dependency for you (`zig fetch --save`); 
if that step can't run it prints the command to finish manually. 

The generated `build.zig` depends on `vex` with `.{ .host = false }`, 
so only the [`vex.zig`](vex.zig) SDK module is pulled in — not the raylib/wasm3 host.

### Writing a cart by hand

A cart is any `wasm32` module that exports `update()` 
_(and optionally `boot()`)_ and imports the API from `env`.

Both SDKs are a single file you drop next to your cart:

```sh
curl -LO https://raw.githubusercontent.com/peterhellberg/vex/main/vex.h    # C
curl -LO https://raw.githubusercontent.com/peterhellberg/vex/main/vex.zig  # Zig
```

In C, include [`vex.h`](vex.h):

```c
#include "vex.h"

VEX_EXPORT("update") void update(void) {
  cls(8);                  // clear to dark blue
  text("VEX C", 8, 8, 12); // white text
  rect(60, 60, 8, 8, 6);   // green square
}
```

Compile it to WASM:

```sh
zig cc --target=wasm32-freestanding \
  -nostdlib -Os -Wl,--no-entry -I. \
  -o mycart.wasm mycart.c
```

Or in Zig — import the [`vex.zig`](vex.zig) SDK and `export` the entry points:

```zig
const vex = @import("vex");

export fn update() void {
    // clear to dark blue
    vex.cls(8);

    // white text
    vex.text("VEX ZIG", 8, 8, 12);
}
```

```sh
zig build-exe -target wasm32-freestanding \
  -O ReleaseSmall -fno-entry -rdynamic \
  -femit-bin=mycart.wasm --dep vex \
  -Mroot=mycart.zig -Mvex=vex.zig
```

> [!Note]
>  
> I've prepared makefiles for [C](base/c/Makefile) and [Zig](base/zig/Makefile) 
> respectively, they are meant to make it easier to 
> build carts written by hand.

## API

| Function | Description |
|----------|-------------|
| `cls(color)` | clear the screen |
| `pset(x, y, color)` | set one pixel |
| `rect(x, y, w, h, color)` | filled rectangle |
| `rectb(x, y, w, h, color)` | rectangle outline |
| `circ(x, y, r, color)` | filled circle |
| `circb(x, y, r, color)` | circle outline |
| `line(x0, y0, x1, y1, color)` | line |
| `tri(x1, y1, x2, y2, x3, y3, color)` | filled triangle |
| `trib(x1, y1, x2, y2, x3, y3, color)` | triangle outline |
| `blit(data, x, y, w, h, key)` | draw a `w`×`h` bitmap of palette indices (one byte/pixel); pixels equal to `key` are skipped |
| `text(s, x, y, color)` | draw a string |
| `title(s)` | set the window title |
| `btn(button) -> int` | `1` if a button is held, else `0` |
| `btnp(button) -> int` | `1` if a button was just pressed this frame _(edge detection; same button numbers as `btn()`)_, else `0` |
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

## How vex works

The host — a single [`main.c`](main.c) — opens a [raylib](https://www.raylib.com/)
window and runs the cart on the [wasm3](https://github.com/wasm3/wasm3)
interpreter. It links a small `env` API into the cart's imports, calls the
cart's exported `boot()` once at start and `update()` once per frame, then blits
the 320×180 framebuffer to the window with nearest-neighbour scaling.

### Specs

- **Display** — 320×180 framebuffer, scaled to the window with nearest-neighbour filtering.
- **Palette** — 16 colors ([SWEETIE-16](https://lospec.com/palette-list/sweetie-16)), overridable at runtime via `pal()`.
- **Frame rate** — 60 fps; carts export `update()` (per frame) and optionally `boot()` (once at start).
- **Input** — 6 buttons (arrow keys + `Z`/`X`) and the mouse (position + 3 buttons).
- **Cart** — any `wasm32` module that exports `update()` and imports the API from `env`.

### Drawing & input

- **Lifecycle** — the host calls `boot()` once at start _(optional)_ and
  `update()` every frame at 60 fps; do your drawing from there.
- **Coordinates** — the framebuffer origin is the **top-left**; `x` runs
  `0..319` and `y` runs `0..179`.
- **The framebuffer persists between frames** — it is *not* cleared for you, so
  start `update()` with `cls(color)` (or redraw the whole screen) to avoid
  leftovers from the previous frame.
- **Color is a palette index** `0..15`, never a raw RGB value. Remap an entry at
  runtime with `pal(index, 0xRRGGBB)` and restore the defaults with `palreset()`.
- **Input** — read the buttons with `btn(n)` _(the d-pad and A/B, mapped to the
  arrow keys and `Z`/`X`)_ for held state and `btnp(n)` for a one-shot on the
  frame a button is first pressed. The mouse is read with `mx()`/`my()`/`mbtn(n)`.
  See the [API](#api) for the exact button numbers.

### Components

There are three interchangeable hosts — see the [Native Go
version](#native-go-version) and [Web version](#web-version) sections for the
Go and browser alternatives:

- **C host (`vex`)** — the reference implementation in [`main.c`](main.c).
  - **Runtime:** [wasm3](https://github.com/wasm3/wasm3) — the simplest
    embeddable WASM interpreter _(pure C, MIT)_; only its core files are
    compiled.
  - **Graphics/input:** [raylib](https://www.raylib.com/).
- **Go host (`vex-run`)** — [`cmd/vex-run/main.go`](cmd/vex-run/main.go).
  - **Runtime:** [wazero](https://wazero.io/) — a pure-Go WASM runtime
    _(no cgo)_.
  - **Graphics/input:** [ebitengine](https://ebitengine.org/) — a pure-Go
    2D game engine; brings its own OpenGL/DirectX/Metal backends, so no
    X11/GL system packages are needed at runtime.
- **Web host (`vex-web`)** — a `<canvas>` reimplemented in
  [`vex.js`](cmd/vex-web/assets/vex.js), served by a small Go HTTP server.
- **Build:** the Zig toolchain. [`build.zig`](build.zig) builds the C host,
  the `vex-init` scaffolder, and both example carts; `make` also builds
  `vex-run` and `vex-web` with `go build`.

The C host's dependencies are pulled in via [`build.zig.zon`](build.zig.zon)
and built from source. They're lazy: only the host needs them, so a
cart-only build (`-Dhost=false`) fetches neither and needs no system packages
on any platform. The Go hosts only need a recent Go toolchain.

On macOS and Windows the only requirement is `zig`; on Linux the C host also
links the system X11/OpenGL libraries _(see [Linux
prerequisites](#linux-prerequisites))_. The Go hosts need nothing beyond
Go itself on any platform.
