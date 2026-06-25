const std = @import("std");

/// Return a Sprite struct type with a fixed-size pixel buffer.
pub fn Sprite(comptime max_size: usize) type {
    return struct {
        data: [max_size]u8,
        len: usize,

        pub fn slice(self: *const @This()) []const u8 {
            return self.data[0..self.len];
        }
    };
}

/// Decode a paletted (indexed) PNG into raw sprite pixel bytes at comptime.
/// `max_size` is the maximum expected pixel count (width × height).
/// Usage:  const sprite = fromPNG(@embedFile("sprite-32x32.png"), 1024);
pub fn fromPNG(comptime input: []const u8, comptime max_size: usize) Sprite(max_size) {
    return comptime blk: {
        @setEvalBranchQuota(200_000);

        const r32 = struct {
            fn read(data: []const u8, off: usize) u32 {
                return (@as(u32, data[off]) << 24) |
                    (@as(u32, data[off + 1]) << 16) |
                    (@as(u32, data[off + 2]) << 8) |
                    @as(u32, data[off + 3]);
            }
        }.read;

        const sig = [_]u8{ 137, 80, 78, 71, 13, 10, 26, 10 };
        if (!std.mem.eql(u8, input[0..8], &sig))
            @compileError("not a PNG");

        var pos: usize = 8;

        if (pos + 8 > input.len) @compileError("truncated PNG");
        const ihdr_len = r32(input, pos);
        if (input[pos + 4] != 'I' or input[pos + 5] != 'H' or
            input[pos + 6] != 'D' or input[pos + 7] != 'R')
            @compileError("expected IHDR");
        pos += 8;
        if (ihdr_len != 13) @compileError("bad IHDR length");
        if (pos + 13 > input.len) @compileError("truncated IHDR");

        const w = r32(input, pos);
        const h = r32(input, pos + 4);
        const bit_depth = input[pos + 8];
        const color_type = input[pos + 9];

        if (color_type != 3) @compileError("not a paletted PNG (color type 3)");
        if (bit_depth != 8) @compileError("only 8-bit paletted PNGs supported");
        if (input[pos + 10] != 0) @compileError("unknown compression method");
        if (input[pos + 11] != 0) @compileError("unknown filter method");
        if (input[pos + 12] != 0) @compileError("interlaced PNGs not supported");

        pos += ihdr_len + 4;

        var idat_total: usize = 0;
        var idat_off: usize = 0;
        var found_plte = false;

        while (pos + 8 <= input.len) {
            const len = r32(input, pos);
            const t0 = input[pos + 4];
            const t1 = input[pos + 5];
            const t2 = input[pos + 6];
            const t3 = input[pos + 7];

            if (t0 == 'P') {
                found_plte = true;
            } else if (t0 == 'I' and t1 == 'D' and t2 == 'A' and t3 == 'T') {
                if (idat_total == 0) idat_off = pos + 8;
                idat_total += len;
            } else if (t0 == 'I' and t1 == 'E' and t2 == 'N' and t3 == 'D') {
                break;
            }

            pos += 12 + len;
        }

        if (!found_plte) @compileError("missing PLTE chunk");
        if (idat_total == 0) @compileError("missing IDAT chunk");

        const idat = input[idat_off..][0..idat_total];

        const raw = inflate(idat);

        const stride: usize = w;
        const row_len: usize = 1 + stride;

        var pixels: [max_size]u8 = undefined;
        if (w * h > max_size) @compileError("sprite too large for buffer");
        var prev_row: [max_size]u8 = @splat(0);

        var y: usize = 0;
        while (y < h) : (y += 1) {
            const ftype = raw[y * row_len];
            const src = raw[y * row_len + 1 .. y * row_len + row_len];
            const dst = pixels[y * stride .. y * stride + stride];

            var x: usize = 0;
            while (x < stride) : (x += 1) {
                const p = src[x];
                dst[x] = switch (ftype) {
                    0 => p,
                    1 => if (x == 0) p else p +% dst[x - 1],
                    2 => p +% prev_row[x],
                    3 => blk3: {
                        const a: u16 = if (x == 0) 0 else dst[x - 1];
                        const b: u16 = prev_row[x];
                        break :blk3 p +% @as(u8, @truncate((a + b) / 2));
                    },
                    4 => p +% paeth(
                        if (x == 0) 0 else dst[x - 1],
                        prev_row[x],
                        if (x == 0) 0 else prev_row[x - 1],
                    ),
                    else => @compileError("unknown filter type"),
                };
            }

            @memcpy(prev_row[0..stride], dst);
        }

        break :blk Sprite(max_size){ .data = pixels, .len = w * h };
    };
}

fn paeth(a: u8, b: u8, c: u8) u8 {
    const p = @as(i32, a) + @as(i32, b) - @as(i32, c);
    const pa = @abs(p - a);
    const pb = @abs(p - b);
    const pc = @abs(p - c);
    return if (pa <= pb and pa <= pc) a else if (pb <= pc) b else c;
}

// ---- inflate (RFC 1951 deflate decompression) -------------------------------

fn inflate(data: []const u8) []const u8 {
    var pos: usize = 2;

    var out: [262144]u8 = @splat(0);
    var out_pos: usize = 0;

    var tree: [2048]i32 = @splat(0);
    var tree_len: usize = 1;

    var bitbuf: u32 = 0;
    var bits: u5 = 0;

    const refill = struct {
        fn rf(bb: *u32, bc: *u5, src: []const u8, pp: *usize) void {
            if (bc.* <= 8 and pp.* < src.len) {
                bb.* |= @as(u32, src[pp.*]) << bc.*;
                pp.* += 1;
                bc.* += 8;
            }
        }
    }.rf;

    const readBit = struct {
        fn rb(bb: *u32, bc: *u5, src: []const u8, pp: *usize) u1 {
            refill(bb, bc, src, pp);
            const b = @as(u1, @truncate(bb.* & 1));
            bb.* >>= 1;
            bc.* -= 1;
            return b;
        }
    }.rb;

    const readBits = struct {
        fn rbs(bb: *u32, bc: *u5, n: u5, src: []const u8, pp: *usize) u32 {
            refill(bb, bc, src, pp);
            const mask = (@as(u32, 1) << n) - 1;
            const v = bb.* & mask;
            bb.* >>= n;
            bc.* -= n;
            return v;
        }
    }.rbs;

    const readBitsBig = struct {
        fn rbb(bb: *u32, bc: *u5, n: u5, src: []const u8, pp: *usize) u32 {
            var val: u32 = 0;
            var shift: u5 = 0;
            var rem = n;
            while (rem > 0) {
                refill(bb, bc, src, pp);
                const take = @min(rem, bc.*);
                val |= (bb.* & ((@as(u32, 1) << take) - 1)) << shift;
                shift += take;
                bb.* >>= take;
                bc.* -= take;
                rem -= take;
            }
            return val;
        }
    }.rbb;

    const resetTree = struct {
        fn reset(t: []i32, tl: *usize) void {
            @memset(t[0..@min(tl.* * 2, t.len)], 0);
            tl.* = 1;
        }
    }.reset;

    const insertSym = struct {
        fn ins(t: []i32, tl: *usize, code: u16, code_len: u5, sym: u16) void {
            var node: usize = 0;
            var rem = code_len;
            const c = code;
            while (rem > 0) : (rem -= 1) {
                const bit: usize = @intCast((c >> (rem - 1)) & 1);
                const slot = node * 2 + bit;
                if (slot >= t.len) @compileError("tree overflow");
                if (rem == 1) {
                    t[slot] = @as(i32, @intCast(sym)) + 1;
                } else {
                    if (t[slot] == 0) {
                        if (tl.* * 2 + 1 >= t.len) @compileError("tree overflow");
                        t[slot] = -@as(i32, @intCast(tl.*));
                        tl.* += 1;
                    }
                    const raw = t[slot];
                    node = @as(usize, @intCast(-raw));
                }
            }
        }
    }.ins;

    const buildTree = struct {
        fn bt(t: []i32, tl: *usize, lens: []const u8, count: usize) void {
            resetTree(t, tl);

            var max_len: u5 = 0;
            for (lens[0..count]) |l| {
                if (l > max_len) max_len = l;
            }
            if (max_len == 0) return;

            var bl_count: [16]u16 = @splat(0);
            for (lens[0..count]) |l| {
                if (l > 0) bl_count[l] += 1;
            }

            var code: u16 = 0;
            var next_code: [16]u16 = @splat(0);
            var len: usize = 1;
            while (len <= max_len) : (len += 1) {
                code = (code + bl_count[len - 1]) << 1;
                next_code[len] = code;
            }

            for (lens[0..count], 0..) |l, sym| {
                if (l > 0) {
                    const c = next_code[l];
                    next_code[l] += 1;
                    insertSym(t, tl, c, l, @intCast(sym));
                }
            }
        }
    }.bt;

    const decodeSym = struct {
        fn ds(t: []i32, bb: *u32, bc: *u5, src: []const u8, pp: *usize) u16 {
            var node: usize = 0;
            while (true) {
                refill(bb, bc, src, pp);
                const bit: usize = bb.* & 1;
                bb.* >>= 1;
                bc.* -= 1;
                const slot = node * 2 + bit;
                const raw = t[slot];
                if (raw == 0) @compileError("bad Huffman code");
                if (raw > 0) return @intCast(raw - 1);
                node = @as(usize, @intCast(-raw));
            }
        }
    }.ds;

    const len_base = [_]u16{ 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
    const len_extra = [_]u5{ 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
    const dist_base = [_]u16{ 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
    const dist_extra = [_]u5{ 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

    var fixed_lit: [288]u8 = @splat(0);
    @memset(fixed_lit[0..144], 8);
    @memset(fixed_lit[144..256], 9);
    @memset(fixed_lit[256..280], 7);
    @memset(fixed_lit[280..288], 8);

    var fixed_dist: [32]u8 = @splat(5);

    var lit_tree: [2048]i32 = @splat(0);
    var lit_tree_len: usize = 1;
    var dist_tree: [2048]i32 = @splat(0);
    var dist_tree_len: usize = 1;

    while (true) {
        const bfinal = readBit(&bitbuf, &bits, data, &pos);
        const btype = readBits(&bitbuf, &bits, 2, data, &pos);

        if (btype == 0) {
            pos -= bits / 8;
            bits = 0;
            bitbuf = 0;
            const len = @as(usize, data[pos]) | (@as(usize, data[pos + 1]) << 8);
            pos += 2;
            pos += 2;
            @memcpy(out[out_pos..][0..len], data[pos..][0..len]);
            pos += len;
            out_pos += len;
        } else if (btype == 1 or btype == 2) {
            var lit_lens: [288]u8 = @splat(0);
            var dist_lens: [32]u8 = @splat(0);

            if (btype == 1) {
                @memcpy(&lit_lens, &fixed_lit);
                @memcpy(&dist_lens, &fixed_dist);
            } else {
                const hlit = readBitsBig(&bitbuf, &bits, 5, data, &pos) + 257;
                const hdist = readBitsBig(&bitbuf, &bits, 5, data, &pos) + 1;
                const hclen = readBitsBig(&bitbuf, &bits, 4, data, &pos) + 4;

                const cl_order = [_]u8{ 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
                var cl_lens: [19]u8 = @splat(0);
                var i: usize = 0;
                while (i < hclen) : (i += 1) {
                    cl_lens[cl_order[i]] = @truncate(readBitsBig(&bitbuf, &bits, 3, data, &pos));
                }

                buildTree(&tree, &tree_len, &cl_lens, 19);

                var all: [320]u8 = @splat(0);
                var n: usize = 0;
                const total = hlit + hdist;
                while (n < total) {
                    const sym = decodeSym(&tree, &bitbuf, &bits, data, &pos);
                    if (sym < 16) {
                        all[n] = @truncate(sym);
                        n += 1;
                    } else if (sym == 16) {
                        const repeat = readBitsBig(&bitbuf, &bits, 2, data, &pos) + 3;
                        var j: usize = 0;
                        while (j < repeat) : (j += 1) {
                            all[n + j] = all[n - 1];
                        }
                        n += repeat;
                    } else if (sym == 17) {
                        n += readBitsBig(&bitbuf, &bits, 3, data, &pos) + 3;
                    } else if (sym == 18) {
                        n += readBitsBig(&bitbuf, &bits, 7, data, &pos) + 11;
                    }
                }
                @memcpy(lit_lens[0..hlit], all[0..hlit]);
                @memcpy(dist_lens[0..hdist], all[hlit..][0..hdist]);
            }

            buildTree(&lit_tree, &lit_tree_len, &lit_lens, 288);
            buildTree(&dist_tree, &dist_tree_len, &dist_lens, 32);

            while (true) {
                const sym = decodeSym(&lit_tree, &bitbuf, &bits, data, &pos);
                if (sym < 256) {
                    out[out_pos] = @truncate(sym);
                    out_pos += 1;
                } else if (sym == 256) {
                    break;
                } else {
                    const idx = sym - 257;
                    const length = len_base[idx] + readBitsBig(&bitbuf, &bits, len_extra[idx], data, &pos);
                    const dist_sym = decodeSym(&dist_tree, &bitbuf, &bits, data, &pos);
                    const dist = dist_base[dist_sym] + readBitsBig(&bitbuf, &bits, dist_extra[dist_sym], data, &pos);
                    var j: usize = 0;
                    while (j < length) : (j += 1) {
                        out[out_pos] = out[out_pos - dist];
                        out_pos += 1;
                    }
                }
            }
        } else {
            @compileError("invalid deflate block type 3");
        }

        if (bfinal == 1) break;
    }

    return out[0..out_pos];
}

test "fromPNG decodes a 2x2 paletted PNG" {
    const png = [_]u8{
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x03, 0x00, 0x00, 0x00, 0x45, 0x68, 0xfd,
        0x16, 0x00, 0x00, 0x00, 0x06, 0x50, 0x4c, 0x54, 0x45, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xa5,
        0xd9, 0x9f, 0xdd, 0x00, 0x00, 0x00, 0x0c, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0x60, 0x60,
        0x04, 0x42, 0x00, 0x00, 0x0c, 0x00, 0x03, 0x2b, 0x63, 0xcb, 0x50, 0x00, 0x00, 0x00, 0x00, 0x49,
        0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
    };
    const sprite = comptime fromPNG(&png, 4);
    try std.testing.expectEqualSlices(u8, &[_]u8{ 0, 1, 1, 0 }, sprite.slice());
}

// generated 8×8 paletted PNG, pixels = 0..63 row-major
const test_png_8x8 = [_]u8{
    0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,
    0x00,0x00,0x00,0x08,0x00,0x00,0x00,0x08,0x08,0x03,0x00,0x00,0x00,0xf3,0xd1,0x4e,
    0xb9,0x00,0x00,0x00,0xc0,0x50,0x4c,0x54,0x45,0x00,0x00,0x00,0x04,0x04,0x04,0x08,
    0x08,0x08,0x0c,0x0c,0x0c,0x10,0x10,0x10,0x14,0x14,0x14,0x18,0x18,0x18,0x1c,0x1c,
    0x1c,0x20,0x20,0x20,0x24,0x24,0x24,0x28,0x28,0x28,0x2c,0x2c,0x2c,0x30,0x30,0x30,
    0x34,0x34,0x34,0x38,0x38,0x38,0x3c,0x3c,0x3c,0x40,0x40,0x40,0x44,0x44,0x44,0x48,
    0x48,0x48,0x4c,0x4c,0x4c,0x50,0x50,0x50,0x54,0x54,0x54,0x58,0x58,0x58,0x5c,0x5c,
    0x5c,0x60,0x60,0x60,0x64,0x64,0x64,0x68,0x68,0x68,0x6c,0x6c,0x6c,0x70,0x70,0x70,
    0x74,0x74,0x74,0x78,0x78,0x78,0x7c,0x7c,0x7c,0x80,0x80,0x80,0x84,0x84,0x84,0x88,
    0x88,0x88,0x8c,0x8c,0x8c,0x90,0x90,0x90,0x94,0x94,0x94,0x98,0x98,0x98,0x9c,0x9c,
    0x9c,0xa0,0xa0,0xa0,0xa4,0xa4,0xa4,0xa8,0xa8,0xa8,0xac,0xac,0xac,0xb0,0xb0,0xb0,
    0xb4,0xb4,0xb4,0xb8,0xb8,0xb8,0xbc,0xbc,0xbc,0xc0,0xc0,0xc0,0xc4,0xc4,0xc4,0xc8,
    0xc8,0xc8,0xcc,0xcc,0xcc,0xd0,0xd0,0xd0,0xd4,0xd4,0xd4,0xd8,0xd8,0xd8,0xdc,0xdc,
    0xdc,0xe0,0xe0,0xe0,0xe4,0xe4,0xe4,0xe8,0xe8,0xe8,0xec,0xec,0xec,0xf0,0xf0,0xf0,
    0xf4,0xf4,0xf4,0xf8,0xf8,0xf8,0xfc,0xfc,0xfc,0x8d,0x4c,0xd9,0x99,0x00,0x00,0x00,
    0x50,0x49,0x44,0x41,0x54,0x78,0x9c,0x63,0x60,0x60,0x64,0x62,0x66,0x61,0x65,0x63,
    0x67,0xe0,0xe0,0xe4,0xe2,0xe6,0xe1,0xe5,0xe3,0x67,0x10,0x10,0x14,0x12,0x16,0x11,
    0x15,0x13,0x67,0x90,0x90,0x94,0x92,0x96,0x91,0x95,0x93,0x67,0x50,0x50,0x54,0x52,
    0x56,0x51,0x55,0x53,0x67,0xd0,0xd0,0xd4,0xd2,0xd6,0xd1,0xd5,0xd3,0x67,0x30,0x30,
    0x34,0x32,0x36,0x31,0x35,0x33,0x67,0xb0,0xb0,0xb4,0xb2,0xb6,0xb1,0xb5,0xb3,0x07,
    0x00,0xbb,0xf8,0x07,0xe1,0x64,0x72,0xa3,0xf6,0x00,0x00,0x00,0x00,0x49,0x45,0x4e,
    0x44,0xae,0x42,0x60,0x82,
};

test "fromPNG decodes an 8x8 paletted PNG" {
    const expected: [64]u8 = expected: {
        var buf: [64]u8 = undefined;
        for (&buf, 0..) |*p, i| p.* = @intCast(i);
        break :expected buf;
    };
    const sprite = comptime fromPNG(&test_png_8x8, 64);
    try std.testing.expectEqualSlices(u8, &expected, sprite.slice());
}
