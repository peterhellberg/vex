Vendored copy of wasm3, pinned to the v0.9.0 release tag
(0cd38327f0c721e75172f4f1eeb55854dc0517af); the interpreter sources here
are byte-identical to that tag's source/ tree.

Why vendored instead of a zig-pkg dependency:

- Its own build.zig predates current Zig's Build API (addStaticLibrary
  was removed) and fails to even load as a dependency under current Zig;
  upstream PR #576 fixes it but is still unmerged (milestone v0.9.1).
  Until that lands, only a vendored copy works -- and once it does, this
  tree can be dropped for a build.zig.zon URL dependency.

Only the interpreter core is kept; the optional m3_api_*.c modules
(WASI/libc/tracer) are skipped because the console supplies its own host
functions. Upstream reorganizations reflected here: m3_emit and m3_optimize
no longer exist, m3_validate.c is new (and required).

Bulk-memory operations (memory.copy/fill), which modern Zig emits from
any cart touching std.fmt/std.mem, are fully supported by this release.

To update: copy source/*.c + *.h from the new pinned tag (minus the
m3_api_* modules), adjust the file list in build.zig if files were added
or removed, and re-run the full test suite (`make test-hosts`, golden
tests, headless runs of every cart).
