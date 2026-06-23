// =========================================================================
// vex.js
// =========================================================================

//// Part 1: Constants and framebuffer

const VEX_W = 320;
const VEX_H = 180;

const canvas = document.getElementById("screen");

// The drawing buffer stays at the fixed 320x180 framebuffer resolution; the
// displayed size is handled by CSS (see index.html), scaled up with
// nearest-neighbour filtering.
canvas.width = VEX_W;
canvas.height = VEX_H;

const ctx = canvas.getContext("2d", {
    alpha: false
});

const image = ctx.createImageData(VEX_W, VEX_H);
const pixels = image.data;

//// Part 2: Palette

const DEFAULT_PALETTE = [
0x1A1C2C,
0x5D275D,
0xB13E53,
0xEF7D57,
0xFFCD75,
0xA7F070,
0x38B764,
0x257179,
0x29366F,
0x3B5DC9,
0x41A6F6,
0x73EFF7,
0xF4F4F4,
0x94B0C2,
0x566C86,
0x333C57
];

let palette = [...DEFAULT_PALETTE];

function pal(index, rgb)
{
    palette[index & 15] = rgb >>> 0;
}

function palreset()
{
    palette = [...DEFAULT_PALETTE];
}

function unpackColor(index)
{
    const c = palette[index & 15];

    return [
        (c >> 16) & 255,
        (c >> 8) & 255,
        c & 255
    ];
}

//// Part 3: Input

const keys = {};

// Keys the cart consumes via btn(); we swallow their default actions so the
// arrows don't scroll the page (and Z/X don't trigger browser shortcuts).
const GAME_KEYS = new Set([
    "ArrowLeft",
    "ArrowRight",
    "ArrowUp",
    "ArrowDown",
    "KeyZ",
    "KeyX"
]);

window.addEventListener("keydown", e => {
    keys[e.code] = true;

    if (GAME_KEYS.has(e.code))
        e.preventDefault();
});

window.addEventListener("keyup", e => {
    keys[e.code] = false;

    if (GAME_KEYS.has(e.code))
        e.preventDefault();
});

function btn(button)
{
    switch(button)
    {
        case 0: return keys.ArrowLeft ? 1 : 0;
        case 1: return keys.ArrowRight ? 1 : 0;
        case 2: return keys.ArrowUp ? 1 : 0;
        case 3: return keys.ArrowDown ? 1 : 0;
        case 4: return keys.KeyZ ? 1 : 0;
        case 5: return keys.KeyX ? 1 : 0;
    }

    return 0;
}

let mouseX = 0;
let mouseY = 0;

const mouseButtons = new Array(8).fill(false);

canvas.addEventListener("mousemove", e => {

    const r = canvas.getBoundingClientRect();

    mouseX = Math.floor(
        (e.clientX - r.left) * VEX_W / r.width
    );

    mouseY = Math.floor(
        (e.clientY - r.top) * VEX_H / r.height
    );
});

canvas.addEventListener("mousedown", e => {
    mouseButtons[e.button] = true;
});

canvas.addEventListener("mouseup", e => {
    mouseButtons[e.button] = false;
});

function mx()
{
    return mouseX;
}

function my()
{
    return mouseY;
}

function mbtn(button)
{
    return mouseButtons[button] ? 1 : 0;
}

//// Part 4: WASM state and string helpers (C string reader)

let instance = null;
let memory = null;
let mem8 = null;

function updateMemoryViews()
{
    memory = instance.exports.memory;
    mem8 = new Uint8Array(memory.buffer);
}

function readCString(ptr)
{
    updateMemoryViews();

    let s = "";

    while (ptr < mem8.length)
    {
        const c = mem8[ptr++];

        if (c === 0)
            break;

        s += String.fromCharCode(c);
    }

    return s;
}


function title(ptr)
{
    document.title = readCString(ptr);
}

//// Part 5: Core pixel routines

function pset(x, y, color)
{
    if (
        x < 0 ||
        x >= VEX_W ||
        y < 0 ||
        y >= VEX_H
    )
        return;

    const c = palette[color & 15];

    const i = (y * VEX_W + x) << 2;

    pixels[i + 0] = (c >> 16) & 255;
    pixels[i + 1] = (c >> 8) & 255;
    pixels[i + 2] = c & 255;
    pixels[i + 3] = 255;
}

function cls(color)
{
    const c = palette[color & 15];

    const r = (c >> 16) & 255;
    const g = (c >> 8) & 255;
    const b = c & 255;

    for (let i = 0; i < pixels.length; i += 4)
    {
        pixels[i] = r;
        pixels[i + 1] = g;
        pixels[i + 2] = b;
        pixels[i + 3] = 255;
    }
}

//// Part 6: Bresenham line()

// =========================================================================
// line()
// =========================================================================

function line(x0, y0, x1, y1, color)
{
    x0 |= 0;
    y0 |= 0;
    x1 |= 0;
    y1 |= 0;

    let dx = Math.abs(x1 - x0);
    let sx = x0 < x1 ? 1 : -1;

    let dy = -Math.abs(y1 - y0);
    let sy = y0 < y1 ? 1 : -1;

    let err = dx + dy;

    while (true)
    {
        pset(x0, y0, color);

        if (x0 === x1 && y0 === y1)
            break;

        let e2 = err << 1;

        if (e2 >= dy)
        {
            err += dy;
            x0 += sx;
        }

        if (e2 <= dx)
        {
            err += dx;
            y0 += sy;
        }
    }
}

// =========================================================================
// rect()
// =========================================================================

function rect(x, y, w, h, color)
{
    if (w <= 0 || h <= 0)
        return;

    let x0 = Math.max(0, x);
    let y0 = Math.max(0, y);

    let x1 = Math.min(VEX_W, x + w);
    let y1 = Math.min(VEX_H, y + h);

    const c = palette[color & 15];

    const r = (c >> 16) & 255;
    const g = (c >> 8) & 255;
    const b = c & 255;

    for (let yy = y0; yy < y1; yy++)
    {
        let i = ((yy * VEX_W) + x0) << 2;

        for (let xx = x0; xx < x1; xx++)
        {
            pixels[i] = r;
            pixels[i + 1] = g;
            pixels[i + 2] = b;
            pixels[i + 3] = 255;

            i += 4;
        }
    }
}

// =========================================================================
// rectb()
// =========================================================================

function rectb(x, y, w, h, color)
{
    if (w <= 0 || h <= 0)
        return;

    rect(x, y, w, 1, color);

    if (h > 1)
        rect(x, y + h - 1, w, 1, color);

    if (h > 2)
    {
        rect(x, y + 1, 1, h - 2, color);

        if (w > 1)
            rect(x + w - 1, y + 1, 1, h - 2, color);
    }
}

//// Part 8: Circle + Circle Outline (midpoint algorithm)

// =========================================================================
// circ()
// =========================================================================

function circ(cx, cy, r, color)
{
    cx |= 0;
    cy |= 0;
    r  |= 0;

    let x = r;
    let y = 0;
    let err = 0;

    while (x >= y)
    {
        // horizontal spans for fill (faster + consistent)
        hline(cx - x, cx + x, cy + y, color);
        hline(cx - y, cx + y, cy + x, color);
        hline(cx - x, cx + x, cy - y, color);
        hline(cx - y, cx + y, cy - x, color);

        y++;

        if (err <= 0)
        {
            err += 2 * y + 1;
        }
        else
        {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

// helper: horizontal line span (used by circ)
function hline(x0, x1, y, color)
{
    if (y < 0 || y >= VEX_H)
        return;

    if (x0 > x1)
    {
        let t = x0;
        x0 = x1;
        x1 = t;
    }

    if (x1 < 0 || x0 >= VEX_W)
        return;

    x0 = Math.max(0, x0);
    x1 = Math.min(VEX_W - 1, x1);

    const c = palette[color & 15];

    const r = (c >> 16) & 255;
    const g = (c >> 8) & 255;
    const b = c & 255;

    let i = (y * VEX_W + x0) << 2;

    for (let x = x0; x <= x1; x++)
    {
        pixels[i] = r;
        pixels[i + 1] = g;
        pixels[i + 2] = b;
        pixels[i + 3] = 255;
        i += 4;
    }
}

// =========================================================================
// circb()
// =========================================================================

function circb(cx, cy, r, color)
{
    cx |= 0;
    cy |= 0;
    r  |= 0;

    let x = r;
    let y = 0;
    let err = 0;

    while (x >= y)
    {
        pset(cx + x, cy + y, color);
        pset(cx + y, cy + x, color);
        pset(cx - y, cy + x, color);
        pset(cx - x, cy + y, color);
        pset(cx - x, cy - y, color);
        pset(cx - y, cy - x, color);
        pset(cx + y, cy - x, color);
        pset(cx + x, cy - y, color);

        y++;

        if (err <= 0)
        {
            err += 2 * y + 1;
        }
        else
        {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

//// Part 9: blit() (bitmap from cart memory)

// =========================================================================
// blit()
// =========================================================================

function blit(ptr, x, y, w, h, key)
{
    x |= 0;
    y |= 0;
    w |= 0;
    h |= 0;

    if (w <= 0 || h <= 0)
        return;

    updateMemoryViews();

    for (let row = 0; row < h; row++)
    {
        for (let col = 0; col < w; col++)
        {
            const c = mem8[ptr + row * w + col];

            // Out-of-bounds reads (undefined) and key pixels are skipped.
            if (c === undefined || c === key)
                continue;

            pset(x + col, y + row, c);
        }
    }
}

//// Part 10: Triangle fill (tri) + outline (trib)

function tri(x1,y1,x2,y2,x3,y3,color)
{
    x1 |= 0; y1 |= 0;
    x2 |= 0; y2 |= 0;
    x3 |= 0; y3 |= 0;

    // For each scanline, track the leftmost and rightmost x where any edge
    // crosses it. A triangle is convex, so [min, max] is exactly the span to
    // fill — independent of vertex order or winding.
    const rows = new Map();

    function addEdge(ax, ay, bx, by)
    {
        if (ay === by)
            return; // horizontal edge: its endpoints are covered by the others

        const slope = (bx - ax) / (by - ay);

        const yStart = Math.min(ay, by);
        const yEnd   = Math.max(ay, by);

        for (let y = yStart; y <= yEnd; y++)
        {
            const x = ax + (y - ay) * slope;
            const row = rows.get(y);

            if (row === undefined)
                rows.set(y, { l: x, r: x });
            else
            {
                if (x < row.l) row.l = x;
                if (x > row.r) row.r = x;
            }
        }
    }

    addEdge(x1, y1, x2, y2);
    addEdge(x2, y2, x3, y3);
    addEdge(x3, y3, x1, y1);

    for (const [y, v] of rows)
    {
        hline(
            Math.round(v.l),
            Math.round(v.r),
            y,
            color
        );
    }
}

function trib(x1,y1,x2,y2,x3,y3,color)
{
    line(x1,y1,x2,y2,color);
    line(x2,y2,x3,y3,color);
    line(x3,y3,x1,y1,color);
}

//// Part 11: Bitmap font + text()

const FONT8 = [
0x0000000000000000n, // 32  space
0x1010101010001000n, // 33  !
0x2828000000000000n, // 34  "
0x28287C287C282800n, // 35  #
0x103C5038147C1000n, // 36  $
0x6094681629063000n, // 37  %
0x20505028105A2400n, // 38  &
0x1010000000000000n, // 39  '
0x1020404040201000n, // 40  (
0x1008040404081000n, // 41  )
0x0008143C14080000n, // 42  *
0x0010107C10100000n, // 43  +
0x0000000000101020n, // 44  ,
0x0000007C00000000n, // 45  -
0x0000000000100000n, // 46  .
0x0204081020408000n, // 47  /

0x3C66666E76663C00n, // 48  0
0x1030501010107C00n, // 49  1
0x3C66061C30607E00n, // 50  2
0x3C66061C06663C00n, // 51  3
0x0C1C2C4C7E0C0C00n, // 52  4
0x7E607C0606663C00n, // 53  5
0x3C66607C66663C00n, // 54  6
0x7E060C1830303000n, // 55  7
0x3C66663C66663C00n, // 56  8
0x3C66663E06663C00n, // 57  9

0x0000100000100000n, // 58  :
0x0000100000101020n, // 59  ;
0x0C18306030180C00n, // 60  <
0x00007C007C000000n, // 61  =
0x6030180C18306000n, // 62  >
0x3C66060C10001000n, // 63  ?
0x3C666E6E6E603C00n, // 64  @

0x183C66667E666600n, // 65  A
0x7C66667C66667C00n, // 66  B
0x3C66606060663C00n, // 67  C
0x786C6666666C7800n, // 68  D
0x7E60607860607E00n, // 69  E
0x7E60607860606000n, // 70  F
0x3C66606E66663C00n, // 71  G
0x6666667E66666600n, // 72  H
0x3C18181818183C00n, // 73  I
0x1E0C0C0C6C6C3800n, // 74  J
0x666C7878786C6600n, // 75  K
0x6060606060607E00n, // 76  L
0x63777F6B63636300n, // 77  M
0x66767E7E6E666600n, // 78  N
0x3C66666666663C00n, // 79  O
0x7C66667C60606000n, // 80  P
0x3C6666666A6C3A00n, // 81  Q
0x7C66667C786C6600n, // 82  R
0x3C66603C06663C00n, // 83  S
0x7E18181818181800n, // 84  T
0x6666666666663C00n, // 85  U
0x66666666663C1800n, // 86  V
0x6363636B7F776300n, // 87  W
0x66663C3C66666600n, // 88  X
0x6666663C18181800n, // 89  Y
0x7E060C1830607E00n, // 90  Z

0x3C30303030303C00n, // 91  [
0x8040201008040200n, // 92  \
0x3C0C0C0C0C0C3C00n, // 93  ]
0x10386C0000000000n, // 94  ^
0x000000000000007Fn, // 95  _
0x2010080000000000n, // 96  `

0x00003C063E663E00n, // 97  a
0x60607C6666667C00n, // 98  b
0x00003C6660663C00n, // 99  c
0x06063E6666663E00n, // 100 d
0x00003C667E603C00n, // 101 e
0x1C30307830303000n, // 102 f
0x00003E66663E063Cn, // 103 g
0x60607C6666666600n, // 104 h
0x1800181818183C00n, // 105 i
0x060006060666663Cn, // 106 j
0x6060666C786C6600n, // 107 k
0x1818181818181800n, // 108 l
0x0000667F7F6B6300n, // 109 m
0x00007C6666666600n, // 110 n
0x00003C6666663C00n, // 111 o
0x00007C66667C6060n, // 112 p
0x00003E66663E0606n, // 113 q
0x00006C7660606000n, // 114 r
0x00003E603C067C00n, // 115 s
0x30307C3030301C00n, // 116 t
0x0000666666663E00n, // 117 u
0x00006666663C1800n, // 118 v
0x0000636B7F7F3600n, // 119 w
0x0000663C183C6600n, // 120 x
0x00006666663E0C38n, // 121 y
0x00007E0C18307E00n, // 122 z

0x0E18183018180E00n, // 123 {
0x1818180018181800n, // 124 |
0x7018180C18187000n, // 125 }
0x0000000000000000n, // 126 ~
0x0000000000000000n  // 127 DEL
];

// FONT8 holds each 8x8 glyph as a 64-bit BigInt: the most-significant byte is
// the top row, the most-significant bit of each byte is the left pixel. JS
// numbers can't hold 64-bit ints exactly, so we unpack the table once into a
// flat byte-per-row array and render from that with plain number math.
const FONT_FIRST = 32; // FONT8[0] is the glyph for char code 32 (space)

const FONT_ROWS = new Uint8Array(FONT8.length * 8);

for (let i = 0; i < FONT8.length; i++)
{
    const glyph = FONT8[i];

    for (let row = 0; row < 8; row++)
        FONT_ROWS[i * 8 + row] =
            Number((glyph >> BigInt((7 - row) * 8)) & 0xFFn);
}

function fontPixel(x, y, color)
{
    pset(x, y, color);
}

function text(ptr, x, y, color)
{
    x |= 0;
    y |= 0;

    const str = readCString(ptr);

    for (let i = 0; i < str.length; i++)
    {
        const c = str.charCodeAt(i) - FONT_FIRST;

        // skip unsupported chars
        if (c < 0 || c >= FONT8.length)
        {
            x += 8;
            continue;
        }

        const base = c * 8;

        for (let yy = 0; yy < 8; yy++)
        {
            const rowBits = FONT_ROWS[base + yy];

            for (let xx = 0; xx < 8; xx++)
            {
                // extract bit (MSB-first per row)
                const bit = (rowBits >> (7 - xx)) & 1;

                if (bit)
                    fontPixel(x + xx, y + yy, color);
            }
        }

        x += 8;
    }
}

//// Part 12: WASM host, env imports, loader, main loop

const env =
{
    cls,
    pset,
    rect,
    rectb,
    circ,
    circb,
    line,
    tri,
    trib,
    blit,

    text,
    title,

    btn,
    mx,
    my,
    mbtn,

    pal,
    palreset
};

let rafId = null;

async function instantiateCart(bytes)
{
    const wasm = await WebAssembly.instantiate(bytes, {
        env
    });

    if (!wasm.instance.exports.update)
        throw new Error("cart has no update() export");

    instance = wasm.instance;

    updateMemoryViews();

    // Start each cart from a clean palette and framebuffer *before* boot(), so
    // any pal() overrides boot() makes survive into the main loop (matches the
    // native host order: reset_palette() then boot()).
    palreset();
    clear();

    if (instance.exports.boot)
        instance.exports.boot();
}

async function loadCart(url)
{
    const res = await fetch(url);

    if (!res.ok)
        throw new Error("failed to load cart");

    await instantiateCart(await res.arrayBuffer());
}

function present()
{
    ctx.putImageData(image, 0, 0);
}

function frame()
{
    // run cart logic
    instance.exports.update();

    // push framebuffer to canvas
    present();

    rafId = requestAnimationFrame(frame);
}

function clear()
{
    pixels.fill(0);
}

// (Re)start the main loop with a freshly loaded cart, cancelling any loop
// already running so reloads don't stack.
function run()
{
    if (rafId !== null)
        cancelAnimationFrame(rafId);

    // palette + framebuffer were already reset before boot() in
    // instantiateCart(); resetting here would clobber boot()'s pal() overrides.
    rafId = requestAnimationFrame(frame);
}

export async function start(cartPath)
{
    await loadCart(cartPath);

    run();
}

// Load a cart from raw bytes (e.g. a dropped .wasm file).
export async function startBytes(bytes)
{
    await instantiateCart(bytes);

    run();
}

//// Part 13: Drag-and-drop cart loading

// Highlight the canvas while a file is dragged over the page, so it reads as a
// drop target.
function setDropCue(on)
{
    if (on)
    {
        canvas.style.outline = "4px dashed #41A6F6";
        canvas.style.outlineOffset = "4px";
        canvas.style.opacity = "0.6";
    }
    else
    {
        canvas.style.outline = "";
        canvas.style.outlineOffset = "";
        canvas.style.opacity = "";
    }
}

// Drop a .wasm file anywhere on the page to load it in place of the current
// cart. Without preventDefault the browser would just navigate to the file.
window.addEventListener("dragover", e => {
    e.preventDefault();
    e.dataTransfer.dropEffect = "copy";
    setDropCue(true);
});

// dragleave fires when crossing into child elements too; only clear the cue
// when the drag actually leaves the window (no element being entered).
window.addEventListener("dragleave", e => {
    if (e.relatedTarget === null)
        setDropCue(false);
});

window.addEventListener("drop", async e => {
    e.preventDefault();
    setDropCue(false);

    const file = e.dataTransfer?.files?.[0];

    if (!file)
        return;

    if (!file.name.endsWith(".wasm"))
    {
        console.warn(`vex: ignoring non-wasm file "${file.name}"`);
        return;
    }

    try
    {
        await startBytes(await file.arrayBuffer());
        document.title = file.name;
    }
    catch (err)
    {
        console.error(`vex: failed to load "${file.name}":`, err);
    }
});
