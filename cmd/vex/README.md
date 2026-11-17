# vex (native host)

`cmd/vex` builds `vex`, the native C host for vex carts. It loads a
`wasm32` cart with [wasm3](https://github.com/wasm3/wasm3), supplies the
console API as `env` imports, and drives it at 60 ticks per second:
`boot()` runs once if exported, then `update()` runs once per tick. The
cart draws into a fixed 320x180 framebuffer that raylib scales into a
window with nearest-neighbour filtering.

This is a standalone Zig package so the cart SDK in the repository root
has no dependency on raylib or wasm3. Only this directory pulls them in.

## Build and install

From this directory:

```sh
zig build                 # -> zig-out/bin/vex
zig build --prefix .      # -> ./bin/vex
zig build -Doptimize=debug
```

From the repository root, `make` builds both the SDK and this host into
`./bin`, and `make install` copies `vex` to `~/.local/bin`. Builds are
release (`fast`) by default; pass `-Doptimize=debug` here for a debug
build. Linux needs the X11/GL development libraries listed in the root
README.

Two convenience steps run the example carts (built by the root `make`):

```sh
zig build run    # ../bin/cart.wasm
zig build runz   # ../bin/zcart.wasm
```

## Usage

```
vex [-s scale] [-w] [-n frames] [-t] [--dump file] [-i tape] <cart.wasm>
```

| Flag | Effect |
|------|--------|
| `-s`, `--scale <n>` | Window scale (default 3, so 960x540). Clamped to 1..20. |
| `-w`, `--watch` | Poll the cart file and reload it when it changes. `Super`+`R` reloads manually. |
| `-n`, `--frames <n>` | Run headlessly for `n` frames instead of opening a window. |
| `-t`, `--trace` | With `-n`, print one line per frame whose framebuffer changed, with its FNV-1a64 hash. |
| `--dump <file>` | With `-n`, write the final RGBA framebuffer (320x180x4 bytes). |
| `-i`, `--input <tape>` | Drive headless input from a tape file, or `-` for stdin. Requires `-n`. |

## Windowed mode

The cart's six `btn()` buttons come from the arrow keys and `Z`/`X`, a
gamepad, or a gamepad's left stick:

| Cart button | Keyboard | Gamepad |
|-------------|----------|---------|
| 0 `LEFT` | Left arrow | d-pad left / stick left |
| 1 `RIGHT` | Right arrow | d-pad right / stick right |
| 2 `UP` | Up arrow | d-pad up / stick up |
| 3 `DOWN` | Down arrow | d-pad down / stick down |
| 4 `Z` | `Z` | bottom face button (`A`/Cross) or `LB`/`L1` |
| 5 `X` | `X` | right face button (`B`/Circle) or `RB`/`R1` |

The stick snaps to a direction past 30% of travel so a resting stick does
not drift. Pads need no setup and can be connected or removed while the
host runs. `mx()`/`my()` report the pointer in cart coordinates, mapped
back through the letterboxed window; `mbtn()` reports mouse buttons
0 left, 1 right, 2 middle.

Host shortcuts: `Super`+`Enter` toggles fullscreen, `Super`+`I` toggles
integer scaling, `Super`+`R` reloads the cart, `Esc` quits. `Super` is
Cmd on macOS and the Super/Windows key on Linux.

Frame pacing uses absolute 60 Hz deadlines rather than a relative sleep,
so motion does not hitch on high-refresh displays. If the loop falls
more than four ticks behind (hidden window, debugger stop), it
resynchronizes instead of bursting through the backlog.

## Headless mode

`-n <frames>` runs without a window or audio. `title()` is ignored and
`tone()` is silent, so runs are deterministic. It prints the frame
count, total and per-frame time, the number of changed frames, the final
framebuffer's FNV-1a64 hash, memory use, and the final palette. Add
`-t` for the per-frame trace and `--dump` to save the last frame as raw
RGBA.

Without `-i`, all inputs read as released and the mouse sits at the
screen center. With `-i`, a tape supplies them instead.

### Input tapes

A tape is a sparse list of frame-indexed input states. Frames are
1-based: `@N` sets an absolute frame and `+N` advances N frames from the
previous event. The button field is a comma-separated list of held
buttons (`-` releases all). An optional trailing `X,Y` sets the mouse
position; omitted fields keep their previous value. Before the first
event, inputs are neutral, so an initial `@1` is not required — a
leading `+1` lands on frame 1 as well.

```
@1   Z                     # start
+1   -
+1   L                     # hold left
+20  -
+17  Z   160,90            # tap Z and move the pointer
+1   -
```

Button names are uppercase: `L`, `R`, `U`, `D`, `Z`, `X`, `LMB`,
`RMB`, `MMB`. Keyboard and mouse buttons share one list. Each event
describes the complete held state, so "hold" means a long gap before
the next event that omits that button. `btnp()` sees a press on the
frame a button first becomes held, using the same edge detection as
the windowed loop.

Use `-i -` to read the tape from stdin:

```sh
printf '@1 Z\n+1 -\n' | vex -n 100 -i - cart.wasm
```

Tape lines may have `#` comments. Malformed lines, out-of-order frames,
and more than 1024 events are reported with a `file:line` diagnostic
(`<stdin>` when piped) and exit non-zero.

## Console API

The host provides these imports in module `env`:

- Drawing: `cls`, `pset`, `rect`, `rectb`, `circ`, `circb`, `line`,
  `tri`, `trib`, `blit`, `blitm`, `text`, `title`
- Input: `btn`, `btnp`, `mx`, `my`, `mbtn`
- Palette: `pal`, `palreset` (SWEETIE-16, 16 entries)
- Audio: `tone` (four voices, pulse/noise/triangle, ADSR, slides and panning)

A cart must export `update()`; `boot()` is optional. A missing import is
reported as such, rather than as a broken cart.

Tests live in [`test/`](test/) and are run from the repository root with
`make test-hosts`. The tone and graphics suites compile the real host
sections verbatim against stubs, so the C host stays in sync with the
other hosts.
