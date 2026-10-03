#!/usr/bin/env node
// A design/handoff/design/*.dc.html referenciák képernyőnkénti PNG-renderelése headless
// Chrome-mal (CDP, függőség nélkül; Node >= 22 a beépített WebSocket miatt).
//   node design/handoff/renders/render.mjs            (a repo gyökeréből)
// Kimenet: design/handoff/renders/<fájl>--<képernyő>.png  (1x lépték, a képernyők natív mérete).
// A Main Window fájlt világos ÉS sötét témában is lerendereli (a `theme` tweak átírásával,
// ideiglenes másolaton). Internet kell a webfontokhoz/ikonokhoz.
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const designDir = path.resolve(here, '../design');
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'tanara-render-'));
fs.copyFileSync(path.join(designDir, 'support.js'), path.join(tmp, 'support.js'));

// [forrásfájl, kimeneti előtag, forrás-átírás]
const jobs = [
  ['Tanara Visual Language.dc.html', 'visual-language', (s) => s],
  ['Tanara Main Window.dc.html', 'main-light', (s) => s.replace("this.props.theme ?? 'mixed'", "'light'")],
  ['Tanara Main Window.dc.html', 'main-dark', (s) => s.replace("this.props.theme ?? 'mixed'", "'dark'")],
  ['Tanara Transcript Editor.dc.html', 'editor', (s) => s],
];

const port = 9333 + Math.floor(Math.random() * 500);
const chrome = spawn('google-chrome', [
  '--headless=new', '--no-sandbox', '--allow-file-access-from-files', '--disable-gpu', '--hide-scrollbars',
  `--user-data-dir=${path.join(tmp, 'profile')}`, `--remote-debugging-port=${port}`,
  '--window-size=1600,1000', 'about:blank',
], { stdio: 'ignore' });

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
async function connect() {
  for (let i = 0; i < 50; ++i) {
    try {
      const list = await (await fetch(`http://127.0.0.1:${port}/json`)).json();
      const page = list.find((t) => t.type === 'page');
      if (page) return page.webSocketDebuggerUrl;
    } catch { /* még nem él */ }
    await sleep(200);
  }
  throw new Error('a Chrome nem indult el');
}

const ws = new WebSocket(await connect());
await new Promise((r) => (ws.onopen = r));
let seq = 0;
const pending = new Map();
ws.onmessage = (ev) => {
  const m = JSON.parse(ev.data);
  if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); }
};
const send = (method, params = {}) => new Promise((res, rej) => {
  const id = ++seq;
  pending.set(id, (m) => (m.error ? rej(new Error(method + ': ' + m.error.message)) : res(m.result)));
  ws.send(JSON.stringify({ id, method, params }));
});
const evaluate = async (expr) =>
  (await send('Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise: true })).result.value;

// A Main Window-ban a kétváltozós data-screen-label attribútumot a sablon-motor eldobja,
// ezért ott a képernyőket a méretükről (1280×820) ismerjük fel; a címke a fölöttük lévő sor.
const SEL = '[data-screen-label], div[style*="width: 1280px"][style*="height: 820px"]';
const slug = (s) => s.normalize('NFD').replace(/[̀-ͯ]/g, '').toLowerCase()
  .replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '').slice(0, 60);

await send('Page.enable');
for (const [file, prefix, patch] of jobs) {
  const copy = path.join(tmp, prefix + '.html');
  fs.writeFileSync(copy, patch(fs.readFileSync(path.join(designDir, file), 'utf8')));
  await send('Page.navigate', { url: 'file://' + copy });
  // A sablon-motor aszinkron renderel: megvárjuk, míg megjelennek a képernyők.
  for (let i = 0; i < 60; ++i) {
    await sleep(500);
    if (await evaluate(`document.querySelectorAll('${SEL}').length`) > 0) break;
  }
  await evaluate('document.fonts.ready.then(() => true)');
  await sleep(1500);
  // A Visual Language-ben csak az 1b irány érvényes: az 1a alatti képernyőket kihagyjuk.
  const rects = await evaluate(`Array.from(document.querySelectorAll('${SEL}')).map((e) => {
    const r = e.getBoundingClientRect();
    const cap = e.previousElementSibling ? Array.from(e.previousElementSibling.children).slice(0, 2).map((c) => c.textContent).join(' ') : 'screen';
    return { label: e.getAttribute('data-screen-label') || cap, skip: !!e.closest('[id="1a"]'),
             x: r.left + scrollX, y: r.top + scrollY, w: r.width, h: r.height };
  })`);
  for (const r of rects) {
    if (r.skip || r.w < 10 || r.h < 10) continue;
    const shot = await send('Page.captureScreenshot', {
      format: 'png', captureBeyondViewport: true,
      clip: { x: r.x, y: r.y, width: r.w, height: r.h, scale: 1 },
    });
    const out = path.join(here, `${prefix}--${slug(r.label)}.png`);
    fs.writeFileSync(out, Buffer.from(shot.data, 'base64'));
    console.log(path.basename(out), `${Math.round(r.w)}x${Math.round(r.h)}`);
  }
}
ws.close();
chrome.kill();
await sleep(300);
fs.rmSync(tmp, { recursive: true, force: true });
