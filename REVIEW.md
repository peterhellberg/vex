# Code Review — vex

Review of the three-host WASM fantasy console: the C host (`cmd/vex`),
Go native host (`cmd/vex-run`), web host (`cmd/vex-web`), SDKs (`vex.h`,
`vex.zig`, `spr.zig`), scaffolder (`cmd/vex-init`), and build/release tooling.

## Overall impression

This is a tidy, thoughtfully engineered codebase. Highlights:

- The hosts agree on a deliberately shared pixel model (midpoint circles over
  `hline`, run-batched `blit`, identical 8×8 font), and the comments explain
  *why* (e.g. `main.c:268-280`, the wasm3 slot-alignment fix in
  `cmd/vex/build.zig:77-86`).
- Hostile-cart hardening is taken seriously: coordinate bounds
  (`VEX_COORD_MAX`), w/h clamps, memory-bounded string copies, monophonic
  beep semantics matched across all three hosts.
- The Makefile release pipeline is unusually well documented, and the
  Playwright gamepad tests are a nice touch.

The suggestions below are ordered roughly by priority within each section.

---

## Resolution status

All findings have been addressed except where noted:

- **§1 Bugs** — all seven fixed (mbtn OOB, JS blit view refresh, CSS
  fallback order, Makefile uninstall, vex-run CLI exit codes, README links,
  typo).
- **§2 Cross-host consistency** — `blit` key now raw-compares in all hosts;
  `mx()/my()` clamp everywhere (matching the documented 0..319/0..179);
  `line`/`tri`/`trib` use the same Bresenham + f64 integer-scanline
  algorithms in all three hosts; JS `circ`/`circb` gained the same radius
  clamp as C/Go.
- **§3 Robustness** — all fixed (link errors reported, headless audio stall
  removed, x/sys/unix portability, strict scale parsing, unknown-flag
  diagnostics, render-texture checks, ServeContent-based cart serving, SSE
  keepalive, spr.zig hardening). The original `:8383` (all-interfaces)
  default bind was deliberately kept so carts are easy to reach from other
  devices; `-addr localhost:8383` restricts it.
- **§4 raylib build** — `cmd/vex/build.zig` now compiles raylib sources
  directly and links platform libs itself (4b approach); the link is quiet
  and no longer depends on raylib's build.zig API.
- **§5 Duplication** — FONT8/palette drift is now caught by root
  `conformance_test.go`; stale root zig-pkg deleted; compile_flags.txt
  regenerated at the repo root with working dep paths.
- **§6 Testing** — CI workflow added (`.github/workflows/ci.yml`) incl. the
  new `zig build test` step; Go unit tests for parse/raster/beep engine;
  golden framebuffer-hash tests for every built cart; hostile-input test
  cart (`test_hostile`). The Playwright suite runs as a separate manually
  triggered workflow (`.github/workflows/test-web.yml`).
- **§7/§8** — LICENSE (MIT) added earlier; docs-target regeneration note and
  `scripts/install-zig.sh` done.

Deferred on purpose: trimming raylib's rmodels/rtext modules (an experiment
for after the direct-source build settles), and full *cross-host* pixel
comparison of rendered carts (goldens currently pin each host's output
independently; comparing C vs Go vs JS framebuffers needs a headless C/JS
render harness).

---

## Second review pass (post-fix audit)

A fresh pass over the tree once the fixes landed surfaced six more items,
all resolved:

1. **JS host: per-call view allocation.** `updateMemoryViews()` rebuilt a
   `Uint8Array` on every `text()`/`blit()`/`title()` call. It now caches the
   view and refreshes only when the underlying buffer was replaced — which
   happens exactly when the cart grows its linear memory (the old buffer is
   detached) or a new cart is instantiated. Same correctness as the §1.2 fix,
   without the GC churn.
2. **vex-web: directory diagnostics.** Pointing the server at a directory
   logged `read <path>: <nil>`; it now prints "is a directory" (still 404).
3. **CI gap: `cmd/vex-run` was never tested.** It's a separate Go module, so
   the root `go vet ./... && go test ./...` silently skipped its unit and
   golden tests. The CI job now vets/builds/tests both modules.
4. **Web-tests workflow**: added `npx playwright install-deps chromium` so
   the manually triggered job has Chromium's system libraries on the runner.
5. **go.mod tidiness**: `golang.org/x/sys` moved from the `// indirect`
   block to a direct require, since `filterStderr` imports it directly.
6. **conformance_test.go**: dropped a hand-rolled substring search in favor
   of `strings.Index`.

Also re-verified during this pass: root `build.zig` carries no leftovers
from the briefly attempted build-delegation experiment; and a cold
`make distclean && make all` rebuilds everything — including the raylib 6.0
tag fetch — with zero warnings and no `libraylib.a` anywhere in project or
global caches.

---

## 1. Bugs

### 1.1 C host: `mbtn()` accepts out-of-range buttons → OOB read

`cmd/vex/main.c:497`:

```c
int held = (button >= 0 && button <= 6) ? IsMouseButtonDown(button) : 0;
```

raylib's mouse-button enum only defines values 0–2; `IsMouseButtonDown`
indexes an internal `bool[3]` array, so a cart calling `mbtn(3..6)` makes the
host read past it (UB). Every other input function validates correctly
(`btn` uses `< 6`). This should be `button >= 0 && button < 3`. The Go host
does this right (`cmd/vex-run/main.go:735`).

### 1.2 Web host: `blit()` bounds-checks a stale memory view

`cmd/vex-web/assets/vex.js:763-776` checks `ptr + w * h > mem8.length`
*before* calling `updateMemoryViews()`. If the cart grows its linear memory
(e.g. via allocator), `mem8` refers to a detached buffer whose `length` is 0,
so every blit silently no-ops until some other call refreshes the view.
Move `updateMemoryViews()` to the top of `blit()` (and ideally cache the view,
refreshing only when `memory.buffer` changes).

### 1.3 index.html: CSS fallback declarations are inverted

`cmd/vex-web/assets/index.html:15-16`:

```css
height: 100dvh;
height: 100vh;
```

In CSS the last valid declaration wins everywhere, so `100dvh` is dead code
and iOS Safari still gets the URL-bar overflow the comment warns about. Same
pattern at `.dpad` `width:` (`index.html:117-118`). The fallback order must be
reversed:

```css
height: 100vh;
height: 100dvh;
```

### 1.4 Makefile: `uninstall` misses `vex-run`

`Makefile`: `install` copies four binaries but `uninstall` only removes
`vex`, `vex-init`, and `vex-web`. Add `$(BINDIR)/vex-run`.

### 1.5 vex-run: bad CLI invocations exit 0

`cmd/vex-run/main.go:140` ignores the error from `fs.Parse(args)` (unknown
flags are silently swallowed), and the missing-cart path prints usage and
returns `nil` (`main.go:163-167`), so `vex-run --bogus` exits with status 0.
Propagate parse errors and return a non-nil error (or `os.Exit(2)`) for usage
failures, matching `vex-web`'s handling.

### 1.6 README: two broken self-links

`README.md:363` and `README.md:400` link `[main.c](main.c)` — relative to the
repo root that file doesn't exist; both should point at `cmd/vex/main.c`.

### 1.7 Typo

`cmd/vex-run/main.go:184`: `"build env moduleule"` → `"build env module"`.

---

## 2. Cross-host consistency

The project's core promise is that carts behave identically on all three
hosts. A few places where that currently leaks:

| Area | C | Go | JS |
|------|---|----|----|
| `blit` key compare | raw `int` compare (`main.c:432`) — keys > 255 never match | truncated to `byte(key)` (`main.go:603`) — key 256 behaves as key 0 | raw compare (`vex.js:786`) |
| `mx()/my()` outside window | mapped, may be negative / ≥ W (`main.c:485`) | clamped to `[0, W-1]` (`main.go:724-732`) | unmapped, may exceed range (`vex.js:125-136`) |
| `tri` fill | raylib GPU triangle + winding normalize (`main.c:355`) | scanline, floor-adjusted edges (`main.go:493`) | scanline, `Math.round` of row extents (`vex.js:795`) |

None of these break anything today, but each is a silent divergence a cart
author will eventually hit. Suggestions:

- Pick one semantic for `blit`'s key (raw compare is the documented one) and
  one for `mx()/my()` clamping, then make all three hosts match and document
  the choice in the API table.
- Consider implementing the same integer scanline triangle in the C host
  (like you already do for `circ`/`circb`) so triangles are also
  pixel-identical instead of driver-dependent.

## 3. Robustness

### 3.1 C host: linking swallows signature errors

`link_host` (`cmd/vex/main.c:628-653`) discards *all* `m3_LinkRawFunction`
results. Lookup failure is genuinely harmless, but other failures (e.g. a
cart importing `env.cls` with a mismatched signature) are masked too, surfacing
later as a confusing trap. Treat `m3Err_functionLookupFailed` as OK and report
anything else.

### 3.2 vex-run: 2-second startup stall on machines without audio

`ensureAudio` marks `audioOn = true` even when player creation fails
(`main.go:879-892`), so `audioFlowStarted()` returns false forever and every
`Update()` early-outs until the `audioReadyTimeout` (2 s) expires
(`main.go:950-956`). On headless/CI machines every launch wastes 2 seconds.
Short-circuit: if `g.audioPl == nil` after `ensureAudio`, consider audio
"ready" immediately.

### 3.3 vex-run: `syscall.Dup2` blocks Apple Silicon builds

`filterStderr` (`main.go:1119-1148`) uses `syscall.Dup2`, which doesn't exist
for `darwin/arm64` — the package won't compile there. Switch to
`golang.org/x/sys/unix` (`unix.Dup2`/`Dup3`), which is already in the module
graph transitively.

### 3.4 C host: minor unchecked things

- `LoadRenderTexture` results aren't checked, especially the re-create after a
  fullscreen toggle (`main.c:902-913`); a failed recreate would draw into a
  dead texture for the rest of the session.
- Scale parsing via `atoi` (`main.c:814`) treats `-s abc` as `-s 0`; `strtol`
  with validation would give a clear error.
- Unknown flags fall through to "cart path", so `vex --scalle 5 x.wasm`
  reports `cannot read --scalle`. A quick "unknown option" diagnostic would
  help.
- `host_text`/`host_title` silently truncate at 127 bytes (`buf[128]`);
  fine, but worth a line in the API docs since `title("…")` truncation is
  user-visible.

### 3.5 vex-web server

- Default bind address is `:8383` (all interfaces). For a dev tool whose page
  can load arbitrary local files, defaulting to `localhost:8383` (with an
  explicit `-addr 0.0.0.0:8383` escape hatch) is safer.
- `serveCart` reads the whole file per request (`main.go:282-295`);
  `http.ServeContent` would add Range/If-Modified-Since support for free.
- The SSE endpoint sends nothing between reloads; a periodic `: ping`
  comment line keeps intermediaries from timing out long-lived connections.

### 3.6 spr.zig

- `inflate()` returns a slice of a function-local `[262144]u8`
  (`spr.zig:140-145, 389`). This is safe because `fromPNG` forces comptime
  evaluation, but it's fragile: any future runtime caller gets a dangling
  slice, and >256 KiB inflates fail opaquely. Worth a doc comment warning (or
  passing the output buffer in).
- `found_plte` is set by *any* chunk starting with `'P'` (`spr.zig:70-71`);
  comparing all four bytes is just as cheap and more precise.

---

## 4. Build: compile raylib sources directly instead of linking its archive

**Problem.** `cmd/vex/build.zig:49-56` + `:88` consume raylib as a prebuilt
artifact — `b.lazyDependency("raylib", …)` → `raylib_dep.artifact("raylib")`
→ `linkLibrary(raylib)`. Raylib's own `build.zig` produces a static archive
that *embeds resolved system libraries* (e.g. `libGL.so`) as archive members,
which is why every vex link prints the `archive member '…/libGL.so' is neither
ET_REL nor LLVM bitcode` / `unexpected LLD stderr` noise documented at
`README.md:91-96`. It also couples vex to raylib's `build.zig` API surface
(the `.linux_display_backend` option field) — exactly the kind of churn that
forces the pinned-Zig workflow in the first place.

**Fix (taken from the 4b project, `../4b/build.zig:197-269`).** 4b avoids the
problem entirely by compiling raylib's sources straight into the consuming
executable and doing the platform linkage itself:

- Include paths: `src/`, `src/platforms/`, `src/external/glfw/include`.
- Macros: `PLATFORM_DESKTOP_GLFW`, `GRAPHICS_API_OPENGL_33`,
  `SUPPORT_MODULE_R{SHAPES,TEXTURES,TEXT,MODELS,AUDIO}=1`, `_GNU_SOURCE`,
  `GL_SILENCE_DEPRECATION`.
- Per-OS system linkage chosen by `target.os.tag`: Linux defines `_GLFW_X11`
  and links `GL X11 Xrandr Xinerama Xi Xcursor`; Windows links
  `opengl32 winmm gdi32`; macOS links six frameworks.
- `rglfw.c` compiled as its own translation unit (`-ObjC` on macOS).

Ported to vex, the three artifact-linking lines become a small helper called
from `pub fn build`:

```zig
fn addRaylib(mod: *std.Build.Module, raylib: *std.Build.Dependency) void {
    const target = mod.resolved_target.?.result;
    const rl = raylib.path("src");

    mod.addIncludePath(rl);
    mod.addIncludePath(raylib.path("src/platforms"));
    mod.addIncludePath(raylib.path("src/external/glfw/include"));

    mod.addCMacro("_GNU_SOURCE", "");
    mod.addCMacro("PLATFORM_DESKTOP_GLFW", "");
    mod.addCMacro("GRAPHICS_API_OPENGL_33", "");
    mod.addCMacro("GL_SILENCE_DEPRECATION", "199309L");
    inline for (.{ "RSHAPES", "RTEXTURES", "RTEXT", "RMODELS", "RAUDIO" }) |m|
        mod.addCMacro("SUPPORT_MODULE_" ++ m, "1");

    switch (target.os.tag) {
        .linux => {
            // Map the existing -Dlinux_display_backend option here:
            // X11 -> _GLFW_X11 (default), Wayland -> _GLFW_WAYLAND plus
            // wayland-client/wayland-cursor/xkbcommon, Both -> both macros.
            mod.addCMacro("_GLFW_X11", "");
            inline for (.{ "GL", "X11", "Xrandr", "Xinerama", "Xi", "Xcursor" }) |l|
                mod.linkSystemLibrary(l, .{});
        },
        .windows => inline for (.{ "opengl32", "winmm", "gdi32" }) |l|
            mod.linkSystemLibrary(l, .{}),
        .macos => inline for (
            .{ "Foundation", "CoreServices", "CoreGraphics", "AppKit", "IOKit", "QuartzCore" }
        ) |fw| mod.linkFramework(fw, .{}),
        else => @panic("vex: unsupported target for raylib"),
    }

    mod.addCSourceFiles(.{
        .root = rl,
        .files = &.{ "rcore.c", "rshapes.c", "rtextures.c", "rtext.c", "rmodels.c", "raudio.c" },
        .flags = &.{"-std=c99"},
    });

    // rglfw.c pulls in Objective-C on macOS, so it compiles separately.
    const glfw_flags: []const []const u8 =
        if (target.os.tag == .macos) &.{ "-std=c99", "-ObjC" } else &.{"-std=c99"};
    mod.addCSourceFile(.{ .file = raylib.path("src/rglfw.c"), .flags = glfw_flags });
}
```

with the call site reduced to:

```zig
const raylib = b.lazyDependency("raylib", .{ .target = target, .optimize = optimize }) orelse return;
addRaylib(exe.root_module, raylib);
```

Notes for vex's specific setup:

- **Display backend option:** keep `-Dlinux_display_backend` working by
  translating it to `_GLFW_X11` / `_GLFW_WAYLAND` macros in the switch above
  instead of forwarding an option struct into raylib's build (Wayland also
  needs `wayland-client`, `wayland-cursor`, `xkbcommon` linked).
- **macOS cross-compiles:** `linkFramework()` flags alone don't satisfy a
  Mach-O link from a Linux host, so keep the existing `xcode_frameworks`
  include/lib plumbing (`cmd/vex/build.zig:44-47,58-60,89-92`) alongside the
  framework list.
- **Release matrix:** the Windows (`x86_64-windows-gnu`) and macOS targets in
  the Makefile work unchanged — the per-OS switch covers them, and they inherit
  the same quiet-linking benefit (Zig supplies the MinGW import libs exactly as
  it did through raylib's archive).
- **Bonus trim:** vex uses only rcore (window/input/audio device), rtextures
  (render texture, font atlas), rshapes (pixels/rects/lines/triangles) and
  raudio. Once the direct build is proven, try dropping `rmodels.c` +
  `SUPPORT_MODULE_RMODELS=0` (clearly unused) — and experimentally
  `rtext.c` + `SUPPORT_MODULE_RTEXT=0`, though `rtextures.c` has some guarded
  `ImageDrawText*` paths that need verifying — to cut compile time and shrink
  the ~5 MB binary the release pipeline strips.

**Payoff.** The "these warnings are harmless" note (`README.md:91-96`) can be
deleted, vex stops depending on raylib's `build.zig` internals entirely
(easing future Zig bumps beyond just raylib's), and release builds go quiet.

## 5. Duplication worth automating away

The 96-glyph `FONT8` table and the 16-color palette are hand-copied into four
places: `cmd/vex/main.c:44`, `cmd/vex-run/main.go:73`, `cmd/vex-web/assets/vex.js:855`
(and again as BigInt rows), plus palette in all three. Comments assert they're
"byte-for-byte" identical, but nothing enforces it — the next glyph addition
will drift.

Suggestion: make the tables generated. Even a small script checked into the
repo (`tools/gen_tables.zig|.go`) emitting the three source snippets, wired
into `zig build`/`go generate`, turns drift into a build error. Alternatively,
a conformance test (§7) catches drift automatically.

Also: the root `zig-pkg/` directory holds stale packages (raylib, zemscripten)
that the root `build.zig.zon` (`.dependencies = .{}`) no longer references —
leftovers from before the host moved to `cmd/vex/`. `make clean` removes
`cmd/vex/zig-pkg` but not the root one; and the committed `compile_flags.txt`
points into `/zig-pkg/…` paths that don't exist on a fresh clone. Consider
deleting the stale dir and generating `compile_flags.txt` (or pointing clangd
at the fetched dep paths).

---

## 6. Smaller polish

- **Go host**: `readCString` (`main.go:1102`) does one `ReadByte` per char;
  a single `Read(ptr, remaining)` like `text()` uses is simpler and faster.
- **JS host**: `readCString` builds the string with `+=` (O(n²)); fine for
  titles, but `String.fromCharCode.apply` over a bounded chunk is cheap to do.
- **JS host**: during a hidden-tab catch-up burst, `tick()` runs multiple
  times per frame and re-presents each time; presenting once after the while
  loop is free perf. Also, `btnp()` edge state is captured per tick, so a
  burst can deliver the same "just pressed" several times — probably
  acceptable, but worth knowing.
- **C host**: `die()` doesn't free the runtime/environment/wasm on fatal
  paths; fine for exit(1), noting only for completeness.
- **vex-png-to-spr** is an orphan: not mentioned in the README or Makefile,
  and the `.spr` format it emits isn't consumed by anything in the repo
  (`spr.zig` decodes PNG directly). Either document it (and the format) or
  drop it.

---

## 7. Testing

Current state: Playwright UI tests for the web gamepad, two comptime tests in
`spr.zig`, zero Go tests, no CI.

Concrete suggestions, in order of value:

1. **CI (GitHub Actions).** A single workflow covering:
   - `zig build --prefix .` + `cd cmd/vex && zig build` (Linux; X11 deps
     available on `ubuntu-latest`)
   - `go vet ./... && go build ./...` for both modules
   - `zig build test` — note the root `build.zig` has **no test step**, so
     the `spr.zig` tests currently never run anywhere. Add:
     ```zig
     const tests = b.addTest(.{ .root_module = /* sdk module */ });
     b.step("test", "Run SDK tests").dependOn(&b.addRunArtifact(tests).step);
     ```
   - `make test-web` (Playwright works headless; cache the Chromium download).

2. **Cross-host golden-image conformance.** You already ship eight `test_*`
   carts (`examples/test-carts/`) that exercise exactly the risky surface
   (coords, blit, arith, palette, font). Render each cart for N frames on each
   host headlessly and compare framebuffer hashes:
   - C: easy offscreen mode or just screenshot via raylib;
   - Go: render into an `ebiten.Image` in a test binary;
   - JS: `getImageData` in the existing Playwright harness.
   This converts the many "keep in sync with…" comments into enforced
   invariants and would have caught §1.2/§2-class bugs.

3. **Go unit tests** for the pure logic: palette packing, `hline`/`tri`
   rasterization against a tiny golden fixture, `beepEngine.Read` phase/end
   behavior (it's fully deterministic behind its mutex), and `parse()`.

4. **A hostile-cart fuzz-ish test**: a cart that calls every import with
   extreme arguments (`mbtn(6)`, `pal(-1, …)`, giant `blit` sizes, unterminated
   strings) run for a few hundred frames on each host — guards the hardening
   you've already built against regressions.

---

## 8. Repo hygiene

- **No CI badge/workflow** — see §7.1.
- **docs/** contains committed build artifacts (`main.wasm`, `sources.tar`).
  Fine if intentional for GitHub Pages, but worth a `docs/README` or a line in
  the Makefile `docs` target noting they're regenerated, so nobody hand-edits
  them.
- **Version pinning UX**: the pinned Zig dev build is well documented; a
  `scripts/install-zig.sh` referenced from the README would remove the last
  manual step.

---

## Summary

| Priority | Item |
|----------|------|
| High | §1.1 `mbtn` OOB, §1.2 JS stale mem view, §1.3 CSS order |
| Medium | §1.4–1.7 small bugs, §3.1–3.3 robustness, §4 raylib direct compile, §7.1 CI + zig test step |
| Low | §2 semantics alignment, §5 table generation, §6 polish |

The codebase is in very good shape for its size and ambition; most findings
are about making the already-good invariants *enforced* rather than merely
documented.
