const std = @import("std");

/// raylib's Linux display backend, mapped to GLFW's _GLFW_X11 /
/// _GLFW_WAYLAND macros by addRaylib.
pub const LinuxDisplayBackend = enum { X11, Wayland, Both, None };

// vex - the C console host.
//
// Lives in its own package so that the cart SDK (the parent package) has no
// transitive dependency on raylib or wasm3. Only this build pulls those in;
// a cart that just depends on the `vex` SDK fetches neither.
//
//   zig build         build ./vex into ./zig-out/bin (or --prefix . into ./bin)
//   zig build run     build and run the C example cart
//   zig build runz    build and run the Zig example cart
//
// The `run` / `runz` steps expect the example carts to already be installed
// at ../bin/cart.wasm and ../bin/zcart.wasm (built by the top-level `make`).
pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{
        .preferred_optimize_mode = .ReleaseFast,
    });
    const strip = optimize != .Debug;

    // raylib's Linux display backend. X11 (the default) also covers Wayland
    // via XWayland; tag names match raylib's own enum.
    const linux_display_backend = b.option(
        LinuxDisplayBackend,
        "linux_display_backend",
        "raylib Linux display backend: X11 (default), Wayland, Both, None",
    ) orelse .X11;

    // wasm3 core interpreter sources, vendored from the v0.9.0 release
    // tag (see wasm3/README.md).
    // Vendored rather than fetched: the pinned master build.zig predates
    // current Zig's Build API and fails to even load as a dependency.
    //
    // The optional m3_api_*.c modules (WASI/libc/tracer) are skipped: the
    // console supplies its own host functions. m3_emit/m3_optimize no longer
    // exist upstream; m3_validate.c is new and required.
    const wasm3_core = [_][]const u8{
        "m3_bind.c",  "m3_code.c",   "m3_compile.c", "m3_core.c",
        "m3_env.c",   "m3_exec.c",   "m3_function.c",
        "m3_info.c",  "m3_module.c", "m3_parse.c",   "m3_validate.c",
    };

    // macOS framework stubs (AppKit, IOKit, ...) for cross-compiling to
    // macOS from a non-macOS host. linkFramework() alone doesn't satisfy a
    // Mach-O link from Linux, so the stub include + framework + lib paths
    // are added alongside it below.
    const xcode_frameworks = if (target.result.os.tag == .macos)
        b.lazyDependency("xcode_frameworks", .{})
    else
        null;

    const raylib_dep = b.lazyDependency("raylib", .{
        .target = target,
        .optimize = optimize,
    }) orelse return; // raylib not yet fetched; exit cleanly so zig fetches it

    const exe = b.addExecutable(.{
        .name = "vex",
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
            .strip = strip,
            .link_libc = true,
        }),
    });
    if (target.result.os.tag == .windows) {
        exe.subsystem = .Windows;
    }
    exe.root_module.addCSourceFile(.{
        .file = b.path("main.c"),
        // -ffp-contract=off keeps the triangle edge math bit-for-bit
        // identical to the Go/JS hosts: clang would otherwise fuse
        // ax + (y-ay)*slope into an FMA whose rounding differs from their
        // separate multiply+add, moving individual edge pixels.
        .flags = &.{ "-std=c23", "-ffp-contract=off" },
    });

    // Compile raylib's sources directly into the executable and link the
    // platform libraries ourselves, instead of linking the dependency's
    // static archive (which embeds resolved system libraries as archive
    // members and makes the linker emit warnings for each of them).
    addRaylib(exe.root_module, raylib_dep, linux_display_backend);

    if (xcode_frameworks) |fws| {
        exe.root_module.addSystemIncludePath(fws.path("include"));
    }

    exe.root_module.addCSourceFiles(.{
        .root = b.path("wasm3"),
        .files = &wasm3_core,
        // By default wasm3 packs its slot/constant tables as u32
        // (d_m3Use32BitSlots) and then stores 64-bit constants into them,
        // producing unaligned 8-byte writes (PushConst -> Compile_Const_i64).
        // x86_64/arm64 tolerate that, but Zig's UBSan traps it in
        // Debug/ReleaseSafe and aborts on the first update() of any cart with
        // an i64 const. Widen the slots to u64 so those writes are naturally
        // aligned -- the supported 64-bit-host configuration, correct in
        // every build mode.
        .flags = &.{ "-Dd_m3Use32BitSlots=0", "-fwrapv" },
    });
    exe.root_module.addIncludePath(b.path("wasm3"));
    if (xcode_frameworks) |fws| {
        exe.root_module.addSystemFrameworkPath(fws.path("Frameworks"));
        exe.root_module.addLibraryPath(fws.path("lib"));
    }
    b.installArtifact(exe);

    // --- run steps ---------------------------------------------------------
    // Expect the example carts to have been installed into ../../bin by the
    // top-level build (see the project Makefile). Extra args after `--` are
    // forwarded to vex, e.g. `... -- -s 5`.
    const cart_c = b.path("../../bin/carts/cart.wasm");
    const cart_z = b.path("../../bin/carts/zcart.wasm");

    const run_c = b.addRunArtifact(exe);
    run_c.addFileArg(cart_c);
    if (b.args) |a| run_c.addArgs(a);
    b.step("run", "Run the C example cart").dependOn(&run_c.step);

    const run_z = b.addRunArtifact(exe);
    run_z.addFileArg(cart_z);
    if (b.args) |a| run_z.addArgs(a);
    b.step("runz", "Run the Zig example cart").dependOn(&run_z.step);
}

/// Compile raylib (desktop GLFW backend) directly into the given module and
/// link its platform libraries, instead of linking the dependency's prebuilt
/// static archive.
fn addRaylib(
    mod: *std.Build.Module,
    raylib_dep: *std.Build.Dependency,
    linux_display_backend: LinuxDisplayBackend,
) void {
    const target = mod.resolved_target.?.result;

    const rl_src = raylib_dep.path("src");

    mod.addIncludePath(rl_src);
    mod.addIncludePath(raylib_dep.path("src/platforms"));
    mod.addIncludePath(raylib_dep.path("src/external/glfw/include"));

    mod.addCMacro("_GNU_SOURCE", "");
    mod.addCMacro("GL_SILENCE_DEPRECATION", "199309L");
    mod.addCMacro("SUPPORT_MODULE_RSHAPES", "1");
    mod.addCMacro("SUPPORT_MODULE_RTEXTURES", "1");
    mod.addCMacro("SUPPORT_MODULE_RTEXT", "1");
    mod.addCMacro("SUPPORT_MODULE_RMODELS", "1");
    mod.addCMacro("SUPPORT_MODULE_RAUDIO", "1");
    mod.addCMacro("PLATFORM_DESKTOP_GLFW", "");
    mod.addCMacro("GRAPHICS_API_OPENGL_33", "");

    switch (target.os.tag) {
        .linux => {
            mod.linkSystemLibrary("GL", .{});
            if (linux_display_backend != .Wayland) {
                // X11 is the default (and covers Wayland via XWayland).
                // None also resolves here: GLFW refuses to compile without
                // a window-system backend, so "none" can only mean "X11
                // macros, kept for compatibility with the old option".
                mod.addCMacro("_GLFW_X11", "");
                inline for (.{ "X11", "Xrandr", "Xinerama", "Xi", "Xcursor" }) |lib| {
                    mod.linkSystemLibrary(lib, .{});
                }
            }
            if (linux_display_backend == .Wayland or linux_display_backend == .Both) {
                mod.addCMacro("_GLFW_WAYLAND", "");
                inline for (.{ "wayland-client", "wayland-cursor", "wayland-egl", "xkbcommon" }) |lib| {
                    mod.linkSystemLibrary(lib, .{});
                }
            }
        },
        .windows => {
            inline for (.{ "opengl32", "winmm", "gdi32" }) |lib| {
                mod.linkSystemLibrary(lib, .{});
            }
        },
        .macos => {
            inline for (.{ "Foundation", "CoreServices", "CoreGraphics", "AppKit", "IOKit", "QuartzCore" }) |fw| {
                mod.linkFramework(fw, .{});
            }
        },
        else => @panic("vex: unsupported target OS for raylib"),
    }

    mod.addCSourceFiles(.{
        .root = rl_src,
        .files = &.{
            "rcore.c",
            "rshapes.c",
            "rtextures.c",
            "rtext.c",
            "rmodels.c",
            "raudio.c",
        },
        .flags = &.{"-std=c99"},
    });

    // rglfw.c includes Objective-C on macOS and must be compiled separately.
    const glfw_flags: []const []const u8 = if (target.os.tag == .macos)
        &.{ "-std=c99", "-ObjC" }
    else
        &.{"-std=c99"};
    mod.addCSourceFile(.{
        .file = raylib_dep.path("src/rglfw.c"),
        .flags = glfw_flags,
    });
}
