// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 M-Vave-OP1 contributors
// Drive the browser emulator (web/emu, built by web/emu/build.sh and served from localhost) with Playwright and save
// screenshots. Edit the STEPS list for the workflow to check.
//   python3 -m http.server -d build/emu 8765 &
//   NODE_PATH=$(npm root -g) node .claude/skills/fm1-platform/scripts/emu_drive.js http://127.0.0.1:8765/ build/emu_shots
// Steps: ["key", "z", ms] a note key by its computer key (z = F3; see the page's help line), held ms;
// ["button", "PLAY", ms] a panel button by its printed label, held ms; ["knob", "KNOB 1", n] n detents (- = left);
// ["wait", ms]; ["shot", name].
const { chromium } = require('playwright');
const fs = require('fs');
const [url, out = 'build/emu_shots'] = process.argv.slice(2);
const STEPS = [
  ['shot', 'boot'],
  ['knob', 'KNOB 1', 3],
  ['knob', 'KNOB 4', -2],
  ['shot', 'knobs'],
  ['button', 'PLAY', 600],
  ['key', 'z', 600],
  ['wait', 300],
  ['shot', 'after'],
];
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

(async () => {
  fs.mkdirSync(out, { recursive: true });
  const browser = await chromium.launch({ args: ['--autoplay-policy=no-user-gesture-required'] });
  const page = await browser.newPage({ viewport: { width: 900, height: 900 } });
  const logs = [];
  page.on('console', (m) => { if (m.type() === 'error') logs.push(m.text()); });
  page.on('pageerror', (e) => logs.push('pageerror: ' + e.message));
  await page.goto(url);
  await page.click('#go');
  await page.waitForSelector('#start', { state: 'hidden', timeout: 20000 });
  await sleep(500);
  for (const [op, a, b] of STEPS) {
    if (op === 'key') { await page.keyboard.down(a); await sleep(b || 200); await page.keyboard.up(a); await sleep(100); }
    else if (op === 'button') {
      const btn = page.locator('button.b', { hasText: new RegExp(`^${a.replace(/[+-]/g, '\\$&')}$`) });
      const box = await btn.boundingBox();
      await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
      await page.mouse.down();
      await sleep(b || 200);
      await page.locator('#screen').screenshot({ path: `${out}/${a}-held-screen.png` });   // while held
      await sleep(100);
      await page.mouse.up();
      await sleep(100);
    } else if (op === 'knob') {
      await page.focus(`[role="slider"][aria-label="${a}"]`);
      for (let i = 0; i < Math.abs(b); i++) { await page.keyboard.press(b > 0 ? 'ArrowUp' : 'ArrowDown'); await sleep(60); }
      await page.evaluate(() => document.activeElement && document.activeElement.blur());   // or keys go to it
      await sleep(150);
    } else if (op === 'wait') await sleep(a);
    else if (op === 'shot') {
      await sleep(120);                                    // a frame or two (the page draws ~30 times a second)
      await page.locator('#screen').screenshot({ path: `${out}/${a}-screen.png` });
      await page.screenshot({ path: `${out}/${a}-page.png` });
    }
  }
  console.log(logs.length ? 'console errors:\n' + logs.join('\n') : 'no console errors');
  await browser.close();
})().catch((e) => { console.error(e); process.exit(1); });
