const std = @import("std");

// vex SDK package: the cart SDK + example carts + vex-init scaffolder.
//
// This package has NO external dependencies -- a cart that just depends on
// `vex` fetches nothing heavy. The console host itself (./vex, the C binary
// that links raylib + wasm3) lives in a separate `cmd/vex/` package so the
// raylib/wasm3 deps aren't pulled in by everyone.
//
//   zig build           build ./vex-init + cart.wasm + zcart.wasm
//   zig build --prefix .   install everything into ./bin
//
// The host is built separately, see cmd/vex/build.zig (or just run `make`,
// which builds both).
pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{
        .preferred_optimize_mode = .ReleaseFast,
    });

    // The cart SDK, exposed as a public module so external carts can
    // `@import("vex")`. Cheap to expose -- it pulls in no other dependencies.
    const vex_mod = b.addModule("vex", .{ .root_source_file = b.path("vex.zig") });

    // comptime PNG→spr decoder, imported as `const spr = @import("spr");`.
    const spr_mod = b.addModule("spr", .{ .root_source_file = b.path("spr.zig") });

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
    cart_c.root_module.addCSourceFile(.{ .file = b.path("examples/cart/main.c") });
    cart_c.root_module.addIncludePath(b.path(".")); // for vex.h
    cart_c.entry = .disabled;
    b.installArtifact(cart_c);

    // Zig cart. Imports the host API from the reusable vex.zig SDK.
    const cart_zig = b.addExecutable(.{
        .name = "zcart",
        .root_module = b.createModule(.{
            .root_source_file = b.path("examples/zcart/main.zig"),
            .target = wasm_target,
            .optimize = .ReleaseSmall,
            .imports = &.{
                .{ .name = "vex", .module = vex_mod },
                .{ .name = "spr", .module = spr_mod },
            },
        }),
    });
    cart_zig.entry = .disabled;
    cart_zig.rdynamic = true; // export the `export fn`s
    b.installArtifact(cart_zig);

    // --- vex-init: scaffold a new cart project ------------------------------
    const init_exe = b.addExecutable(.{
        .name = "vex-init",
        .root_module = b.createModule(.{
            .root_source_file = b.path("cmd/vex-init/main.zig"),
            .target = target,
            .optimize = optimize,
        }),
    });
    b.installArtifact(init_exe);
}