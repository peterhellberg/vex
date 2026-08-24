// Temporary diagnostic: drive a vex-web bundle in Chromium, tap the master
// mix and each channel's volGain through ScriptProcessorNodes, and write the
// resulting PCM to /tmp/opencode for comparison against the native hosts.
//
// Usage: xvfb-run -a node capture_vs.js [cart]
const { chromium } = require('playwright');
const { execFileSync } = require('child_process');
const fs = require('fs');
const http = require('http');
const path = require('path');

const CART = process.argv[2] || '/tmp/vex-sound.wasm';
const repo = path.join(__dirname, '..', '..', '..');

function serveDir(dir) {
    return new Promise((resolve) => {
        const server = http.createServer((req, res) => {
            let p = req.url.split('?')[0];
            if (p === '/') p = '/index.html';
            fs.readFile(path.join(dir, p), (err, data) => {
                if (err) { res.statusCode = 404; res.end(); return; }
                res.setHeader('Content-Type', p.endsWith('.wasm') ? 'application/wasm' : 'text/html');
                res.end(data);
            });
        });
        server.listen(0, '127.0.0.1', () =>
            resolve({ server, url: `http://127.0.0.1:${server.address().port}/` }));
    });
}

function drain(page, arrName, count, chunk) {
    // Pull Float32 chunks out of the page in batches; returns flat number[].
    return (async () => {
        const all = [];
        for (let i = 0; i < count; i += 8) {
            const slice = await page.evaluate(
                ([name, from, to]) => {
                    const arr = window[name] || [];
                    const out = [];
                    for (let j = from; j < Math.min(to, arr.length); j++)
                        out.push(Array.from(arr[j]));
                    return out;
                }, [arrName, i, i + 8]);
            for (const a of slice) all.push(...a);
        }
        void chunk;
        return all;
    })();
}

function toPcm(floats, file) {
    const buf = Buffer.alloc(floats.length * 2);
    for (let i = 0; i < floats.length; i++) {
        const v = Math.max(-1, Math.min(1, floats[i]));
        buf.writeInt16LE(Math.round(v * 32767), i * 2);
    }
    fs.writeFileSync(file, buf);
}

(async () => {
    execFileSync('go', ['run', './cmd/vex-web', '-bundle', CART], { cwd: repo });
    const name = path.basename(CART).replace(/\.wasm$/, '');
    const { server, url } = await serveDir(path.join(repo, 'bundle', name));

    const browser = await chromium.launch({
        headless: !process.env.XVFB,
        args: ['--autoplay-policy=no-user-gesture-required'],
    });
    try {
        const page = await (await browser.newContext()).newPage();
        page.on('pageerror', e => console.error('PAGEERROR', String(e)));
        await page.addInitScript(() => {
            window.__dbg = { states: [], osc: 0, srcs: 0, connects: 0 };
            window.__chunks = [];      // master mix
            window.__tapBuf = {};      // ch -> [Float32Array]

            // Per-channel tap hook, called by vex.js's initChannelNodes.
            window.__taps = function (ctx, oscG, noiG, smG, volG) {
                try {
                    const ch = ctx.__tapN = (ctx.__tapN || 0);
                    ctx.__tapN = ch + 1;
                    const proc = ctx.createScriptProcessor(4096, 1, 1);
                    window.__tapBuf[ch] = [];
                    proc.onaudioprocess = e =>
                        window.__tapBuf[ch].push(new Float32Array(e.inputBuffer.getChannelData(0)));
                    volG.connect(proc);
                    proc.connect(ctx.destination); // needed so Chrome pulls it
                } catch (err) {
                    window.__dbg.tapErr = String(err);
                }
            };

            const Orig = window.AudioContext;
            window.AudioContext = class extends Orig {
                constructor(...a) {
                    super(...a);
                    const ctx = this;
                    window.__dbg.states.push(this.state);
                    this.addEventListener('statechange', () => window.__dbg.states.push(this.state));
                    const proc = this.createScriptProcessor(4096, 1, 1);
                    proc.onaudioprocess = e => {
                        window.__chunks.push(new Float32Array(e.inputBuffer.getChannelData(0)));
                    };
                    proc.connect(this.destination); // eager pull

                    const patch = (node) => {
                        const oc = node.connect.bind(node);
                        node.connect = function (dest, ...rest) {
                            if (dest === ctx.destination) {
                                window.__dbg.connects++;
                                oc(proc);
                                return dest;
                            }
                            return oc(dest, ...rest);
                        };
                        return node;
                    };
                    for (const m of ['createOscillator', 'createGain', 'createBufferSource']) {
                        const fn = this[m].bind(this);
                        this[m] = (...a) => { window.__dbg[m === 'createOscillator' ? 'osc' : m === 'createGain' ? 'gains' : 'srcs']++; return patch(fn(...a)); };
                    }
                    window.__ctxRef = ctx;
                }
            };
        });

        await page.goto(url);
        await page.mouse.click(160, 90);
        await page.waitForTimeout(3000);

        const dbg = await page.evaluate(() => ({
            ...window.__dbg,
            masterChunks: (window.__chunks || []).length,
            taps: Object.fromEntries(Object.entries(window.__tapBuf || {}).map(([k, v]) => [k, v.length])),
        }));
        console.log('DBG', JSON.stringify(dbg));

        const nCh = Object.keys(dbg.taps || {}).length;
        const master = await drain(page, '__chunks', dbg.masterChunks);
        toPcm(master, '/tmp/opencode/js_vs.pcm');
        console.log('wrote /tmp/opencode/js_vs.pcm,', master.length, 'frames');
        for (let ch = 0; ch < nCh; ch++) {
            const cnt = dbg.taps[ch];
            const data = await drain(page, '__tapBuf.' + ch, 0); // placeholder
            void data;
            // drain per-channel (nested array path needs special handling)
            const all = [];
            for (let i = 0; i < cnt; i += 8) {
                const slice = await page.evaluate(
                    ([ch, from, to]) => {
                        const arr = window.__tapBuf[ch] || [];
                        const out = [];
                        for (let j = from; j < Math.min(to, arr.length); j++)
                            out.push(Array.from(arr[j]));
                        return out;
                    }, [ch, i, i + 8]);
                for (const a of slice) all.push(...a);
            }
            toPcm(all, `/tmp/opencode/js_ch${ch}.pcm`);
            console.log(`wrote /tmp/opencode/js_ch${ch}.pcm,`, all.length, 'frames');
        }
        void nCh;
    } finally {
        await browser.close();
        server.close();
    }
})().catch(e => { console.error(e); process.exit(1); });
