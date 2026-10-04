#!/usr/bin/env node
// Bírálati pontok összesítése modell × prompt szerint. Használat: node report.mjs <tag> [S,M,L]
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
const HERE = path.dirname(fileURLToPath(import.meta.url));
const tag = process.argv[2] || 'r2';
const dir = path.join(HERE, '.local', 'judge', tag);
const meetings = (process.argv[3] || 'S,M,L').split(',').filter((m) => fs.existsSync(path.join(dir, m + '.scores.json')));
const rows = [];
for (const m of meetings) {
  const map = JSON.parse(fs.readFileSync(path.join(dir, m + '.map.json'), 'utf8'));
  for (const s of JSON.parse(fs.readFileSync(path.join(dir, m + '.scores.json'), 'utf8')))
    rows.push({ meeting: m, ...map[s.id], ...s });
}
const avg = (a) => (a.length ? a.reduce((x, y) => x + y, 0) / a.length : NaN);
const f = (n, d = 1) => (Number.isNaN(n) ? '–' : n.toFixed(d));
const short = (m) => m.replace('google/', '').replace('qwen/', '');
function table(keyFn, title) {
  const groups = new Map();
  for (const r of rows) { const k = keyFn(r); if (!groups.has(k)) groups.set(k, []); groups.get(k).push(r); }
  const out = [...groups.entries()].map(([k, g]) => ({
    k, n: g.length, overall: avg(g.map((r) => r.overall)),
    cov: avg(g.map((r) => r.topicCoverage)),
    dec: avg(g.map((r) => r.decisionsFound / Math.max(1, r.decisionsClearTotal))),
    act: avg(g.map((r) => r.actionsFound / Math.max(1, r.actionsClearTotal))),
    falseItems: avg(g.map((r) => r.falseDecisions + r.falseActions)),
    owner: avg(g.map((r) => r.ownerErrors)), hall: avg(g.map((r) => r.hallucinations)),
    garbled: avg(g.map((r) => r.garbled)), bad: g.filter((r) => !r.formatOk || r.language !== 'hu').length,
    secs: avg(g.map((r) => r.secs)),
  })).sort((a, b) => b.overall - a.overall);
  console.log(`\n## ${title}  (megbeszélések: ${meetings.join(', ')})`);
  console.log('csoport'.padEnd(34), 'n  össz  lefed  dönt  teendő  hamis  rosszfel  téves  zagyva  hibás  mp');
  for (const o of out)
    console.log(o.k.padEnd(34), String(o.n).padStart(2), f(o.overall).padStart(5), f(o.cov, 2).padStart(6), f(o.dec, 2).padStart(5),
      f(o.act, 2).padStart(6), f(o.falseItems).padStart(6), f(o.owner).padStart(8), f(o.hall).padStart(6), f(o.garbled).padStart(6),
      String(o.bad).padStart(6), f(o.secs, 0).padStart(4));
}
table((r) => short(r.model), 'Modell szerint');
table((r) => r.prompt, 'Prompt szerint');
table((r) => short(r.model) + ' · ' + r.prompt, 'Modell × prompt');
