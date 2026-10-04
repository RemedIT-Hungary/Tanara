#!/usr/bin/env node
// Bírálati csomag: egy kör kimenetei megbeszélésenként, VAK azonosítókkal (a bíráló nem látja,
// melyik modell / prompt írta). Használat: node bundle.mjs <tag> [modell-szűrő regex]
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
const HERE = path.dirname(fileURLToPath(import.meta.url));
const tag = process.argv[2] || 'r2';
const filter = process.argv[3] ? new RegExp(process.argv[3]) : null;
const dir = path.join(HERE, '.local', 'runs', tag);
const out = path.join(HERE, '.local', 'judge', tag);
fs.mkdirSync(out, { recursive: true });
const rows = fs.readdirSync(dir).filter((f) => f.endsWith('.json') && !f.startsWith('_'))
  .map((f) => JSON.parse(fs.readFileSync(path.join(dir, f), 'utf8')))
  .filter((r) => !filter || filter.test(r.model));
const meetings = [...new Set(rows.map((r) => r.meeting))];
for (const m of meetings) {
  const mine = rows.filter((r) => r.meeting === m && (r.text || '').trim().length > 0);
  // determinisztikus, de nem sorrendtartó keverés
  mine.sort((a, b) => (hash(a.model + a.prompt) - hash(b.model + b.prompt)));
  const outputs = [], map = {};
  mine.forEach((r, i) => {
    const id = m + String(i + 1).padStart(2, '0');
    map[id] = { model: r.model, prompt: r.prompt, secs: r.secs, completionTokens: r.completionTokens, valid: r.valid, finish: r.finish };
    // A bíráló azt kapja, amit a felhasználó látna: az értelmezett szerkezetet (a munkavázlat
    // "topics" kulcs nélkül), vagy ha nem értelmezhető, a nyers szöveget.
    let shown;
    if (r.valid && r.parsed) { const { topics, ...rest } = r.parsed; shown = rest; }
    else shown = { unparseable: true, raw: r.text.slice(0, 6000) };
    outputs.push({ id, output: shown });
  });
  fs.writeFileSync(path.join(out, m + '.outputs.json'), JSON.stringify(outputs, null, 2));
  fs.writeFileSync(path.join(out, m + '.map.json'), JSON.stringify(map, null, 2));
  console.log(m, outputs.length, 'kimenet →', path.join(out, m + '.outputs.json'));
}
function hash(s) { let h = 2166136261; for (const c of s) { h ^= c.charCodeAt(0); h = Math.imul(h, 16777619); } return h >>> 0; }
