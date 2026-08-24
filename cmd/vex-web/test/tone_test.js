// Behavioral test of vex.js tone() with a mocked Web Audio API.
//
// Loads the real audio section of cmd/vex-web/assets/vex.js (sliced out by
// its "Part 3c" banner) and drives it through a fake AudioContext that
// records oscillator/gain scheduling, then asserts the same semantics the
// C host (rewritten Sound buffer) and Go host (toneEngine,
// cmd/vex-run/main_test.go) implement:
//
//   - channel clamps to 0..3, out-of-range coerced
//   - freq <= 0 silences the channel (stops the previous osc)
//   - freq > 20000 clamps to 20000
//   - ms <= 0: flat 100ms blip at peak gain 8000/32768
//   - ms > 0: duration secs = min(max(ms,1),5000)/1000 with an exponential
//     ramp from peak down to peak/256 at the end
//   - a new tone on an occupied channel replaces (mutes) its previous osc
//   - notes on different channels overlap
//
// Run via `make test-hosts` or directly:
//   node cmd/vex-web/test/tone_test.js [path/to/vex.js]

const fs = require("fs");
const vm = require("vm");
const path = require("path");

const VEX_JS =
  process.argv[2] || path.join(__dirname, "..", "assets", "vex.js");

let idCounter = 1;

class FakeOsc {
  constructor(ctx) {
    this.ctx = ctx;
    this.id = idCounter++;
    this.type = null;
    this.frequency = { value: 0 };
    this.connected = null;
    this.startedAt = null;
    this.stoppedAt = null;
    this.onended = null;
  }
  connect(node) { this.connected = node; node.inputs.push(this); return node; }
  start(t) { this.startedAt = t; this.ctx.started.push(this); }
  stop(t) {
    if (this.stoppedAt !== null) throw new Error("InvalidStateError");
    if (!this.ctx.running) throw new Error("InvalidStateError");
    this.stoppedAt = t;
  }
}

class FakeGain {
  constructor(ctx) {
    this.ctx = ctx;
    this.gain = new FakeParam(this.ctx);
    this.inputs = [];
    this.disconnected = false;
  }
  connect(dest) { this.dest = dest; return dest; }
  disconnect() { this.disconnected = true; }
}

const EPS = 1e-12;

class FakeParam {
  constructor(ctx) { this.ctx = ctx; this.events = []; this.value = 0; }
  setValueAtTime(v, t) { this.events.push(["set", v, t]); }
  exponentialRampToValueAtTime(v, t) { this.events.push(["ramp", v, t]); }
}

class FakeCtx {
  constructor() {
    this.currentTime = 10.0;
    this.sampleRate = 48000;
    this.state = "running";
    this.running = true;
    this.started = [];
    this.destination = { inputs: [] };
  }
  createOscillator() { return new FakeOsc(this); }
  createGain() { return new FakeGain(this); }
  createBuffer(ch, n, rate) { return { ch, n, rate }; }
  createBufferSource() {
    return { buffer: null, connect() {}, start() {} };
  }
}

const ctx = new FakeCtx();

const sandbox = {
  window: { addEventListener() {} },
  console,
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

const tone = sandbox.tone;
sandbox.unlockAudio(); // creates + resumes the (fake) AudioContext inside the vm
check("context unlocked", ctx.state === "running");

let failures = 0;
function check(name, cond) {
  if (!cond) { failures++; console.log("FAIL:", name); } else { console.log("ok:", name); }
}
function eq(a, b, eps = 1e-9) { return Math.abs(a - b) <= eps; }

function lastOsc() { return ctx.started[ctx.started.length - 1]; }

// Not unlocked -> dropped
ctx.state = "suspended";
tone(0, 440, 0);
check("dropped while suspended", ctx.started.length === 0);
ctx.state = "running";

// Legacy flat blip on channel 0
tone(0, 440, 0);
{
  const o = lastOsc();
  check("osc created", o !== undefined && o.type === "square");
  check("freq passes through", o.frequency.value === 440);
  const g = o.connected;
  const set = g.gain.events[0];
  check("flat blip: constant gain", set[0] === "set" && eq(set[1], 8000 / 32768));
  check("flat blip: no decay ramp", g.gain.events.length === 1);
  check("flat blip: starts now", eq(o.startedAt, 10.0));
  check("flat blip: 100ms", eq(o.stoppedAt - o.startedAt, 0.1));
}

// New tone on same channel replaces the old osc; different freq kept.
// Note: the old osc's stop() was already called to schedule its natural
// end, so the replacement's stop() throws InvalidStateError (swallowed by
// vex.js); muting happens through the gain disconnect, which is the
// audible effect we assert.
ctx.currentTime += 0.01;
tone(0, 880, 250);
{
  const prev = ctx.started[0];
  const o = lastOsc();
  check("previous gain disconnected (channel muted)", prev.connected.disconnected === true);
  check("replacement freq", o.frequency.value === 880);
  const ev = o.connected.gain.events;
  check("decay: set + ramp", ev.length === 2 && ev[0][0] === "set" && ev[1][0] === "ramp");
  const peak = 8000 / 32768;
  check("decay: starts at peak", eq(ev[0][1], peak));
  check("decay: ends at peak/256", eq(ev[1][1], peak / 256, 1e-15));
  check("decay: ms=250 duration", eq(o.stoppedAt - o.startedAt, 0.25));
}

// Channel clamp + overlap across channels
ctx.currentTime += 0.01;
const before = ctx.started.length;
tone(-9, 440, 0);   // -> channel 0
tone(42, 440, 0);   // -> channel 3
check("clamped channels scheduled", ctx.started.length === before + 2);

// freq 0 silences a channel without scheduling anything
ctx.currentTime += 0.01;
// The last two scheduled oscs are channel 0 (from -9) then channel 3 (from 42).
const ch0Osc = ctx.started[ctx.started.length - 2];
const ch3Osc = ctx.started[ctx.started.length - 1];
tone(0, 0, 0);
tone(99, 0, 0); // clamps to channel 3
check("freq 0 silences channels",
  ch0Osc.connected.disconnected && ch3Osc.connected.disconnected);
check("silence call schedules nothing", ctx.started.length === before + 2);

// freq > 20000 clamps; huge ms clamps to 5s
ctx.currentTime += 0.01;
tone(1, 100000, 999999);
{
  const o = lastOsc();
  check("freq clamped to 20000", o.frequency.value === 20000);
  check("ms clamped to 5000ms", eq(o.stoppedAt - o.startedAt, 5.0));
}

// ms between 0 and 1 keeps at least 1ms
ctx.currentTime += 0.01;
tone(2, 440, 0); // legacy anyway; use tiny positive ms instead
ctx.currentTime += 0.01;
tone(2, 440, -3); // negative ms is the legacy shim too
{
  const o = lastOsc();
  check("negative ms uses legacy flat 100ms",
    o.connected.gain.events.length === 1 && eq(o.stoppedAt - o.startedAt, 0.1));
}

process.exit(failures ? 1 : 0);
