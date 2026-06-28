const std = @import("std");

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

    // raylib's Linux display backend. Forwarded to the raylib dependency;
    // X11 (the default) also covers Wayland via XWayland. Tag names match
    // raylib's own enum so the value passes straight through.
    const LinuxDisplayBackend = enum { X11, Wayland, Both, None };
    const linux_display_backend = b.option(
        LinuxDisplayBackend,
        "linux_display_backend",
        "raylib Linux display backend: X11 (default), Wayland, Both, None",
    ) orelse .X11;

    // wasm3 core interpreter sources. The optional m3_api_*.c modules
    // (WASI/libc/tracer) are skipped: the console supplies its own host
    // functions.
    const wasm3_core = [_][]const u8{
        "m3_bind.c", "m3_code.c",   "m3_compile.c",  "m3_core.c",
        "m3_emit.c", "m3_env.c",    "m3_exec.c",     "m3_function.c",
        "m3_info.c", "m3_module.c", "m3_optimize.c", "m3_parse.c",
    };

    const raylib_dep = b.lazyDependency("raylib", .{
        .target = target,
        .optimize = optimize,
        .linux_display_backend = linux_display_backend,
    }) orelse return; // raylib not yet fetched; exit cleanly so zig fetches it
    const wasm3 = b.lazyDependency("wasm3", .{}) orelse return;

    const raylib = raylib_dep.artifact("raylib");

    const exe = b.addExecutable(.{
        .name = "vex",
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
            .link_libc = true,
        }),
    });
    exe.root_module.addCSourceFile(.{
        .file = b.path("main.c"),
        .flags = &.{"-std=c23"},
    });
    exe.root_module.addCSourceFiles(.{
        .root = wasm3.path("source"),
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
    exe.root_module.addIncludePath(wasm3.path("source"));
    exe.root_module.linkLibrary(raylib); // brings in raylib headers + platform libs
    b.installArtifact(exe);

    // --- run steps ---------------------------------------------------------
    // Expect the example carts to have been installed into ../bin by the
    // top-level build (see the project Makefile). Extra args after `--` are
    // forwarded to vex, e.g. `... -- -s 5`.
    const cart_c = b.path("../bin/cart.wasm");
    const cart_z = b.path("../bin/zcart.wasm");

    const run_c = b.addRunArtifact(exe);
    run_c.addFileArg(cart_c);
    if (b.args) |a| run_c.addArgs(a);
    b.step("run", "Run the C example cart").dependOn(&run_c.step);

    const run_z = b.addRunArtifact(exe);
    run_z.addFileArg(cart_z);
    if (b.args) |a| run_z.addArgs(a);
    b.step("runz", "Run the Zig example cart").dependOn(&run_z.step);
}