const std = @import("std");

// vex SDK package: the cart SDK + example carts + vex-init scaffolder.
//
// This package has NO external dependencies -- a cart that just depends on
// `vex` fetches nothing heavy. The console host itself (./vex, the C binary
// that links raylib + wasm3) lives in a separate `cmd/vex/` package so the
// raylib/wasm3 deps aren't pulled in by everyone.
//
//   zig build              build ./vex-init + cart.wasm + zcart.wasm
//   zig build --prefix .   install vex-init into ./bin and carts into ./bin/carts
//   zig build test         run the SDK tests
//
// The host is built separately, see cmd/vex/build.zig (or just run `make`,
// which builds both).
pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{
        .preferred_optimize_mode = .ReleaseFast,
    });
    const strip = optimize != .Debug;

    // The cart SDK, exposed as a public module so external carts can
    // `@import("vex")`. Cheap to expose -- it pulls in no other dependencies.
    const vex_mod = b.addModule("vex", .{ .root_source_file = b.path("vex.zig") });

    // The chiptune tracker, exposed as `const mus = @import("mus")` for Zig carts.
    const mus_mod = b.addModule("mus", .{
        .root_source_file = b.path("mus.zig"),
        .imports = &.{
            .{ .name = "vex", .module = vex_mod },
        },
    });

    // comptime PNG→spr decoder, imported as `const spr = @import("spr");`.
    const spr_mod = b.addModule("spr", .{ .root_source_file = b.path("spr.zig") });

    // --- carts: wasm32-freestanding modules ---------------------------------
    // Example carts are opt-in so `vex` as a dependency doesn't build them
    // for every downstream cart (`zig build` in a project that `@import("vex")`
    // should only get the `vex`/`mus`/`spr` modules). Pass `-Dexamples=true`
    // or run `make` (which does) to build them.
    const build_examples = b.option(bool, "examples", "Build example carts") orelse false;
    if (build_examples) {
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
                .strip = true,
            }),
        });
        cart_c.root_module.addCSourceFile(.{ .file = b.path("examples/cart/main.c") });
        cart_c.root_module.addIncludePath(b.path(".")); // for vex.h
        cart_c.entry = .disabled;
        b.getInstallStep().dependOn(&b.addInstallArtifact(cart_c, .{ .dest_dir = .{ .override = .{ .custom = "bin/carts" } } }).step);

        // Zig cart. Imports the host API from the reusable vex.zig SDK.
        const cart_zig = b.addExecutable(.{
            .name = "zcart",
            .root_module = b.createModule(.{
                .root_source_file = b.path("examples/zcart/main.zig"),
                .target = wasm_target,
                .optimize = .ReleaseSmall,
                .strip = true,
                .imports = &.{
                    .{ .name = "vex", .module = vex_mod },
                    .{ .name = "spr", .module = spr_mod },
                },
            }),
        });
        cart_zig.entry = .disabled;
        cart_zig.rdynamic = true; // export the `export fn`s
        b.getInstallStep().dependOn(&b.addInstallArtifact(cart_zig, .{ .dest_dir = .{ .override = .{ .custom = "bin/carts" } } }).step);

        // --- test carts: wasm32-freestanding stress-test modules ------------------
        const test_carts = [_][]const u8{
            "test_audio", "test_coords", "test_blit", "test_arith", "test_palette",
            "test_api",   "test_bench",  "test_font", "test_hostile", "test_music",
        };
        inline for (test_carts) |name| {
            const t = b.addExecutable(.{
                .name = name,
                .root_module = b.createModule(.{
                    .target = wasm_target,
                    .optimize = .ReleaseSmall,
                    .strip = true,
                }),
            });
            t.root_module.addCSourceFile(.{ .file = b.path("examples/test-carts/" ++ name ++ ".c") });
            t.root_module.addIncludePath(b.path("."));
            t.entry = .disabled;
            b.getInstallStep().dependOn(&b.addInstallArtifact(t, .{ .dest_dir = .{ .override = .{ .custom = "bin/carts" } } }).step);
        }

        // Zig version of the music demo — Beethoven Für Elise, exercises mus.zig.
        const zmusic = b.addExecutable(.{
            .name = "test_zmusic",
            .root_module = b.createModule(.{
                .root_source_file = b.path("examples/test-carts/test_zmusic.zig"),
                .target = wasm_target,
                .optimize = .ReleaseSmall,
                .strip = true,
                .imports = &.{
                    .{ .name = "vex", .module = vex_mod },
                    .{ .name = "mus", .module = mus_mod },
                },
            }),
        });
        zmusic.entry = .disabled;
        zmusic.rdynamic = true;
        b.getInstallStep().dependOn(&b.addInstallArtifact(zmusic, .{ .dest_dir = .{ .override = .{ .custom = "bin/carts" } } }).step);
    }

    // --- tests: run the SDK's zig test blocks (`zig build test`) -------------
    const sdk_tests = b.addTest(.{ .root_module = b.createModule(.{
        .root_source_file = b.path("spr.zig"),
        .target = b.resolveTargetQuery(.{}),
        .optimize = optimize,
        .strip = strip,
    }) });
    const run_sdk_tests = b.addRunArtifact(sdk_tests);
    const test_step = b.step("test", "Run SDK tests (spr.zig)");
    test_step.dependOn(&run_sdk_tests.step);

    // --- vex-init: scaffold a new cart project ------------------------------
    const init_exe = b.addExecutable(.{
        .name = "vex-init",
        .root_module = b.createModule(.{
            .root_source_file = b.path("cmd/vex-init/main.zig"),
            .target = target,
            .optimize = optimize,
            .strip = strip,
        }),
    });
    b.installArtifact(init_exe);
}
