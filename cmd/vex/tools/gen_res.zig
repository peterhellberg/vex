const std = @import("std");

pub fn main(init: std.process.Init.Minimal) !void {
    var arena = std.heap.ArenaAllocator.init(std.heap.page_allocator);
    defer arena.deinit();
    const allocator = arena.allocator();

    var threaded: std.Io.Threaded = .init(allocator, .{
        .argv0 = .init(init.args),
        .environ = init.environ,
    });
    defer threaded.deinit();
    const io = threaded.io();

    const vec = init.args.vector;
    // vec includes argv0, so we expect 5 entries for 4 real args
    if (vec.len != 5) {
        std.debug.print("usage: gen_res <x86_64|x86|aarch64> <icon.ico> <manifest.xml> <out.o>\n", .{});
        return error.InvalidArgs;
    }

    const arch_s = std.mem.span(vec[1]);
    const ico_path = std.mem.span(vec[2]);
    const manifest_path = std.mem.span(vec[3]);
    const out_path = std.mem.span(vec[4]);

    const machine: u16 = if (std.mem.eql(u8, arch_s, "x86_64"))
        0x8664
    else if (std.mem.eql(u8, arch_s, "x86"))
        0x014c
    else if (std.mem.eql(u8, arch_s, "aarch64") or std.mem.eql(u8, arch_s, "arm64"))
        0xAA64
    else {
        std.debug.print("usage: gen_res <x86_64|x86|aarch64> <icon.ico> <manifest.xml> <out.o>\n", .{});
        return error.InvalidArgs;
    };

    const cwd = std.Io.Dir.cwd();
    const ico_data = try cwd.readFileAlloc(io, ico_path, allocator, .limited(16 * 1024 * 1024));
    const manifest_data = try cwd.readFileAlloc(io, manifest_path, allocator, .limited(16 * 1024 * 1024));

    // Parse .ico
    const images = try parseIco(allocator, ico_data);

    // Build GRPICONDIR blob
    const grp_blob = try buildGrpBlob(allocator, images);

    // Build .rsrc section
    const rsrc = try buildRsrc(allocator, images, grp_blob, manifest_data);

    // Build COFF object
    const coff = try buildCoff(allocator, machine, rsrc);

    try cwd.writeFile(io, .{ .sub_path = out_path, .data = coff });
}

const IcoImage = struct {
    width: u8,
    height: u8,
    color_count: u8,
    planes: u16,
    bit_count: u16,
    data: []const u8,
};

fn parseIco(allocator: std.mem.Allocator, data: []const u8) ![]IcoImage {
    if (data.len < 6) return error.BadIcoFile;
    const reserved = readU16(data, 0) catch return error.BadIcoFile;
    const typ = readU16(data, 2) catch return error.BadIcoFile;
    const count = readU16(data, 4) catch return error.BadIcoFile;
    if (reserved != 0 or typ != 1) return error.BadIcoFile;
    if (6 + @as(usize, count) * 16 > data.len) return error.BadIcoFile;

    var images = try allocator.alloc(IcoImage, count);

    for (0..count) |i| {
        const off = 6 + i * 16;
        const width = data[off];
        const height = data[off + 1];
        const color_count = data[off + 2];
        // data[off+3] reserved
        const planes = try readU16(data, off + 4);
        const bit_count = try readU16(data, off + 6);
        const bytes_in_res = try readU32(data, off + 8);
        const image_offset = try readU32(data, off + 12);
        const len = @as(usize, bytes_in_res);
        const ioff = @as(usize, image_offset);
        if (ioff + len > data.len) return error.BadIcoFile;
        if (len == 0) return error.BadIcoFile;
        images[i] = .{
            .width = width,
            .height = height,
            .color_count = color_count,
            .planes = planes,
            .bit_count = bit_count,
            .data = data[ioff .. ioff + len],
        };
    }
    return images;
}

fn buildGrpBlob(allocator: std.mem.Allocator, images: []const IcoImage) ![]const u8 {
    var list: std.ArrayList(u8) = .empty;
    defer list.deinit(allocator);
    try writeU16(&list, allocator, 0);
    try writeU16(&list, allocator, 1);
    try writeU16(&list, allocator, @intCast(images.len));
    for (images, 0..) |img, idx| {
        const w: u16 = if (img.width == 0) 256 else img.width;
        const h: u16 = if (img.height == 0) 256 else img.height;
        try writeU16(&list, allocator, w);
        try writeU16(&list, allocator, h);
        try list.append(allocator, img.color_count);
        try list.append(allocator, 0);
        try writeU16(&list, allocator, img.planes);
        try writeU16(&list, allocator, img.bit_count);
        try writeU32(&list, allocator, @intCast(img.data.len));
        try writeU16(&list, allocator, @intCast(idx + 1));
    }
    return try list.toOwnedSlice(allocator);
}

fn buildRsrc(
    allocator: std.mem.Allocator,
    images: []const IcoImage,
    grp_blob: []const u8,
    manifest: []const u8,
) ![]const u8 {
    const n = images.len;
    const L = n + 2; // icons + group + manifest

    // payloads in order
    var payloads = try allocator.alloc([]const u8, L);
    for (0..n) |i| payloads[i] = images[i].data;
    payloads[n] = grp_blob;
    payloads[n + 1] = manifest;

    const num_types: usize = if (n > 0) 3 else 2;
    var id_dir_counts = try allocator.alloc(usize, num_types);
    if (n > 0) {
        id_dir_counts[0] = n;
        id_dir_counts[1] = 1;
        id_dir_counts[2] = 1;
    } else {
        id_dir_counts[0] = 1;
        id_dir_counts[1] = 1;
    }

    // Compute offsets with root at 0 so Windows loader finds the resource tree.
    // Order: root -> ID dirs -> language dirs -> leaves -> payloads.
    // This keeps all offsets section-relative (no relocations) and satisfies
    // the PE DataDirectory requirement that the resource directory is at .rsrc RVA.
    var cur: usize = 0;
    const root_offset: usize = cur;
    const root_size: usize = 16 + 8 * num_types;
    cur += root_size;

    var id_dir_offsets = try allocator.alloc(u32, num_types);
    for (0..num_types) |i| {
        id_dir_offsets[i] = @intCast(cur);
        cur += 16 + 8 * id_dir_counts[i];
    }

    const lang_start: usize = cur;
    const lang_size: usize = L * 24;
    cur += lang_size;

    const leaf_offset: usize = cur;
    const leaf_size: usize = L * 16;
    cur += leaf_size;

    cur = align4(cur);
    var payload_offsets = try allocator.alloc(u32, L);
    for (0..L) |i| {
        cur = align4(cur);
        payload_offsets[i] = @intCast(cur);
        cur += payloads[i].len;
    }
    const total: usize = cur;

    var out = try allocator.alloc(u8, total);
    @memset(out, 0);

    // 1. root
    const type_ids: []const u32 = if (n > 0) &[_]u32{ 3, 14, 24 } else &[_]u32{ 14, 24 };
    writeU32At(out, root_offset, 0);
    writeU32At(out, root_offset + 4, 0);
    writeU16At(out, root_offset + 8, 0);
    writeU16At(out, root_offset + 10, 0);
    writeU16At(out, root_offset + 12, 0);
    writeU16At(out, root_offset + 14, @intCast(num_types));
    for (0..num_types) |i| {
        const eoff = root_offset + 16 + i * 8;
        writeU32At(out, eoff, type_ids[i]);
        writeU32At(out, eoff + 4, id_dir_offsets[i] | 0x80000000);
    }

    // 2. ID dirs
    var leaf_start: usize = 0;
    for (0..num_types) |ti| {
        const off = id_dir_offsets[ti];
        const cnt = id_dir_counts[ti];
        writeU32At(out, off, 0);
        writeU32At(out, off + 4, 0);
        writeU16At(out, off + 8, 0);
        writeU16At(out, off + 10, 0);
        writeU16At(out, off + 12, 0);
        writeU16At(out, off + 14, @intCast(cnt));
        for (0..cnt) |j| {
            const eoff = off + 16 + j * 8;
            const resource_id: u32 = if (ti == 0 and n > 0)
                @intCast(j + 1)
            else
                1;
            const lang_off = lang_start + (leaf_start + j) * 24;
            writeU32At(out, eoff, resource_id);
            writeU32At(out, eoff + 4, @as(u32, @intCast(lang_off)) | 0x80000000);
        }
        leaf_start += cnt;
    }

    // 3. language dirs
    for (0..L) |i| {
        const off = lang_start + i * 24;
        writeU32At(out, off, 0);
        writeU32At(out, off + 4, 0);
        writeU16At(out, off + 8, 0);
        writeU16At(out, off + 10, 0);
        writeU16At(out, off + 12, 0);
        writeU16At(out, off + 14, 1);
        writeU32At(out, off + 16, 0x0409);
        const leaf_entry_off: u32 = @intCast(leaf_offset + i * 16);
        writeU32At(out, off + 20, leaf_entry_off);
    }

    // 4. leaves
    for (0..L) |i| {
        const off = leaf_offset + i * 16;
        writeU32At(out, off, payload_offsets[i]);
        writeU32At(out, off + 4, @intCast(payloads[i].len));
        writeU32At(out, off + 8, 0);
        writeU32At(out, off + 12, 0);
    }

    // 5. payloads (4-byte aligned)
    for (0..L) |i| {
        const off = payload_offsets[i];
        @memcpy(out[off .. off + payloads[i].len], payloads[i]);
    }

    return out;
}

fn buildCoff(allocator: std.mem.Allocator, machine: u16, rsrc: []const u8) ![]const u8 {
    const rsrc_len: u32 = @intCast(rsrc.len);
    const ptr_sym: u32 = 20 + 40 + rsrc_len;

    var list: std.ArrayList(u8) = .empty;
    defer list.deinit(allocator);

    // File header 20
    try writeU16(&list, allocator, machine);
    try writeU16(&list, allocator, 1);
    try writeU32(&list, allocator, 0);
    try writeU32(&list, allocator, ptr_sym);
    try writeU32(&list, allocator, 1);
    try writeU16(&list, allocator, 0);
    try writeU16(&list, allocator, 0);

    // Section header 40
    // Name .rsrc
    try list.appendSlice(allocator, &[_]u8{ '.', 'r', 's', 'r', 'c', 0, 0, 0 });
    try writeU32(&list, allocator, 0); // VirtualSize
    try writeU32(&list, allocator, 0); // VirtualAddress
    try writeU32(&list, allocator, rsrc_len); // SizeOfRawData
    try writeU32(&list, allocator, 60); // PointerToRawData
    try writeU32(&list, allocator, 0); // PointerToRelocations
    try writeU32(&list, allocator, 0); // PointerToLineNumbers
    try writeU16(&list, allocator, 0); // NumberOfRelocations
    try writeU16(&list, allocator, 0); // NumberOfLineNumbers
    try writeU32(&list, allocator, 0x40000040); // Characteristics

    // .rsrc raw data
    try list.appendSlice(allocator, rsrc);

    // Symbol table 18
    try list.appendSlice(allocator, &[_]u8{ '.', 'r', 's', 'r', 'c', 0, 0, 0 });
    try writeU32(&list, allocator, 0); // Value
    try writeU16(&list, allocator, 1); // SectionNumber
    try writeU16(&list, allocator, 0); // Type
    try list.append(allocator, 3); // StorageClass
    try list.append(allocator, 0); // NumberOfAuxSymbols

    // String table 4
    try writeU32(&list, allocator, 4);

    return try list.toOwnedSlice(allocator);
}

// helpers

fn readU16(data: []const u8, off: usize) !u16 {
    if (off + 2 > data.len) return error.BadIcoFile;
    return std.mem.readInt(u16, data[off..][0..2], .little);
}

fn readU32(data: []const u8, off: usize) !u32 {
    if (off + 4 > data.len) return error.BadIcoFile;
    return std.mem.readInt(u32, data[off..][0..4], .little);
}

fn writeU16(list: *std.ArrayList(u8), gpa: std.mem.Allocator, v: u16) !void {
    var buf: [2]u8 = undefined;
    std.mem.writeInt(u16, &buf, v, .little);
    try list.appendSlice(gpa, &buf);
}

fn writeU32(list: *std.ArrayList(u8), gpa: std.mem.Allocator, v: u32) !void {
    var buf: [4]u8 = undefined;
    std.mem.writeInt(u32, &buf, v, .little);
    try list.appendSlice(gpa, &buf);
}

fn writeU32At(buf: []u8, off: usize, v: u32) void {
    std.mem.writeInt(u32, buf[off..][0..4], v, .little);
}

fn writeU16At(buf: []u8, off: usize, v: u16) void {
    std.mem.writeInt(u16, buf[off..][0..2], v, .little);
}

fn align4(x: usize) usize {
    return (x + 3) & ~@as(usize, 3);
}
