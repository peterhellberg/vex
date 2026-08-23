# TODO — fixing all REVIEW.md findings

STATUS: COMPLETE. All findings fixed or explicitly deferred; everything
verified. Deferred items are listed in REVIEW.md "Resolution status".

Working notes kept below so any interrupted change can be resumed.

## Legend

- [x] done + verified
- [ ] pending / in progress

---

## 1. C host `cmd/vex/main.c` — DONE, verified (`zig cc -c` OK)

- [x] §1.1 `host_mbtn`: `button <= 6` → `button < 3` (raylib bool[3] OOB).
- [x] §2 `mx()/my()` clamp to `[0, W-1]/[0, H-1]` via new `clampi32` helper
      (matches Go host + documented API range).
- [x] §2 `line/tri/trib` unified with Go/JS hosts:
      - new `host_pixel`, `host_bresenham` (Bresenham = Go's line());
      - `host_tri` rewritten as integer scanline fill w/ floor'd edge
        crossings (`tri_add_edge`, static `g_tri_l/g_tri_r[TRI_MAX_ROWS]`);
      - `host_trib` = three Bresenham edges; removed DrawLine/
        DrawTriangle/DrawTriangleLines + winding normalization.
- [x] §3.1 `link_host`: collects first non-lookupFailed error via `LINK`
      macro; `load_cart` reports it ("vex: link: ...") and frees runtime.
- [x] §3.4 strict `-s/--scale` parsing via `parse_long` (strtol, rejects
      garbage); missing value → clear error exit 1.
- [x] §3.4 unknown `-option` → "vex: unknown option" + usage, exit 1
      (previously treated as cart path).
- [x] §3.4 `LoadRenderTexture` id==0 checked at startup and after fullscreen
      toggle → `die(cart.rt, msg, NULL)`; `die()` now tolerates NULL err
      (no printf-of-NULL UB).
- [x] Added `#include <errno.h>`.

## 2. Go host `cmd/vex-run/main.go` — DONE, verified (go build + go vet OK)

- [x] §1.5 `parse()`: propagates `fs.Parse(args)` error.
- [x] §1.5 `run()`: parse error prints usage line and RETURNS the error
      (exit 1); `main()` ignores `flag.ErrHelp` for exit-code purposes.
- [x] §1.7 typo fixed ("build env module").
- [x] §2 `blit`: raw key compare via `uint32(src[col]) == key` in both loops.
- [x] §3.2 `ensureAudio`: sets `g.audioReady = true` when player creation
      fails (no 2 s headless stall).
- [x] §3.3 `filterStderr`: all syscall calls → x/sys/unix (darwin/arm64 OK);
      import swapped syscall→unix, added errors.
- [x] §6 polish: readCString single mem.Read instead of per-byte ReadByte.

## 3. Web host JS `cmd/vex-web/assets/vex.js` — DONE

- [x] §1.2 `blit()`: `updateMemoryViews()` now runs BEFORE the bounds check.
- [x] §2 `setMouse()`: mouseX/mouseY clamped to `[0, VEX_W-1]/[0, VEX_H-1]`.
- [x] §2 `tri()`: rewritten as integer scanline w/ reusable Int32Array
      buffers, Math.floor crossings, INF=1<<30 — matches Go exactly.
      NOTE: C's tri_add_edge also switched float→double so all three hosts
      compute edge crossings in f64 (pixel-identical at half boundaries).
- [x] §5 polish `readCString`: NUL scan + chunked String.fromCharCode (4096).
- [x] §5 polish `frame()`: present() once after the catch-up loop (`ran`
      counter); tick() = update + prevButtons only.

## 4. `cmd/vex-web/assets/index.html` — DONE

- [x] §1.3 body height: vh then dvh (comment updated).
- [x] §1.3 .dpad width: vh min() then dvh min() (comment updated).

## 5. Web server `cmd/vex-web/main.go` — DONE

- [x] §3.5 default `-addr` now "localhost:8383" (loopback; comment explains).
- [x] §3.5 `serveCart`: os.Open + Stat + http.ServeContent (Range/IMS),
      per-request open keeps live-reload semantics.
- [x] §3.5 SSE keepalive: ": ping" every 30th poll tick (~15s default).
- Verify with `go build ./...` in verification phase.

## 6. `spr.zig` — DONE

- [x] §3.6 PLTE detect: all four bytes checked ('P','L','T','E').
- [x] §3.6 doc comment on `inflate`: comptime-only, internal buffer,
      256 KiB cap.

## 7. `cmd/vex/build.zig` — raylib direct compile (§4) — PENDING

Plan (mirror ../4b/build.zig addRaylib):
- Drop `.linux_display_backend` from raylib lazyDependency options; dep used
  for paths only. Remove `artifact("raylib")` + `linkLibrary`.
- New `addRaylib(mod, raylib_dep, linux_display_backend)`:
  - includes: src/, src/platforms/, src/external/glfw/include
  - macros: _GNU_SOURCE, PLATFORM_DESKTOP_GLFW, GRAPHICS_API_OPENGL_33,
    GL_SILENCE_DEPRECATION=199309L, SUPPORT_MODULE_R{SHAPES,TEXTURES,TEXT,
    MODELS,AUDIO}=1
  - linux: X11→_GLFW_X11 (+GL,X11,Xrandr,Xinerama,Xi,Xcursor); Wayland→
    _GLFW_WAYLAND (+wayland-client,cursor,egl,xkbcommon); Both→both;
    None→only GL; always link GL.
  - windows: opengl32,winmm,gdi32; macos: frameworks Foundation,CoreServices,
    CoreGraphics,AppKit,IOKit,QuartzCore.
  - rcore/rshapes/rtextures/rtext/rmodels/raudio.c (-std=c99); rglfw.c
    separate, -ObjC on macOS.
- Keep xcode_frameworks plumbing for macOS cross-compiles.
- VERIFY: cd cmd/vex && zig build --prefix ../.. (uses cached deps).

## 8. Root `build.zig` test step (§6.1) — PENDING

- Add `zig build test` running spr.zig tests via addTest(.root_module=spr_mod).

## 9. Makefile — DONE

- [x] §1.4 uninstall += $(BINDIR)/vex-run.
- [x] §5 clean += root zig-pkg.
- [x] §8 docs target comment: docs/ is regenerated, don't hand-edit.

## 10. README.md — DONE

- [x] §1.6 fixed both `[main.c](main.c)` links → cmd/vex/main.c.
- [x] §4 deleted "harmless LLD warnings" blockquote (build is quiet now).
- [x] §3.4 API section: text()/title() truncate strings at 127 chars.
- [x] Build & run note references scripts/install-zig.sh.
- NOTE: mx/my clamping now matches README's documented 0..319/0..179 (no doc change needed).

## 11. compile_flags.txt + stale zig-pkg (§5) — DONE

- [x] ROOT compile_flags.txt carries everything (neovim/clangd walks UP the
      tree, so one root file serves both root C files and cmd/vex/main.c):
      `-I.` plus `-Icmd/vex/zig-pkg/raylib-…/src` and
      `-Icmd/vex/zig-pkg/<wasm3-hash>/source` for raylib/wasm3 go-to-def.
      Paths relative to repo root; exist after first build fetches deps.
- [x] Removed nested cmd/vex/compile_flags.txt (would shadow root file).
- [x] rm -rf ./zig-pkg (stale, gitignored, unused by root package).

## 12. scripts/install-zig.sh (§8) — DONE

- [x] Parses VERSION from build.zig.zon minimum_zig_version; downloads
      tarball from pkg.hexops.org for linux/macos x86_64+arm64 into
      ~/.local/zig-$VERSION (override ZIG_DIR), symlinks ~/.local/bin/zig.
      chmod +x applied, sh -n syntax-checked.

## 13. CI workflows (§6.1) — DONE

- [x] .github/workflows/ci.yml: checkout; setup-go (go.mod version);
      scripts/install-zig.sh (pinned); apt X11 dev libs; zig build SDK +
      cmd/vex host; zig build test; go vet/build/test both modules;
      build bin/vex-web + bin/vex-run.
- [x] Playwright web tests moved OUT of CI into
      .github/workflows/test-web.yml with `workflow_dispatch:` only
      (manual trigger from Actions tab, per user request). Chromium cached.

## 14. Tests (§6) — DONE

- [x] root `conformance_test.go` (package vex): FONT8 (96 glyphs) and
      SWEETIE-16 palette parsed from C/Go/JS sources and asserted equal.
      Per user decision this is THE font/palette coverage; no palette unit
      tests in vex-run.
- [x] cmd/vex-run/main_test.go: parse() subtests, rect/hline/tri raster
      fixtures, beepEngine phase+end. newTestGame() builds Game without
      ebiten/audio.
- [x] Production enabler: Game.uiReady flag — input fns return 0 until
      ebiten.RunGame starts (guards headless cart driving in tests).
- [x] cmd/vex-run/golden_test.go: renders every bin/carts/*.wasm for 30
      frames under wazero, sha256 of framebuffer vs testdata/*.golden;
      regenerate with `go test -update`; skips when carts unbuilt.
      Goldens committed for all 9 carts incl. test_hostile.
- [x] examples/test-carts/test_hostile.c registered in root build.zig:
      out-of-range btn/btnp/mbtn/pal, huge geometry, blit key=256 + zero
      dims, >127-char text/title, extreme beeps.

## 15. Verification — DONE

- [x] go vet/build/test at root and cmd/vex-run — all pass.
- [x] zig build test (spr.zig) — passes.
- [x] zig build --prefix . --release=fast — carts rebuilt incl. hostile.
- [x] cd cmd/vex && zig build --prefix ../.. — quiet link, exit 0.
- [x] bin/vex-web + bin/vex-run binaries build.

## 16. Wrap-up — DONE

- [x] REVIEW.md "Resolution status" section added after the intro;
      deferred items listed (raylib module trimming, cross-host pixel diff).

## Notes / decisions

- §2 chosen semantics: blit key = RAW compare (documented); mx/my = CLAMPED
  (README already documents 0..319/0..179; Go was already correct).
- tri canonical algorithm = Go host's integer scanline (floor crossings);
  C and JS updated to match it exactly.
- line canonical = Bresenham (Go formulation), C switched from GL DrawLine.
- Keep rmodels/rtext compiled for now (trimming is an experiment, not a fix).
