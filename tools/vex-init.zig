// vex-init - scaffold a new vex cart project.
//
//   vex-init <name>
//
// Creates a <name>/ directory containing a blank Zig cart (main.zig), a
// build.zig that compiles it to wasm32, and a build.zig.zon that depends on
// the vex SDK from https://github.com/peterhellberg/vex.

const std = @import("std");

const VEX_URL = "git+https://github.com/peterhellberg/vex.git";

pub fn main(init: std.process.Init) !void {
    const a = init.arena.allocator();
    const io = init.io;

    var args = init.minimal.args.iterate();
    defer args.deinit();
    _ = args.next(); // skip the program name
    const dir_path = args.next() orelse usage();
    if (dir_path.len == 0) usage();

    const name = std.fs.path.basename(dir_path);
    const pkg = try sanitize(a, name); // a valid Zig identifier for the .zon name

    const cwd = std.Io.Dir.cwd();

    // Don't clobber an existing path.
    if (cwd.access(io, dir_path, .{})) |_| {
        std.debug.print("vex-init: {s} already exists\n", .{dir_path});
        std.process.exit(1);
    } else |_| {}

    try cwd.createDirPath(io, dir_path);
    var dir = try cwd.openDir(io, dir_path, .{});
    defer dir.close(io);

    const main_zig = try std.fmt.allocPrint(a, main_zig_tmpl, .{name});
    const build_zig = try std.fmt.allocPrint(a, build_zig_tmpl, .{name});
    const build_zon = try std.fmt.allocPrint(a, build_zon_tmpl, .{ pkg, fingerprint(pkg), VEX_URL });

    try dir.writeFile(io, .{ .sub_path = "main.zig", .data = main_zig });
    try dir.writeFile(io, .{ .sub_path = "build.zig", .data = build_zig });
    try dir.writeFile(io, .{ .sub_path = "build.zig.zon", .data = build_zon });
    try dir.writeFile(io, .{ .sub_path = ".gitignore", .data = gitignore });

    std.debug.print(
        \\Created {s}/
        \\  main.zig
        \\  build.zig
        \\  build.zig.zon
        \\  .gitignore
        \\
        \\Next steps:
        \\  cd {s}
        \\  zig fetch --save {s}
        \\  zig build
        \\  vex zig-out/bin/{s}.wasm
        \\
    , .{ dir_path, dir_path, VEX_URL, name });
}

fn usage() noreturn {
    std.debug.print("usage: vex-init <name>\n", .{});
    std.process.exit(1);
}

// Turn an arbitrary project name into a valid Zig identifier for the package
// name (alphanumeric/underscore, not starting with a digit).
fn sanitize(a: std.mem.Allocator, name: []const u8) ![]const u8 {
    var out = try a.alloc(u8, name.len + 1); // room for a possible leading '_'
    var n: usize = 0;
    for (name, 0..) |c, i| {
        const alpha = (c >= 'a' and c <= 'z') or (c >= 'A' and c <= 'Z');
        const digit = c >= '0' and c <= '9';
        if (i == 0 and digit) {
            out[n] = '_';
            n += 1;
        }
        out[n] = if (alpha or digit or c == '_') c else '_';
        n += 1;
    }
    if (n == 0) return "cart";
    return out[0..n];
}

// A build.zig.zon fingerprint: a package id paired with a checksum of the
// name, matching what the Zig toolchain generates. The id is seeded from the
// name plus a little address-space entropy so distinct projects differ.
fn fingerprint(pkg: []const u8) u64 {
    const checksum = std.hash.Crc32.hash(pkg);
    var entropy: u8 = 0;
    const seed: u64 = @as(u64, checksum) ^ @intFromPtr(&entropy);
    var prng = std.Random.DefaultPrng.init(seed);
    const id = prng.random().intRangeAtMost(u32, 1, 0xffff_fffe);
    return (@as(u64, checksum) << 32) | id;
}

const main_zig_tmpl =
    \\const vex = @import("vex");
    \\
    \\export fn boot() void {{
    \\    vex.title("{s}");
    \\}}
    \\
    \\export fn update() void {{
    \\    vex.cls(0); // clear to dark
    \\    vex.text("HELLO VEX", 8, 8, 12);
    \\}}
    \\
;

const build_zig_tmpl =
    \\const std = @import("std");
    \\
    \\pub fn build(b: *std.Build) void {{
    \\    const optimize = b.standardOptimizeOption(.{{ .preferred_optimize_mode = .ReleaseSmall }});
    \\
    \\    // Carts compile to wasm32-freestanding.
    \\    const wasm_target = b.resolveTargetQuery(.{{
    \\        .cpu_arch = .wasm32,
    \\        .os_tag = .freestanding,
    \\    }});
    \\
    \\    // The vex SDK provides the `vex` module (the host API bindings).
    \\    // `.host = false` keeps raylib/wasm3 from being fetched -- a cart only
    \\    // needs the module, not the console host.
    \\    const vex = b.dependency("vex", .{{ .host = false }});
    \\
    \\    const cart = b.addExecutable(.{{
    \\        .name = "{s}",
    \\        .root_module = b.createModule(.{{
    \\            .root_source_file = b.path("main.zig"),
    \\            .target = wasm_target,
    \\            .optimize = optimize,
    \\            .imports = &.{{.{{ .name = "vex", .module = vex.module("vex") }}}},
    \\        }}),
    \\    }});
    \\    cart.entry = .disabled; // no _start; the console calls update()
    \\    cart.rdynamic = true; // export boot()/update()
    \\    b.installArtifact(cart);
    \\}}
    \\
;

const build_zon_tmpl =
    \\.{{
    \\    .name = .{s},
    \\    .version = "0.1.0",
    \\    .fingerprint = 0x{x},
    \\    .minimum_zig_version = "0.17.0-dev.305+bdfbf432d",
    \\    .dependencies = .{{
    \\        // Populated by `zig fetch --save {s}`.
    \\    }},
    \\    .paths = .{{
    \\        "build.zig",
    \\        "build.zig.zon",
    \\        "main.zig",
    \\    }},
    \\}}
    \\
;

const gitignore =
    \\/zig-out/
    \\/.zig-cache/
    \\
;
