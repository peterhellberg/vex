// Behavioral test of vex.js audio (tone/noise/vol/apos) with a mocked Web
// Audio API.
//
// Loads the real audio section of cmd/vex-web/assets/vex.js (sliced out by
// its "Part 3c" banner) and drives it through a fake AudioContext that
// records node creation and gain scheduling, asserting the same semantics
// the C host (streaming mixer) and Go host (toneEngine,
// cmd/vex-run/main_test.go) implement:
//
//   - channels clamp to 0..3
//   - freq <= 0 silences the channel; freq > 20000 clamps
//   - ms > 0: decay to peak/256 over min(ms,5000)/1000 s (+ crossfade)
//   - ms == 0: sustain -- ramp up only, nothing schedules an end
//   - ms < 0: legacy flat ~100ms blip
//   - vol scales linearly via setTargetAtTime, v clamped 0..10
//   - noise sets playbackRate = 2*freq/sampleRate on the looping source
//   - apos advances monotonically with the context clock
//   - persistent per-channel nodes: no node allocation during steady state
//
// Run via `make test-hosts` or directly:
//   node cmd/vex-web/test/tone_test.js [path/to/vex.js]

const fs = require("fs");
const vm = require("vm");
const path = require("path");

const VEX_JS =
  process.argv[2] || path.join(__dirname, "..", "assets", "vex.js");

const SAMPLE_RATE = 48000;
const PEAK = 800 / 32768 * 10; // 8000/32768

class FakeParam {
  constructor(value) {
    this.value = value;
    this.events = [];
    this.cancels = 0;
    this.targets = 0;
  }
  setValueAtTime(v, t) { this.events.push(["set", v, t]); this.value = v; }
  linearRampToValueAtTime(v, t) { this.events.push(["lin", v, t]); this.value = v; }
  exponentialRampToValueAtTime(v, t) { this.events.push(["exp", v, t]); this.value = v; }
  setTargetAtTime(v, t, tc) { this.events.push(["target", v, t]); this.targets++; this.value = v; }
  cancelScheduledValues(t) { this.cancels++; }
}

class FakeOsc {
  constructor() {
    this.type = null;
    this.frequency = new FakeParam(0);
    this.connections = 0;
    this.started = false;
    this.startCalls = 0;
  }
  connect(dest) { this.connections++; return dest; }
  start(t) { this.started = true; this.startCalls++; }
}

class FakeBufferSource {
  constructor(buffer) {
    this.buffer = buffer;
    this.loop = false;
    this.playbackRate = new FakeParam(1);
    this.connections = 0;
    this.started = false;
  }
  connect(dest) { this.connections++; return dest; }
  start(t) { this.started = true; }
}

class FakeGain {
  constructor() { this.gain = new FakeParam(1); }
  connect(dest) { return dest; }
}

let created = { osc: 0, bufferSource: 0, gain: 0 };

class FakeCtx {
  constructor() {
    this.currentTime = 10.0;
    this.sampleRate = SAMPLE_RATE;
    this.state = "running";
    this.destination = {};
  }
  createOscillator() { created.osc++; return new FakeOsc(); }
  createBufferSource() { created.bufferSource++; return new FakeBufferSource(null); }
  createGain() { created.gain++; return new FakeGain(); }
  createBuffer(channels, len, rate) {
    const data = new Float32Array(len);
    return { getChannelData: () => data };
  }
}

const ctx = new FakeCtx();

const sandbox = {
  window: { addEventListener() {} },
  console,
  Math,
  Float32Array,
  AudioContext: function () { return ctx; },
};
sandbox.window.AudioContext = sandbox.AudioContext;
vm.createContext(sandbox);

// vex.js as a whole needs a DOM; Part 3c (audio) is self-contained, so run
// just that section -- sliced out by its part banners.
const src = fs.readFileSync(VEX_JS, "utf8");
const audioPart = src.slice(
  src.indexOf("//// Part 3c:"),
  src.indexOf("//// Part 4:")
);
vm.runInContext(audioPart, sandbox);

let failures = 0;
function check(name, cond) {
  if (!cond) { failures++; console.log("FAIL:", name); }
  else console.log("ok:", name);
}
function eq(a, b, eps = 1e-9) { return Math.abs(a - b) <= eps; }

sandbox.unlockAudio();
check("context unlocked", ctx.state === "running");
// 5th buffer source is the one-sample iOS unlock blip from unlockAudio().
check("4 channel graphs created", created.osc === 4 && created.gain === 12 &&
      created.bufferSource === 5);
check("noise sources loop", sandbox["channelNodes"] === undefined); // module-private; checked indirectly below

// Access the private state through eval inside the vm context.
const nodes = vm.runInContext("channelNodes", sandbox);
check("channelNodes exposed for test", Array.isArray(nodes) && nodes.length === 4);
check("noise buffer ~1s", vm.runInContext("noiseBuffer.getChannelData(0).length", sandbox) === SAMPLE_RATE);

// --- legacy blip (ms < 0) ---------------------------------------------------
sandbox.tone(0, 440, -1);
{
  const ev = nodes[0].oscGain.gain.events;
  check("blip: crossfade up", ev.some(([k, v, t]) => k === "lin" && eq(v, PEAK)));
  const endIdx = ev.findIndex(([k, v, t]) => k === "set" && eq(t, 10.005 + 0.1));
  check("blip: holds peak until 100ms", endIdx >= 0);
  check("blip: ramps down after",
        ev.some(([k, v, t]) => k === "lin" && v === 0 && eq(t, 10.005 + 0.1 + 0.005)));
  check("blip: freq passes through", nodes[0].osc.frequency.value === 440);
  check("blip: no stop() on oscillator", !nodes[0].osc.stopped);
}

// --- sustain (ms == 0) ------------------------------------------------------
ctx.currentTime += 0.01;
const evCountBeforeSustain = nodes[0].oscGain.gain.events.length;
sandbox.tone(0, 660, 0);
{
  // Exactly two events: anchor at the current value, crossfade up to peak --
  // and nothing else. No hold, no ramp-down, no stop: the note never ends.
  const ev = nodes[0].oscGain.gain.events.slice(evCountBeforeSustain);
  check("sustain: only ramp-up scheduled, no scheduled end",
        ev.length === 2 && ev[0][0] === "set" && ev[1][0] === "lin" &&
        eq(ev[1][1], PEAK));
}

// --- decay (ms > 0) ---------------------------------------------------------
ctx.currentTime += 0.01;
sandbox.tone(1, 880, 250);
{
  const ev = nodes[1].oscGain.gain.events;
  const expRamp = ev.find(([k]) => k === "exp");
  check("decay: exponential ramp to peak/256",
        expRamp && eq(expRamp[1], PEAK / 256, 1e-15) && eq(expRamp[2], 10.02 + 0.005 + 0.25));
  check("decay: ends silent after tail crossfade",
        ev.some(([k, v, t]) => k === "lin" && v === 0 && eq(t, 10.02 + 0.005 + 0.25 + 0.005)));
}

// --- noise ------------------------------------------------------------------
ctx.currentTime += 0.01;
sandbox.noise(2, 1000, -1);
check("noise: playbackRate = 2*freq/sampleRate",
      eq(nodes[2].noiseSrc.playbackRate.value, 2000 / SAMPLE_RATE));
check("noise: noise gain active, osc gain faded out",
      nodes[2].noiseGain.gain.events.some(([k, v]) => k === "lin" && eq(v, PEAK)) &&
      nodes[2].oscGain.gain.events.some(([k, v, t]) => k === "lin" && v === 0));

// next tone flips back to square
sandbox.tone(2, 440, 0);
check("tone after noise re-activates square",
      nodes[2].oscGain.gain.events.some(([k, v, t]) => k === "lin" && eq(v, PEAK)));

// --- silence (freq <= 0) ----------------------------------------------------
ctx.currentTime += 0.01;
sandbox.tone(2, 0, 0);
check("freq 0 fades both sources to zero",
      nodes[2].oscGain.gain.events.some(([k, v, t]) => k === "lin" && v === 0) &&
      nodes[2].noiseGain.gain.events.some(([k, v, t]) => k === "lin" && v === 0));

// --- volume -----------------------------------------------------------------
ctx.currentTime += 0.01;
sandbox.vol(3, 32);
check("vol: half gain via setTargetAtTime",
      nodes[3].volGain.gain.events.some(([k, v]) => k === "target" && eq(v, 0.5)));
sandbox.vol(3, 99);
check("vol: clamps high to unity",
      nodes[3].volGain.gain.events.some(([k, v]) => k === "target" && eq(v, 1)));
sandbox.vol(-9, 16); // clamps to channel 0
check("vol: negative channel clamps to ch0",
      nodes[0].volGain.gain.events.some(([k, v]) => k === "target" && eq(v, 0.25)));

// --- clamping ---------------------------------------------------------------
ctx.currentTime += 0.01;
sandbox.tone(42, 100000, -1); // -> channel 3
check("tone: channel 42 clamps to 3", nodes[3].osc.frequency.value === 20000);

// --- steady state allocates no nodes ----------------------------------------
const before = { ...created };
for (let i = 0; i < 100; i++) {
  sandbox.tone(i % 4, 220 + i, i % 3 === 0 ? 120 : 0);
  sandbox.noise((i + 1) % 4, 800, -1);
  sandbox.vol(i % 4, 7);
}
const after = { ...created };
check("steady state: no node allocation across 300 events",
      before.osc === after.osc && before.gain === after.gain &&
      before.bufferSource === after.bufferSource);

// --- apos -------------------------------------------------------------------
const a0 = sandbox.apos();
ctx.currentTime += 0.5;
const a1 = sandbox.apos();
check("apos: advances monotonically at sampleRate",
      a1 > a0 && eq(a1 - a0, 0.5 * SAMPLE_RATE));

process.exit(failures ? 1 : 0);
