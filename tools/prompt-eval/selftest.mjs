#!/usr/bin/env node
// A forrás-jelölők (`[t=mm:ss]`) értelmezésének és a jelölős promptoknak az önellenőrzése —
// LLM és LM Studio NÉLKÜL (kézzel írt, hamis modell-válaszokkal). Használat: node selftest.mjs
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { extractMarkers, memoSourceStats, parseTopics, sentences, sourceStats, strictTime } from './sources.mjs';
import { builtinPrompt } from './builtin.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
let n = 0;
const t = (name, fn) => { fn(); ++n; console.log('ok', name); };

const transcript = [
  '`[00:00]` **Ádám** Az árról beszéljünk.',
  '`[01:05]` **Béla** Száz euró a javaslat.',
  '`[02:00]` **Ádám** Két helyszínen indul a pilot.',
  '`[05:00]` **Béla** Küldök ajánlatot péntekig.',
].join('\n\n');

t('jelölő a mondat után és előtt', () => {
  assert.deepEqual(extractMarkers('Az ár 100 euró. [t=01:05]'), { text: 'Az ár 100 euró.', refs: [{ start: 65000, end: 65000 }] });
  assert.equal(extractMarkers('Az ár 100 euró [t=1:05].').text, 'Az ár 100 euró.');
});
t('több jelölő, tartomány, id, csupasz idő', () => {
  assert.equal(extractMarkers('X. [t=02:00, t=05:00]').refs.length, 2);
  assert.deepEqual(extractMarkers('X. [t=02:00-02:40]').refs, [{ start: 120000, end: 160000 }]);
  assert.deepEqual(extractMarkers('X. [u120000]').refs, [{ id: 'u120000' }]);
  assert.deepEqual(extractMarkers('X. [12:30]').refs, [{ start: 750000, end: 750000 }]);
});
t('rossz idő: a jelölő lekerül, hivatkozás nincs', () => {
  for (const s of ['X. [t=12:7]', 'X. [t=12:75]', 'X. [t=abc]', 'X. [t=]']) {
    const r = extractMarkers(s);
    assert.equal(r.text, 'X.');
    assert.equal(r.refs.length, 0, s);
  }
  assert.equal(strictTime('1:61:00'), -1);
});
t('mondatokra bontás', () => {
  assert.deepEqual(sentences('Egy. [t=01:00] Kettő [t=02:00]. Három'), ['Egy. [t=01:00]', 'Kettő [t=02:00].', 'Három']);
});
t('lefedettség hamis válaszon (tűrés ±5 s)', () => {
  const parsed = {
    execSummary: 'Az árról volt szó. [t=00:03] A pilot két helyszínen indul. [t=02:00] Forrás nélküli mondat.',
    decisions: ['Két helyszín lesz. [t=02:01]', 'Kitalált idő. [t=40:00]'],
    actionItems: [{ text: 'Ajánlat küldése [t=05:02]', owner: 'Béla', due: 'péntek' }],
  };
  assert.deepEqual(sourceStats(parsed, transcript), { items: 6, marked: 5, resolved: 4, coverage: 0.67 });
  // A nyitott kérdések is állítások (v4).
  assert.deepEqual(sourceStats({ ...parsed, openQuestions: ['Mennyi az ár? [t=01:06]', 'Kérdés jelölő nélkül'] }, transcript),
    { items: 8, marked: 6, resolved: 5, coverage: 0.63 });
  assert.equal(sourceStats(null, transcript), null);
});
t('a jelölős promptok kérik a jelölőt', () => {
  for (const id of ['en7', 'red3']) {
    const p = fs.readFileSync(path.join(HERE, 'prompts', id + '.md'), 'utf8');
    assert.ok(p.includes('SOURCE MARKERS'), id);
    assert.ok(p.includes('[t=mm:ss]'), id);
  }
});
t('a beépített promptok (PromptLibrary.cpp) is kérik a jelölőt', () => {
  const cpp = fs.readFileSync(path.join(HERE, '..', '..', 'core', 'src', 'PromptLibrary.cpp'), 'utf8');
  assert.ok(cpp.includes('const char* const kSourceMarkers = R"PROMPT(SOURCE MARKERS'));
  assert.equal((cpp.match(/%SOURCE_MARKERS%/g) || []).length, 3);   // merge + single + a csere
});
// ---- memó (TOPICS): bevezető, pontok, alszakaszok — hamis jegyzet-válasszal ----
const notes = [
  'TOPICS',
  '### [00:00-02:30] Árazás',
  'Az árról egyeztettek. [t=00:03] Béla száz eurót javasolt. [t=01:05]',
  '- A javaslat száz euró. [t=01:05]',
  '#### Arguments',
  '- Ádám szerint drága. [t=02:00]',
  '- Tördelt',
  '  pont jelölő nélkül.',
  '#### Unresolved',
  'Ki dönt az árról? [t=40:00]',
  '### [05:00-05:30] Ajánlat',
  '- Béla péntekig küldi. [t=05:01]',
  'DECISIONS',
  '- none',
  'OPEN',
  '- [01:05] Ár?',
].join('\n');
t('memó-jegyzet szerkezete (bevezető, pontok, alszakaszok)', () => {
  const topics = parseTopics(notes);
  assert.equal(topics.length, 2);
  assert.equal(topics[0].intro, 'Az árról egyeztettek. [t=00:03] Béla száz eurót javasolt. [t=01:05]');
  assert.deepEqual(topics[0].points.map((p) => p.group), ['', 'arguments', 'arguments', 'open']);
  assert.equal(topics[0].points[2].text, 'Tördelt pont jelölő nélkül.');
  assert.equal(topics[1].points.length, 1);
});
t('memó forrás-lefedettség (kitalált idő nem oldható fel)', () => {
  assert.deepEqual(memoSourceStats(notes, transcript),
    { topics: 2, items: 7, marked: 6, resolved: 5, coverage: 0.71, groups: { arguments: 2, open: 1, other: 0 } });
  assert.equal(memoSourceStats('', transcript).items, 0);
});
t('a beépített promptok összeállnak (@single, @notes, @merge)', () => {
  for (const id of ['single', 'notes', 'merge']) {
    const p = builtinPrompt(id, 'Hungarian');
    assert.ok(!/%[A-Z_]+%/.test(p), id + ': feloldatlan helyőrző');
    assert.ok(!p.includes('{{LANGUAGE}}'), id);
    assert.ok(p.includes('[t=mm:ss]'), id);
  }
  for (const id of ['single', 'notes']) {
    const p = builtinPrompt(id);
    assert.ok(p.includes('#### Arguments') && p.includes('#### Unresolved'), id);
    assert.ok(p.includes('Every gist sentence and every point ends with a source marker'), id);
  }
  assert.ok(builtinPrompt('merge').includes('every open question'));
  assert.throws(() => builtinPrompt('nincs'));
});
console.log(`${n} eset rendben`);
