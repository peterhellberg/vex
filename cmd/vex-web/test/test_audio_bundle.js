// Browser test: verifies that a vex-web -bundle of an audio cart actually
// plays audio -- i.e. the bundled index.html embeds the current audio host,
// the cart's tone()/vol() calls reach Web Audio as real node graphs and
// gain automation, and the context reaches the "running" state after a
// user gesture.
//
// Modeled on test_gamepad.js; run directly:
//
//   node cmd/vex-web/test/test_audio_bundle.js [path/to/cart.wasm]
//
// The bundle is rebuilt fresh from the cart so we never test stale assets.

const { chromium } = require('playwright');
const { execFileSync } = require('child_process');
const fs = require('fs');
const http = require('http');
const os = require('os');
const path = require('path');

const CART = process.argv[2] || path.join(__dirname, '..', '..', '..', 'bin', 'carts', 'test_audio.wasm');

let failures = 0;
let tests = 0;
function check(name, ok, detail) {
    tests++;
    const tag = ok ? 'PASS' : 'FAIL';
    console.log(`  [${tag}] ${name}${detail ? ' — ' + detail : ''}`);
    if (!ok) failures++;
}

// ---- static file server ----------------------------------------------------
function serveDir(dir) {
    return new Promise((resolve) => {
        const server = http.createServer((req, res) => {
            let urlPath = req.url.split('?')[0];
            if (urlPath === '/') urlPath = '/index.html';
            const filePath = path.join(dir, urlPath);
            fs.readFile(filePath, (err, data) => {
                if (err) {
                    res.statusCode = 404;
                    res.end('not found');
                    return;
                }
                res.setHeader('Content-Type',
                    urlPath.endsWith('.wasm') ? 'application/wasm' :
                    urlPath.endsWith('.html') ? 'text/html' : 'application/octet-stream');
                res.end(data);
            });
        });
        server.listen(0, '127.0.0.1', () => {
            resolve({ server, url: `http://127.0.0.1:${server.address().port}/` });
        });
    });
}

// ---- main --------------------------------------------------------------------
(async () => {
    // Fresh bundle from the given cart.
    const repo = path.join(__dirname, '..', '..', '..');
    execFileSync('go', ['run', './cmd/vex-web', '-bundle', CART], { cwd: repo });

    const name = path.basename(CART).replace(/\.wasm$/, '');
    const bundleDir = path.join(repo, 'bundle', name);
    if (!fs.existsSync(path.join(bundleDir, 'index.html'))) {
        console.log('  [FAIL] bundle was not written');
        process.exit(1);
    }

    const { server, url } = await serveDir(bundleDir);

    const browser = await chromium.launch({
        args: ['--autoplay-policy=no-user-gesture-required'],
    });
    const pageErrors = [];
    try {
        const ctx = await browser.newContext();
        const page = await ctx.newPage();
        page.on('pageerror', (e) => pageErrors.push(String(e)));

        // Instrument WebAudio before any page script runs.
        await page.addInitScript(() => {
            window.__audio = {
                ctxs: 0, osc: 0, gains: 0, sources: 0,
                ramps: 0, sets: 0, running: false, resumes: 0,
            };
            const Orig = window.AudioContext;
            window.AudioContext = class extends Orig {
                constructor(...a) {
                    super(...a);
                    window.__audio.ctxs++;
                    this.addEventListener('statechange', () => {
                        window.__audio.running = this.state === 'running';
                    });
                    for (const m of ['createOscillator', 'createGain', 'createBufferSource']) {
                        const fn = this[m].bind(this);
                        this[m] = (...args) => {
                            const key = { createOscillator: 'osc', createGain: 'gains',
                                          createBufferSource: 'sources' }[m];
                            window.__audio[key]++;
                            const node = fn(...args);
                            if (node.gain && node.gain.linearRampToValueAtTime) {
                                for (const rm of ['setValueAtTime', 'linearRampToValueAtTime',
                                                  'exponentialRampToValueAtTime']) {
                                    const rf = node.gain[rm].bind(node.gain);
                                    node.gain[rm] = (...ra) => {
                                        if (rm !== 'setValueAtTime') window.__audio.ramps++;
                                        else window.__audio.sets++;
                                        return rf(...ra);
                                    };
                                }
                            }
                            return node;
                        };
                    }
                }
            };
        });

        await page.goto(url);

        // User gesture to unlock audio (the same tap a player would make).
        await page.mouse.click(160, 90);

        // test_audio plays ~10 notes/second; give it two seconds of music.
        await page.waitForTimeout(2000);

        const a = await page.evaluate(() => window.__audio);

        check('one AudioContext created', a.ctxs === 1, `got ${a.ctxs}`);
        check('context reached running state', a.running || a.resumes > 0,
              `state-running=${a.running} resumes=${a.resumes}`);
        check('4 persistent channel oscillators', a.osc === 4, `got ${a.osc}`);
        check('12 gains (3 per channel)', a.gains === 12, `got ${a.gains}`);
        // 5th buffer source is the one-sample iOS unlock blip.
        check('4 looping noise sources', a.sources === 5, `got ${a.sources}`);
        check('cart notes scheduled gain automation', a.ramps > 20,
              `${a.ramps} ramps in 2s`);
        check('no page errors', pageErrors.length === 0,
              pageErrors.slice(0, 3).join('; '));

        // Sustained-state sanity: automation must keep flowing while the
        // cart keeps playing (not just once at startup).
        const r1 = a.ramps;
        await page.waitForTimeout(1000);
        const a2 = await page.evaluate(() => window.__audio.ramps);
        check('automation continues during playback', a2 > r1,
              `${r1} -> ${a2}`);
    } finally {
        await browser.close();
        server.close();
    }

    console.log(`\n${tests - failures}/${tests} checks passed`);
    process.exit(failures ? 1 : 0);
})().catch((e) => {
    console.error(e);
    process.exit(1);
});
