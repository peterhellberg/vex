const std = @import("std");

// vex builds entirely with the Zig toolchain. wasm3 (the WASM runtime) and
// raylib (graphics/input) are pulled in via build.zig.zon, so no system
// packages are required:
//
//   zig build            build ./vex + ./vex-init + cart.wasm + zcart.wasm
//   zig build run        build, then run the C example cart
//   zig build runz       build, then run the Zig example cart
//   zig build -Dhost=false   build only the carts/SDK (skip the raylib host)
//
// Carts can depend on this package and import the `vex` module; passing
// `.{ .host = false }` to b.dependency keeps the heavy host deps (raylib,
// wasm3) from being fetched.
pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{ .preferred_optimize_mode = .ReleaseFast });
    const build_host = b.option(bool, "host", "Build the console host (needs raylib + wasm3)") orelse true;

    // The cart SDK, exposed as a public module so external carts can
    // `@import("vex")`. Cheap to expose -- it pulls in no other dependencies.
    const vex_mod = b.addModule("vex", .{ .root_source_file = b.path("vex.zig") });

    // --- carts: wasm32-freestanding modules ---------------------------------
    const wasm_target = b.resolveTargetQuery(.{
        .cpu_arch = .wasm32,
        .os_tag = .freestanding,
    });

    // C cart. Imports/exports are declared via attributes in vex.h.
    const cart_c = b.addExecutable(.{
        .name = "cart",
        .root_module = b.createModule(.{
            .target = wasm_target,
            .optimize = .ReleaseSmall,
        }),
    });
    cart_c.root_module.addCSourceFile(.{ .file = b.path("cart/main.c") });
    cart_c.root_module.addIncludePath(b.path(".")); // for vex.h
    cart_c.entry = .disabled;
    b.installArtifact(cart_c);

    // Zig cart. Imports the host API from the reusable vex.zig SDK.
    const cart_zig = b.addExecutable(.{
        .name = "zcart",
        .root_module = b.createModule(.{
            .root_source_file = b.path("zcart/main.zig"),
            .target = wasm_target,
            .optimize = .ReleaseSmall,
            .imports = &.{.{ .name = "vex", .module = vex_mod }},
        }),
    });
    cart_zig.entry = .disabled;
    cart_zig.rdynamic = true; // export the `export fn`s
    b.installArtifact(cart_zig);

    // --- vex-init: scaffold a new cart project ------------------------------
    const init_exe = b.addExecutable(.{
        .name = "vex-init",
        .root_module = b.createModule(.{
            .root_source_file = b.path("tools/vex-init.zig"),
            .target = target,
            .optimize = optimize,
        }),
    });
    b.installArtifact(init_exe);

    // --- host: ./vex --------------------------------------------------------
    // Gated so cart-only consumers (host = false) never fetch raylib/wasm3.
    if (build_host) {
        // wasm3 core interpreter sources. The optional m3_api_*.c modules
        // (WASI/libc/tracer) are skipped: the console supplies its own host
        // functions.
        const wasm3_core = [_][]const u8{
            "m3_bind.c",  "m3_code.c",   "m3_compile.c",  "m3_core.c",
            "m3_emit.c",  "m3_env.c",    "m3_exec.c",     "m3_function.c",
            "m3_info.c",  "m3_module.c", "m3_optimize.c", "m3_parse.c",
        };

        if (b.lazyDependency("raylib", .{ .target = target, .optimize = optimize })) |raylib_dep| {
            if (b.lazyDependency("wasm3", .{})) |wasm3| {
                const raylib = raylib_dep.artifact("raylib");

                const exe = b.addExecutable(.{
                    .name = "vex",
                    .root_module = b.createModule(.{
                        .target = target,
                        .optimize = optimize,
                        .link_libc = true,
                    }),
                });
                exe.root_module.addCSourceFile(.{ .file = b.path("main.c"), .flags = &.{"-std=c11"} });
                exe.root_module.addCSourceFiles(.{
                    .root = wasm3.path("source"),
                    .files = &wasm3_core,
                });
                exe.root_module.addIncludePath(wasm3.path("source"));
                exe.root_module.linkLibrary(raylib); // brings in raylib headers + platform libs
                b.installArtifact(exe);

                // --- run steps --------------------------------------------------
                // Extra args after `--` are forwarded to vex, e.g. `... -- -s 5`.
                const run_c = b.addRunArtifact(exe);
                run_c.addFileArg(cart_c.getEmittedBin());
                if (b.args) |a| run_c.addArgs(a);
                b.step("run", "Run the C example cart").dependOn(&run_c.step);

                const run_z = b.addRunArtifact(exe);
                run_z.addFileArg(cart_zig.getEmittedBin());
                if (b.args) |a| run_z.addArgs(a);
                b.step("runz", "Run the Zig example cart").dependOn(&run_z.step);
            }
        }
    }
}
