// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 M-Vave-OP1 contributors
// The browser emulator's WebAssembly without a browser: boot, the screen drawn, a key sounds and lights its LED, a
// button lights its LED, MIDI IN sounds, the render keeps up with real time by a wide margin.
//   node web/emu/emu_test.mjs build/emu/emu.wasm
import { readFileSync } from "node:fs";

let failed = 0;
const ok = (c, what) => { console.log(`${what.padEnd(60)} ${c ? "ok" : "FAIL"}`); if (!c) failed++; };

const { instance } = await WebAssembly.instantiate(readFileSync(process.argv[2] || "build/emu/emu.wasm"), {});
const ex = instance.exports;
ex._initialize();
ex.web_boot();
const N = 128;
const render = (quanta) => {
  let peak = 0;
  for (let q = 0; q < quanta; q++) {
    ex.web_render(N);
    const l = new Float32Array(ex.memory.buffer, ex.web_out_l(), N);
    for (const v of l) peak = Math.max(peak, Math.abs(v));
  }
  return peak;
};
const screen = () => new Uint16Array(ex.memory.buffer, ex.web_screen(), 240 * 240);
const leds = () => new Uint8Array(ex.memory.buffer, ex.web_leds(), 41);

render(20);
const fb = screen(), bg = fb[0];
ok(fb.some((c) => c !== bg), "boot: the screen drawn");
ok(ex.web_sample_rate() === 44100, "sample rate 44100");
ok(render(50) === 0, "silent with nothing held");

ex.web_keys(1);
const p = render(100);
ok(p > 0.05 && p <= 0.5, `a key sounds (peak ${p.toFixed(3)})`);
ok(leds()[14] === 1, "a key lights its LED");
ex.web_keys(0);
render(20);
ok(render(20) === 0 && leds()[14] === 0, "released: silent, LED dark");

ex.web_buttons(1 << 10);                     // PLAY
render(20);
ok(leds()[ex.web_button_id(10)] === 1, "PLAY held lights its LED");
ex.web_buttons(0);

const draws = ex.web_screen_draws();
ex.web_enc(3, 2);                            // KNOB 1
render(20);
ok(ex.web_screen_draws() > draws, "a knob turned redraws");

ex.web_midi(0x90, 69, 100);
ok(render(50) > 0.05, "MIDI note-on sounds");
ex.web_midi(0x80, 69, 0);
render(10);
ok(render(20) === 0, "MIDI note-off: silent");

const t0 = performance.now();
ex.web_keys(1);
render(44100 / N * 5);                       // 5 s of audio with the main loop
const rt = 5000 / (performance.now() - t0);
ok(rt > 20, `5 s rendered at ${rt.toFixed(0)}x real time (> 20x)`);

console.log(failed ? `EMU TEST FAILED (${failed})` : "emu test passed");
process.exit(failed ? 1 : 0);
