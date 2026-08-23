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

//// Part 3: Input

const keys = {};

// Keys btn() reads; swallow their default actions so the arrows don't scroll
// the page and Z/X don't trigger browser shortcuts.
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

    syncPadButtonFromKey(e.code);
});

window.addEventListener("keyup", e => {
    keys[e.code] = false;

    if (GAME_KEYS.has(e.code))
        e.preventDefault();

    syncPadButtonFromKey(e.code);
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

function btnp(button)
{
    const held = btn(button);
    const prev = (prevButtons >> button) & 1;

    return held && !prev ? 1 : 0;
}

let mouseX = 0;
let mouseY = 0;

const mouseButtons = new Array(8).fill(false);

// DOM e.button: 0=left, 1=middle, 2=right.
// vex convention: 0=left, 1=right, 2=middle (matches raylib, ebitengine).
const DOM_TO_VEX = [0, 2, 1];

// Pointer events handle mouse, touch and pen uniformly; touch-action: none
// (see index.html) stops the browser hijacking touch for scroll/pinch.
function setMouse(e)
{
    const r = canvas.getBoundingClientRect();

    // Clamped to the framebuffer (0..W-1 / 0..H-1), matching the native
    // hosts and the documented API range.
    mouseX = Math.max(0, Math.min(VEX_W - 1, Math.floor(
        (e.clientX - r.left) * VEX_W / r.width
    )));

    mouseY = Math.max(0, Math.min(VEX_H - 1, Math.floor(
        (e.clientY - r.top) * VEX_H / r.height
    )));
}

// Each button tracks the pointers holding it, so lifting one finger mid
// multi-touch doesn't release a button another finger still holds.
const heldPointers = new Map();

// Touch maps onto buttons by activation order: finger 1 moves the pointer,
// finger 2 presses left (0), finger 3 right (1); each keeps its assignment
// until it lifts. pointerId -> button index (or -1 for none).
const touchButtons = new Map();

function setButton(e, down)
{
    let button;

    if (e.pointerType === "touch")
    {
        if (down)
        {
            // index counts from those already down, so finger 2 -> left (0),
            // finger 3 -> right (1); extra fingers just move the pointer.
            const index = touchButtons.size;
            touchButtons.set(e.pointerId, index - 1);
        }

        button = touchButtons.get(e.pointerId);

        if (!down)
            touchButtons.delete(e.pointerId);

        // No button assigned: position-only or extra finger.
        if (button < 0 || button > 1)
            return;
    }
    else
    {
        button = DOM_TO_VEX[e.button] ?? e.button;
    }

    const pointers = heldPointers.get(button) ?? new Set();

    if (down)
    {
        pointers.add(e.pointerId);
        heldPointers.set(button, pointers);
    }
    else
    {
        pointers.delete(e.pointerId);
        if (pointers.size === 0)
            heldPointers.delete(button);
    }

    mouseButtons[button] = heldPointers.has(button);
}

// Only the first finger to land moves mouseX/mouseY; a second finger must
// not jump the cursor to itself. Mouse/pen always update the position, so
// this only gates touch moves.
let positionPointer = null;

canvas.addEventListener("pointerdown", e => {
    e.preventDefault();

    if (e.pointerType !== "touch" || positionPointer === null)
    {
        positionPointer = e.pointerId;
        setMouse(e);
    }

    setButton(e, true);
    // Keep receiving pointermove/pointerup even when the finger or pen
    // slides off the canvas mid-drag.
    try { canvas.setPointerCapture(e.pointerId); } catch (_) {}
});

canvas.addEventListener("pointermove", e => {
    if (e.pointerType !== "touch" || e.pointerId === positionPointer)
        setMouse(e);
});

function releasePointer(e)
{
    setButton(e, false);

    // Hand the mouse position to the next active touch pointer when the
    // owning finger lifts.
    if (e.pointerId === positionPointer)
    {
        positionPointer = null;

        for (const id of touchButtons.keys())
        {
            positionPointer = id;
            break;
        }
    }
}

canvas.addEventListener("pointerup", releasePointer);
canvas.addEventListener("pointercancel", releasePointer);

// Prevent the browser context menu so right-click reaches the cart.
canvas.addEventListener("contextmenu", e => e.preventDefault());

//// Part 3b: Virtual gamepad (portrait mode)

/*
 * Portrait mode renders a touch gamepad below the canvas: a d-pad (cart
 * buttons 0..3) plus Z and X (buttons 4, 5). Presses flip the same `keys`
 * entries the keyboard would, so btn()/btnp() work unchanged. A button
 * stays active while touched OR the matching physical key is held. Each
 * button tracks its own touch state, so multi-touch works; setPointerCapture
 * keeps a press alive when the finger slides off, and pointercancel covers
 * OS-interrupted touches.
 */
const PAD_BINDINGS = [
    [".dpad-up",    "ArrowUp"],
    [".dpad-down",  "ArrowDown"],
    [".dpad-left",  "ArrowLeft"],
    [".dpad-right", "ArrowRight"],
    [".dpad-z",     "KeyZ"],   // Z in the centre of the d-pad -> cart button 4
    [".btn-x",      "KeyX"]    // X in the bottom-right corner -> cart button 5
];

// Keyboard code -> gamepad button, so the global keydown/keyup handlers can
// highlight the matching button. Populated by setupGamepad().
const padButtonByCode = new Map();

function bindPadButton(button, code)
{
    // Either source (touch or key) being true marks the button active; OR
    // them on every change.
    let touchHeld = false;
    let keyHeld = false;

    const sync = () => {
        const active = touchHeld || keyHeld;
        button.classList.toggle("active", active);
        keys[code] = active ? true : false;
    };

    button.addEventListener("pointerdown", e => {
        // Prevent the synthetic mouse events that follow a touch from
        // also triggering drags / focus shifts on the page.
        e.preventDefault();
        touchHeld = true;
        // Track the press so pointerup outside the button still releases it.
        try { button.setPointerCapture(e.pointerId); } catch (_) {}
        sync();
    });

    const releaseTouch = () => {
        if (touchHeld)
        {
            touchHeld = false;
            sync();
        }
    };

    button.addEventListener("pointerup",     releaseTouch);
    button.addEventListener("pointercancel", releaseTouch);
    // pointerleave releases when the finger slides off without lifting
    // (setPointerCapture normally handles this, but some browsers don't
    // dispatch pointerup for cancelled captures).
    button.addEventListener("pointerleave",  releaseTouch);

    // Accessibility: Space/Enter on a focused button also acts as a press.
    button.addEventListener("keydown", e => {
        if (e.code === "Space" || e.code === "Enter")
        {
            e.preventDefault();
            touchHeld = true;
            sync();
        }
    });

    button.addEventListener("keyup", e => {
        if (e.code === "Space" || e.code === "Enter")
        {
            releaseTouch();
        }
    });

    // Let the global keydown/keyup handlers highlight the button for the
    // matching physical key.
    padButtonByCode.set(code, { setKeyHeld(v) { keyHeld = v; sync(); } });
}
export function setupGamepad()
{
    for (const [selector, code] of PAD_BINDINGS)
    {
        const button = document.querySelector(`#gamepad ${selector}`);

        if (button)
            bindPadButton(button, code);
    }
}

function syncPadButtonFromKey(code)
{
    // Called from the global keydown/keyup handlers so the on-screen button
    // highlights when the user presses a physical key (arrow / Z / X).
    const entry = padButtonByCode.get(code);

    if (entry)
        entry.setKeyHeld(!!keys[code]);
}

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

//// Part 3c: Audio (beep)

// One shared AudioContext, created inside the first user gesture: browsers
// start a lazily-created context suspended, and iOS only starts one created
// (not just resumed) inside a gesture handler -- creating it from the game
// loop left it suspended and audio only came up on the second tap. Until the
// first gesture beeps are dropped (like the C host's lost opening note)
// rather than queued on a frozen timeline where they would fire together.
let audioCtx = null;

// The currently-playing oscillator and its gain node, so a fresh beep() can
// stop the previous one and start a new one at currentTime. Without this,
// the Web Audio API happily lets multiple oscillators overlap and the cart
// would stack blips on top of each other in amplitude; with it, beep() is
// monophonic and matches the C and Go hosts. The pointer is cleared on
// onended so a beep() that fires after the previous blip has already
// finished doesn't try to stop an already-stopped oscillator.
let activeOsc = null;
let activeGain = null;

function unlockAudio()
{
    if (
        typeof AudioContext === "undefined" &&
        typeof webkitAudioContext === "undefined"
    )
        return;

    if (!audioCtx) {
        audioCtx = new (window.AudioContext || window.webkitAudioContext)();

        // A one-sample silent buffer within the gesture makes iOS start the
        // audio pipeline for real; otherwise it can defer actual rendering,
        // and the first blips scheduled after resume() fire together.
        const buf = audioCtx.createBuffer(1, 1, audioCtx.sampleRate);
        const src = audioCtx.createBufferSource();
        src.buffer = buf;
        src.connect(audioCtx.destination);
        src.start(0);
    }

    if (audioCtx.state === "suspended")
        audioCtx.resume();
}

// touchstart/pointerdown/mousedown/keydown cover every unlock gesture across
// iOS Safari (older iOS only honoured touch events) and desktop browsers.
// resume() on a running context is a no-op, so keeping the listeners
// attached also recovers from a later re-suspend.
window.addEventListener("touchstart", unlockAudio);
window.addEventListener("pointerdown", unlockAudio);
window.addEventListener("mousedown", unlockAudio);
window.addEventListener("keydown", unlockAudio);

function beep(freq)
{
    if (!audioCtx || audioCtx.state !== "running")
        return; // not unlocked yet: dropping matches the C host's lost first note

    const start = audioCtx.currentTime;

    // Cut the previous blip off, if any. start <= currentTime here, so the
    // stop() takes effect immediately; if the oscillator already finished on
    // its own, onended has nulled activeOsc and stop() is a no-op (but it
    // would throw InvalidStateError on a node that's been disconnected, so
    // the try/catch also covers the case where a stray stop() races with
    // garbage collection of the previous node).
    if (activeOsc) {
        try { activeOsc.stop(start); } catch (e) { /* already stopped */ }
        activeOsc = null;
    }
    if (activeGain) {
        activeGain.disconnect();
        activeGain = null;
    }

    const osc = audioCtx.createOscillator();
    const gain = audioCtx.createGain();

    osc.type = "square";

    // Clamp so a bogus cart value can't drive the oscillator into NaN territory.
    osc.frequency.value = Math.max(1, Math.min(20000, freq));

    // Full-amplitude 100ms square, matching the C host's ±8000 f32 wave and
    // the Go host's ±8000 s16 wave (8000/32768 ≈ 0.24 of full scale). No decay
    // envelope: the other hosts hard-cut at 100ms too, and a fresh oscillator
    // starts at phase 0 so a frequency change retriggers cleanly instead of
    // clicking mid-cycle.
    gain.gain.value = 8000 / 32768;

    osc.connect(gain);
    gain.connect(audioCtx.destination);

    osc.start(start);
    osc.stop(start + 0.1);

    activeOsc = osc;
    activeGain = gain;

    // Once the blip has actually played out, drop the references so the next
    // beep() doesn't carry a stale oscillator that would need a try/catch to
    // stop safely. Comparing against the captured node keeps a freshly-scheduled
    // beep from clobbering the next blip's cleanup.
    osc.onended = () => {
        if (activeOsc === osc) activeOsc = null;
        if (activeGain === gain) activeGain = null;
    };
}

//// Part 4: WASM state and string helpers (C string reader)

let instance = null;
let memory = null;
let mem8 = null;
let prevButtons = 0;

function updateMemoryViews()
{
    memory = instance.exports.memory;
    mem8 = new Uint8Array(memory.buffer);
}

function readCString(ptr)
{
    updateMemoryViews();

    let end = ptr;
    while (end < mem8.length && mem8[end] !== 0)
        end++;

    // Chunked String.fromCharCode keeps long strings O(n) instead of the
    // O(n²) of repeated += concatenation.
    let s = "";
    for (let i = ptr; i < end; i += 4096)
        s += String.fromCharCode.apply(null, mem8.subarray(i, Math.min(i + 4096, end)));

    return s;
}


function title(ptr)
{
    document.title = readCString(ptr);
}

//// Part 5: Core pixel routines

// Write a framebuffer pixel at byte index i (4 bytes per pixel) from a
// palette color.
function fillPixel(i, c)
{
    pixels[i + 0] = (c >> 16) & 255;
    pixels[i + 1] = (c >> 8) & 255;
    pixels[i + 2] = c & 255;
    pixels[i + 3] = 255;
}

function pset(x, y, color)
{
    if (
        x < 0 ||
        x >= VEX_W ||
        y < 0 ||
        y >= VEX_H
    )
        return;

    fillPixel((y * VEX_W + x) << 2, palette[color & 15]);
}

function cls(color)
{
    const c = palette[color & 15];

    // Seed one pixel, then double the filled prefix onto itself with native
    // copyWithin (4 -> 8 -> 16 ... bytes) -- ~16 memcpys instead of one
    // fillPixel call per pixel, matching the Go host's cls().
    pixels[0] = (c >> 16) & 255;
    pixels[1] = (c >> 8) & 255;
    pixels[2] = c & 255;
    pixels[3] = 255;

    let n = 4;
    while (n < pixels.length)
    {
        const m = Math.min(n, pixels.length - n);
        pixels.copyWithin(n, 0, m);
        n += m;
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

    for (let yy = y0; yy < y1; yy++)
    {
        let i = ((yy * VEX_W) + x0) << 2;

        for (let xx = x0; xx < x1; xx++)
        {
            fillPixel(i, c);
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

    // Radius clamp matching the native hosts: bounds the midpoint loop so a
    // bogus cart value can't spin millions of iterations.
    const R_MAX = VEX_W * 16;
    if (r < 0) r = 0;
    if (r > R_MAX) r = R_MAX;

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

    let i = (y * VEX_W + x0) << 2;

    for (let x = x0; x <= x1; x++)
    {
        fillPixel(i, c);
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

    // Radius clamp matching the native hosts (see circ).
    const R_MAX = VEX_W * 16;
    if (r < 0) r = 0;
    if (r > R_MAX) r = R_MAX;

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

    // Refresh the memory view BEFORE the bounds check: a cart that grew its
    // linear memory detaches the old buffer (length 0), and checking a stale
    // view would silently skip every blit.
    updateMemoryViews();

    if (w <= 0 || h <= 0)
        return;

    if (ptr < 0 || ptr + w * h > mem8.length)
        return;

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

// Reusable scanline edge buffers, grown on demand -- avoids allocating two
// arrays per tri() call.
const TRI_INF = 1 << 30;
const TRI_MAX_ROWS = 2 * (VEX_W * 16) + 1; // matches the hosts' coord bound
let triL = new Int32Array(64);
let triR = new Int32Array(64);

function tri(x1,y1,x2,y2,x3,y3,color)
{
    x1 |= 0; y1 |= 0;
    x2 |= 0; y2 |= 0;
    x3 |= 0; y3 |= 0;

    const ymin = Math.min(y1, Math.min(y2, y3));
    const ymax = Math.max(y1, Math.max(y2, y3));

    const n = ymax - ymin + 1;
    if (n <= 0 || n > TRI_MAX_ROWS)
        return; // guard hostile spans, matching the native hosts

    if (n > triL.length)
    {
        triL = new Int32Array(n);
        triR = new Int32Array(n);
    }

    for (let i = 0; i < n; i++)
    {
        triL[i] = TRI_INF;
        triR[i] = -TRI_INF;
    }

    // Per row, track the floor'd leftmost/rightmost x where any edge crosses
    // it -- byte-for-byte the tri() of the C and Go hosts, so all three fill
    // identical pixels regardless of vertex order or winding.
    function addEdge(ax, ay, bx, by)
    {
        if (ay === by)
            return; // horizontal edge: its endpoints are covered by the others

        const slope = (bx - ax) / (by - ay);

        const yStart = Math.min(ay, by);
        const yEnd   = Math.max(ay, by);

        for (let y = yStart; y <= yEnd; y++)
        {
            const xf = ax + (y - ay) * slope;
            const xi = Math.floor(xf);
            const i = y - ymin;

            if (xi < triL[i]) triL[i] = xi;
            if (xi > triR[i]) triR[i] = xi;
        }
    }

    addEdge(x1, y1, x2, y2);
    addEdge(x2, y2, x3, y3);
    addEdge(x3, y3, x1, y1);

    for (let i = 0; i < n; i++)
    {
        if (triL[i] <= triR[i])
            hline(triL[i], triR[i], ymin + i, color);
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
0x1818181818001800n, // 33  !
0x2828000000000000n, // 34  "
0x28287C287C282800n, // 35  #
0x103C5038147C1000n, // 36  $
0x6264081020460600n, // 37  %
0x304848304A443A00n, // 38  &
0x1010000000000000n, // 39  '
0x1020404040201000n, // 40  (
0x1008040404081000n, // 41  )
0x00141C3E1C140000n, // 42  *
0x0010107C10100000n, // 43  +
0x0000000000101020n, // 44  ,
0x0000007C00000000n, // 45  -
0x0000000000100000n, // 46  .
0x0204081020408000n, // 47  /

0x3C66666E76663C00n, // 48  0
0x1838181818183C00n, // 49  1
0x3C66061C30607E00n, // 50  2
0x3C66061C06663C00n, // 51  3
0x0C1C2C4C7E0C0C00n, // 52  4
0x7E607C0606663C00n, // 53  5
0x3C66607C66663C00n, // 54  6
0x7E060C1830303000n, // 55  7
0x3C66663C66663C00n, // 56  8
0x3C66663E06663C00n, // 57  9

0x0000200000200000n, // 58  :
0x0000200000202040n, // 59  ;
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
0x1818181818181E00n, // 108 l
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

// FONT8 packs each 8x8 glyph as a 64-bit BigInt (most-significant byte =
// top row, most-significant bit of each byte = left pixel). JS numbers can't
// hold 64-bit ints exactly, so we unpack the table once into a flat
// byte-per-row array and render from that with plain number math.
const FONT_FIRST = 32; // FONT8[0] is the glyph for char code 32 (space)

const FONT_ROWS = new Uint8Array(FONT8.length * 8);

for (let i = 0; i < FONT8.length; i++)
{
    const glyph = FONT8[i];

    for (let row = 0; row < 8; row++)
        FONT_ROWS[i * 8 + row] =
            Number((glyph >> BigInt((7 - row) * 8)) & 0xFFn);
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
                    pset(x + xx, y + yy, color);
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
    btnp,
    mx,
    my,
    mbtn,

    pal,
    palreset,

    beep
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

    // Reset palette + framebuffer *before* boot(), so any pal() overrides
    // boot() makes survive into the main loop (matches the native host
    // order: reset_palette() then boot()).
    palreset();
    clear();

    if (instance.exports.boot)
        instance.exports.boot();
}

async function loadCart(url)
{
    const res = await fetch(url);

    if (!res.ok)
        throw new Error(`failed to load cart (HTTP ${res.status})`);

    // Safari rejects WebAssembly.instantiate() with a fetched buffer whose
    // Content-Type wasn't application/wasm (the spec says it shouldn't
    // check). Copying into a fresh buffer breaks that tracking.
    const src = new Uint8Array(await res.arrayBuffer());
    const bytes = new Uint8Array(src.length);
    bytes.set(src);
    await instantiateCart(bytes.buffer);
}

function present()
{
    ctx.putImageData(image, 0, 0);
}

let frameGen = 0;

// One cart tick: run logic, capture button state for btnp(). The framebuffer
// is presented once per animation frame (after all catch-up ticks), not once
// per tick.
function tick(gen)
{
    // run cart logic
    instance.exports.update();

    // Capture button state for next frame's btnp().
    prevButtons = 0;

    for (let i = 0; i < 6; i++)
        if (btn(i)) prevButtons |= (1 << i);
}

// Fixed 60 TPS driven by the wall clock, not one tick per rAF: browsers
// throttle rAF in hidden tabs and deliver a catch-up burst of callbacks when
// the tab becomes visible again, which a naive per-rAF loop fast-forwards
// through (and at display refresh on 60Hz+ monitors, where the C host and
// the ebiten port both lock to 60fps). A fixed-timestep accumulator with a
// hidden-gap clamp keeps the cart at 60 TPS regardless.
const TICK_MS = 1000 / 60;
let lastFrame = null;
let acc = 0;

function frame(gen, now)
{
    if (lastFrame !== null) {
        let elapsed = now - lastFrame;
        if (elapsed > 250) elapsed = 0; // hidden tab gap: don't fast-forward
        acc += elapsed;
    }
    lastFrame = now;

    // Slow frames must not trigger a catch-up burst of ticks.
    if (acc > 5 * TICK_MS) acc = 5 * TICK_MS;

    let ran = 0;
    while (acc >= TICK_MS) {
        tick(gen);
        acc -= TICK_MS;
        ran++;
    }

    if (ran > 0)
        present(); // one putImageData per animation frame, not per tick

    if (frameGen === gen)
        rafId = requestAnimationFrame(t => frame(gen, t));
}

function clear()
{
    pixels.fill(0);
}

// (Re)start the main loop with a freshly loaded cart, cancelling any loop
// already running so reloads don't stack.
function run()
{
    const gen = ++frameGen;

    if (rafId !== null)
        cancelAnimationFrame(rafId);

    // Palette + framebuffer were already reset before boot() in
    // instantiateCart(); resetting here would clobber boot()'s pal() overrides.
    lastFrame = null;
    acc = 0;
    rafId = requestAnimationFrame(t => frame(gen, t));
}

export async function start(cartPath)
{
    try
    {
        await loadCart(cartPath);

        run();
    }
    catch (err)
    {
        console.error("vex: failed to load cart:", err);
    }
}

// Load a cart from raw bytes (e.g. a dropped .wasm file).
export async function startBytes(bytes)
{
    await instantiateCart(bytes);

    run();
}

//// Part 13: Drag-and-drop cart loading

// Highlight the canvas while a file is dragged over the page, so it reads as
// a drop target.
function setDropCue(on)
{
    if (on)
    {
        canvas.style.outline = "4px dashed #38b764";
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
