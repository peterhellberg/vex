// Browser tests for the vex-web virtual gamepad.
//
// Boots a tiny static server pointing at a freshly-built bundle of the
// cart given on the command line (default: ./bin/zcart.wasm), then
// drives the page with Playwright across several viewport sizes. Each
// test reports PASS/FAIL on stdout; the process exits non-zero on the
// first failure so it can plug into `make test-web`.
//
// Run via the Makefile:
//
//   make test-web                       # tests with the default cart
//   make test-web CART=bin/cart.wasm    # tests with a different cart
//
// Or directly:
//
//   node cmd/vex-web/test/test_gamepad.js [path/to/cart.wasm]
//
// First run downloads Chromium (~115MB) via `npx playwright install`.

const { chromium } = require('playwright');
const { spawn, execFileSync } = require('child_process');
const fs = require('fs');
const http = require('http');
const os = require('os');
const path = require('path');

const CART = process.argv[2] || path.join(__dirname, '..', '..', '..', 'bin', 'zcart.wasm');

// ---- harness --------------------------------------------------------------

let failures = 0;
let tests = 0;

function check(name, ok, detail) {
    tests++;
    const tag = ok ? 'PASS' : 'FAIL';
    console.log(`  [${tag}] ${name}${detail ? ' — ' + detail : ''}`);
    if (!ok) failures++;
}

function approxEq(a, b, tol) {
    return Math.abs(a - b) <= tol;
}

// ---- static file server for the bundle -----------------------------------

// `serveDir(dir, extra)` returns a tiny HTTP server bound to an ephemeral port,
// plus the URL it ended up listening on. Used to serve the freshly
// built bundle so the test never hits a stale copy from a previous run.
//
// `extra` maps URL paths to files outside the bundle, as {path: file}.
// A bundle inlines vex.js into index.html rather than shipping it, so
// '/vex.js' is served from the source tree to let a test import the module
// and reach its exported helpers. It loads a second copy of the module into
// the page, which is harmless here: the gamepad checks use their own page and
// only read pure functions.
function serveDir(dir, extra = {}) {
    return new Promise((resolve) => {
        const server = http.createServer((req, res) => {
            let urlPath = req.url.split('?')[0];
            if (urlPath === '/') urlPath = '/index.html';
            const filePath = extra[urlPath] || path.join(dir, urlPath);
            fs.readFile(filePath, (err, data) => {
                if (err) {
                    res.statusCode = 404;
                    res.end('not found');
                    return;
                }
                const ext = path.extname(filePath).toLowerCase();
                const type =
                    ext === '.html' ? 'text/html; charset=utf-8' :
                    ext === '.js'   ? 'text/javascript; charset=utf-8' :
                    ext === '.wasm' ? 'application/wasm' :
                    'application/octet-stream';
                res.setHeader('Content-Type', type);
                res.end(data);
            });
        });
        server.listen(0, '127.0.0.1', () => {
            const { port } = server.address();
            resolve({ server, url: `http://127.0.0.1:${port}` });
        });
    });
}

// ---- bundle build ---------------------------------------------------------

// `buildBundle(cart)` invokes `go run ./cmd/vex-web -bundle <cart>` and
// returns the on-disk path of the bundle directory it wrote. The tool
// prints "wrote bundle bundle/<name> and bundle/<name>.zip" relative
// to wherever it was invoked from, so we resolve against that working
// directory to get an absolute path back.
function buildBundle(cart) {
    const repoRoot = path.resolve(__dirname, '..', '..', '..');
    const out = execFileSync(
        'go',
        ['run', './cmd/vex-web', '-bundle', cart],
        { cwd: repoRoot, encoding: 'utf8' }
    );

    const m = out.match(/wrote bundle (\S+) and /);
    if (!m) throw new Error(`could not parse bundle output: ${out}`);
    return path.resolve(repoRoot, m[1]);
}

// ---- browser Gamepad API mapping ----------------------------------------

// The real-pad path is pure: gamepadMaskOf(pad) -> cart button mask. Chromium
// has no scriptable way to attach a physical pad, so the pad is stubbed and the
// exported mapper is called directly. Button bits: 1 left, 2 right, 4 up,
// 8 down, 16 Z, 32 X.
async function runGamepadApiChecks(browser, url) {
    console.log('\n=== Gamepad API mapping ===');
    const ctx = await browser.newContext({ viewport: { width: 844, height: 390 } });
    const page = await ctx.newPage();
    const pageErrors = [];
    page.on('pageerror', err => pageErrors.push(err.message));

    // Count getGamepads() calls so we can prove the frame loop actually polls.
    await page.addInitScript(() => {
        window.__padPolls = 0;
        const real = navigator.getGamepads.bind(navigator);
        navigator.getGamepads = function () {
            window.__padPolls++;
            return real();
        };
    });

    await page.goto(url);
    await page.waitForTimeout(400);

    const m = await page.evaluate(async () => {
        const { gamepadMaskOf } = await import('./vex.js');
        // A standard-mapping pad: pressed button indices plus a stick position.
        const pad = (pressed, x = 0, y = 0) => ({
            mapping: 'standard',
            connected: true,
            buttons: Array.from({ length: 17 }, (_, i) => ({ pressed: pressed.includes(i) })),
            axes: [x, y, 0, 0],
        });
        return {
            idle: gamepadMaskOf(pad([])),
            dpadUp: gamepadMaskOf(pad([12])),
            dpadDown: gamepadMaskOf(pad([13])),
            dpadLeft: gamepadMaskOf(pad([14])),
            dpadRight: gamepadMaskOf(pad([15])),
            faceA: gamepadMaskOf(pad([0])),
            faceB: gamepadMaskOf(pad([1])),
            shoulderL: gamepadMaskOf(pad([4])),
            shoulderR: gamepadMaskOf(pad([5])),
            unassigned: gamepadMaskOf(pad([3, 8, 11])),
            stickUp: gamepadMaskOf(pad([], 0, -0.9)),
            stickDown: gamepadMaskOf(pad([], 0, 0.9)),
            stickLeft: gamepadMaskOf(pad([], -0.9, 0)),
            stickRight: gamepadMaskOf(pad([], 0.9, 0)),
            justPastDeadzone: gamepadMaskOf(pad([], -0.35, 0)),
            drift: gamepadMaskOf(pad([], 0.1, -0.2)),
            diagonal: gamepadMaskOf(pad([], -0.9, -0.9)),
            dpadPlusStick: gamepadMaskOf(pad([13], 1, 0)),
            noMapping: gamepadMaskOf({ ...pad([0, 12]), mapping: '' }),
            disconnected: gamepadMaskOf({ ...pad([0, 12]), connected: false }),
            nullPad: gamepadMaskOf(null),
            shortButtons: gamepadMaskOf({ mapping: 'standard', connected: true, buttons: [], axes: [] }),
            polls: window.__padPolls,
        };
    });

    check('page errors while polling pads', pageErrors.length === 0, pageErrors.join('; '));
    check('frame loop polls getGamepads', m.polls > 0, `${m.polls} calls`);

    check('idle pad reads nothing', m.idle === 0, `mask=${m.idle}`);
    check('d-pad up -> up', m.dpadUp === 4, `mask=${m.dpadUp}`);
    check('d-pad down -> down', m.dpadDown === 8, `mask=${m.dpadDown}`);
    check('d-pad left -> left', m.dpadLeft === 1, `mask=${m.dpadLeft}`);
    check('d-pad right -> right', m.dpadRight === 2, `mask=${m.dpadRight}`);
    check('face A -> Z', m.faceA === 16, `mask=${m.faceA}`);
    check('face B -> X', m.faceB === 32, `mask=${m.faceB}`);
    check('left shoulder -> Z', m.shoulderL === 16, `mask=${m.shoulderL}`);
    check('right shoulder -> X', m.shoulderR === 32, `mask=${m.shoulderR}`);
    check('unassigned buttons ignored', m.unassigned === 0, `mask=${m.unassigned}`);

    check('stick up -> up', m.stickUp === 4, `mask=${m.stickUp}`);
    check('stick down -> down', m.stickDown === 8, `mask=${m.stickDown}`);
    check('stick left -> left', m.stickLeft === 1, `mask=${m.stickLeft}`);
    check('stick right -> right', m.stickRight === 2, `mask=${m.stickRight}`);
    check('stick just past deadzone registers', m.justPastDeadzone === 1, `mask=${m.justPastDeadzone}`);
    check('resting stick drift ignored', m.drift === 0, `mask=${m.drift}`);
    check('diagonal sets two arrows', m.diagonal === 5, `mask=${m.diagonal}`);
    check('d-pad and stick combine', m.dpadPlusStick === 10, `mask=${m.dpadPlusStick}`);

    check('non-standard pad ignored', m.noMapping === 0, `mask=${m.noMapping}`);
    check('disconnected pad ignored', m.disconnected === 0, `mask=${m.disconnected}`);
    check('null pad slot ignored', m.nullPad === 0, `mask=${m.nullPad}`);
    check('pad with empty arrays ignored', m.shortButtons === 0, `mask=${m.shortButtons}`);

    await ctx.close();
}

// ---- per-viewport test ----------------------------------------------------

async function runFor(browser, label, w, h, url) {
    console.log(`\n=== ${label} (${w}x${h}) ===`);
    const ctx = await browser.newContext({ viewport: { width: w, height: h }, hasTouch: true });
    const page = await ctx.newPage();
    const pageErrors = [];
    page.on('pageerror', err => pageErrors.push(err.message));
    await page.goto(url);
    // Give the cart a moment to start drawing before we snapshot.
    await page.waitForTimeout(400);

    const bootFailed = await page.locator('body > pre').count() > 0;
    check('cart booted without page errors', !bootFailed && pageErrors.length === 0,
        pageErrors.join('; '));

    const gamepadVisible = await page.evaluate(() => {
        const el = document.getElementById('gamepad');
        if (!el) return false;
        return window.getComputedStyle(el).display !== 'none';
    });

    const portraitish = h > w * 1.2;
    check('gamepad shown iff viewport is portrait-ish', gamepadVisible === portraitish);

    if (!gamepadVisible) {
        await ctx.close();
        return;
    }

    // ---- button layout ----

    // All six buttons should be present and the same size (square cells).
    const layout = await page.evaluate(() => {
        const sel = ['.dpad-up', '.dpad-left', '.dpad-z', '.dpad-right', '.dpad-down', '.btn-x'];
        return sel.map(s => {
            const el = document.querySelector(`#gamepad ${s}`);
            if (!el) return { sel: s, missing: true };
            const r = el.getBoundingClientRect();
            return { sel: s, x: Math.round(r.left), y: Math.round(r.top), w: Math.round(r.width), h: Math.round(r.height) };
        });
    });

    let allFound = true;
    for (const b of layout) {
        if (b.missing) {
            check(`button ${b.sel} present`, false, 'not found');
            allFound = false;
        }
    }
    if (allFound) check('all six buttons present', true);

    // All buttons should be the same size (1px tolerance for sub-pixel
    // rounding across browsers).
    const sizes = layout.map(b => `${b.w}x${b.h}`);
    const allSame = sizes.every(s => s === sizes[0]);
    check('all six buttons are the same size', allSame, sizes.join(', '));

    // Cells should be roughly square (within 2px).
    const allSquare = layout.every(b => approxEq(b.w, b.h, 2));
    check('all six buttons are square', allSquare, layout.map(b => `${b.sel}=${b.w}x${b.h}`).join(' '));

    // ---- touch input ----

    await page.evaluate(() => {
        document.querySelector('#gamepad .dpad-up').dispatchEvent(
            new PointerEvent('pointerdown', { pointerId: 1, bubbles: true, cancelable: true })
        );
    });
    await page.waitForTimeout(30);
    const upHeld = await page.evaluate(() =>
        document.querySelector('#gamepad .dpad-up').classList.contains('active'));
    check('pointerdown highlights dpad-up', upHeld);
    await page.evaluate(() => {
        document.querySelector('#gamepad .dpad-up').dispatchEvent(
            new PointerEvent('pointerup', { pointerId: 1, bubbles: true })
        );
    });

    // ---- keyboard input highlights matching button ----

    await page.keyboard.down('KeyZ');
    await page.waitForTimeout(30);
    const zHeld = await page.evaluate(() =>
        document.querySelector('#gamepad .dpad-z').classList.contains('active'));
    check('keyboard KeyZ highlights dpad-z', zHeld);
    await page.keyboard.up('KeyZ');

    await page.keyboard.down('ArrowUp');
    await page.waitForTimeout(30);
    const upKb = await page.evaluate(() =>
        document.querySelector('#gamepad .dpad-up').classList.contains('active'));
    check('keyboard ArrowUp highlights dpad-up', upKb);
    await page.keyboard.up('ArrowUp');

    await page.keyboard.down('KeyX');
    await page.waitForTimeout(30);
    const xHeld = await page.evaluate(() =>
        document.querySelector('#gamepad .btn-x').classList.contains('active'));
    check('keyboard KeyX highlights btn-x', xHeld);
    await page.keyboard.up('KeyX');

    // ---- multi-touch: two buttons held at once ----

    const both = await page.evaluate(() => {
        const l = document.querySelector('#gamepad .dpad-left');
        const r = document.querySelector('#gamepad .dpad-right');
        l.dispatchEvent(new PointerEvent('pointerdown', { pointerId: 1, bubbles: true, cancelable: true }));
        r.dispatchEvent(new PointerEvent('pointerdown', { pointerId: 2, bubbles: true, cancelable: true }));
        const out = {
            leftActive: l.classList.contains('active'),
            rightActive: r.classList.contains('active'),
        };
        l.dispatchEvent(new PointerEvent('pointerup', { pointerId: 1, bubbles: true }));
        r.dispatchEvent(new PointerEvent('pointerup', { pointerId: 2, bubbles: true }));
        return out;
    });
    check('multi-touch: left and right both highlight', both.leftActive && both.rightActive,
        `left=${both.leftActive}, right=${both.rightActive}`);

    // ---- a screenshot for the humans ----

    const out = `/tmp/vex_test_${label}.png`;
    await page.screenshot({ path: out });
    console.log(`  (screenshot: ${out})`);

    await ctx.close();
}

// ---- main -----------------------------------------------------------------

(async () => {
    if (!fs.existsSync(CART)) {
        console.error(`cart not found: ${CART}`);
        console.error('build it first (e.g. `zig build --prefix . -Dhost=false`)');
        process.exit(2);
    }

    console.log(`building bundle from ${CART}...`);
    const bundleDir = buildBundle(CART);
    console.log(`bundle ready: ${bundleDir}`);

    const { server, url } = await serveDir(bundleDir, {
        '/vex.js': path.join(__dirname, '..', 'assets', 'vex.js'),
    });

    let exitCode = 0;
    try {
        const browser = await chromium.launch();

        await runGamepadApiChecks(browser, url);
        await runFor(browser, 'iPhone_SE',     375, 667, url);
        await runFor(browser, 'iPhone_12',     390, 844, url);
        await runFor(browser, 'iPhone_ProMax', 430, 932, url);
        await runFor(browser, 'square',        600, 600, url);
        await runFor(browser, 'landscape',     844, 390, url);

        await browser.close();
    } finally {
        server.close();
        // `go run ./cmd/vex-web -bundle` writes `bundle/<name>/` and
        // `bundle/<name>.zip` next to the repo root. Drop them in
        // every code path — success, failure, or exception — so the
        // test never leaves a scratch directory behind.
        try {
            fs.rmSync(path.resolve(bundleDir, '..'), { recursive: true, force: true });
        } catch (_) {}
    }

    console.log(`\n${tests - failures}/${tests} checks passed`);
    if (failures > 0) {
        console.log(`${failures} failure(s)`);
        exitCode = 1;
    }
    process.exit(exitCode);
})().catch((err) => {
    console.error(err.stack || err.message || err);
    process.exit(2);
});
