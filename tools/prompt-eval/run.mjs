#!/usr/bin/env node
// Prompt-kiértékelő: összefoglaló-promptok × lokális modellek × átiratok, LM Studio-n át.
//
//   node run.mjs --models google/gemma-4-12b-qat,qwen/qwen3-coder-30b \
//                --prompts hu0,en1,en2 --meetings S,M,L [--ctx 49152] [--max-tokens 8000]
//                [--temp 0.2] [--lang Hungarian] [--tag r1] [--force]
//
// A modelleket EGYMÁS UTÁN tölti be (lms load --parallel 1), futás után kiüríti a GPU-t.
// Bemenet: .local/data/<id>.md (átirat) + .local/data/<id>.ctx (kontextus-jegyzet, opcionális).
// Kimenet: .local/runs/<tag>/<modell>__<prompt>__<meeting>.json  (+ összesítő a végén).
// A .local/ privát adat (valódi átiratok) — nincs verziókövetve.
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const arg = (name, def) => {
  const i = process.argv.indexOf('--' + name);
  return i >= 0 ? process.argv[i + 1] : def;
};
const flag = (name) => process.argv.includes('--' + name);
const list = (s) => String(s || '').split(',').map((x) => x.trim()).filter(Boolean);

const BASE = arg('base', 'http://localhost:1234/v1');
const MODELS = list(arg('models', 'google/gemma-4-12b-qat'));
const PROMPTS = list(arg('prompts', 'hu0,en1'));
const MEETINGS = list(arg('meetings', 'S'));
const CTX = Number(arg('ctx', 49152));
const MAX_TOKENS = Number(arg('max-tokens', 8000));
const TEMP = Number(arg('temp', 0.2));
const LANG = arg('lang', 'Hungarian');
const TAG = arg('tag', 'r1');
const FORCE = flag('force');
// A modell „gondolkodása": none = kikapcsolva (reasoning_effort: "none"), default = a modell alapja.
const REASONING = arg('reasoning', 'none');
const PAUSE_MS = Number(arg('pause', 8)) * 1000;   // szünet a futások között (GPU kímélése)
const MAX_RETRY = 0;                               // NINCS újrapróbálás: egy modell-összeomlás = GPU-reset
// A modell GPU-ra tett hányada (lms load --gpu). Üres = az LM Studio dönt (ez tele tudja tölteni
// a kártyát, és akkor az asztal éhezik) — nagy modellnél adj meg pl. 0.8-at.
const GPU_RATIO = arg('gpu-ratio', '');
const DATA = path.join(HERE, '.local', 'data');
const OUT = path.join(HERE, '.local', 'runs', TAG);
fs.mkdirSync(OUT, { recursive: true });

const sh = (cmd, args, opts = {}) => {
  try {
    return execFileSync(cmd, args, { encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'], ...opts });
  } catch (e) {
    return (e.stdout || '') + (e.stderr || '') + `\n[exit ${e.status}]`;
  }
};
const safe = (s) => s.replace(/[^a-zA-Z0-9._-]+/g, '_');

// A felhasználói üzenet ugyanúgy épül, mint az appban (SummaryService::buildUserPrompt);
// az angol promptokhoz angol címkével.
function userPrompt(meeting, english) {
  const transcript = fs.readFileSync(path.join(DATA, meeting + '.md'), 'utf8').trim();
  const ctxFile = path.join(DATA, meeting + '.ctx');
  const ctx = fs.existsSync(ctxFile) ? fs.readFileSync(ctxFile, 'utf8').trim() : '';
  let out = '';
  if (ctx) out += (english ? 'Context / notes:\n' : 'Kontextus / jegyzetek:\n') + ctx + '\n\n';
  return out + '----\n' + transcript;
}

// Megengedő JSON-kinyerés, mint az appban (kódkerítés le, első { … utolsó }).
function extractJson(raw) {
  let s = raw.trim();
  if (s.startsWith('```')) {
    const nl = s.indexOf('\n');
    if (nl >= 0) s = s.slice(nl + 1);
    const f = s.lastIndexOf('```');
    if (f >= 0) s = s.slice(0, f);
    s = s.trim();
  }
  if (!s.startsWith('{')) {
    const a = s.indexOf('{'), b = s.lastIndexOf('}');
    if (a >= 0 && b > a) s = s.slice(a, b + 1);
  }
  return s;
}

function parseMarkdown(raw) {
  const sec = (h) => {
    const m = raw.match(new RegExp('^##\\s*' + h + '\\s*\\n([\\s\\S]*?)(?=^##\\s|\\s*$(?![\\s\\S]))', 'm'));
    return m ? m[1].trim() : null;
  };
  const lines = (t) => (t || '').split('\n').map((l) => l.replace(/^[-*]\s*(\[.\]\s*)?/, '').trim())
    .filter((l) => l && l !== '–' && l !== '-');
  const exec = sec('Vezetői összefoglaló'), dec = sec('Döntések'), act = sec('Teendők'), part = sec('Résztvevők');
  if (exec === null || dec === null || act === null || part === null) return null;
  return {
    execSummary: exec,
    decisions: lines(dec),
    actionItems: lines(act).map((l) => {
      const m = l.match(/^(.*?)(?:\s+—\s+(.*?))?(?:\s*\(([^()]*)\))?$/);
      return { text: (m && m[1]) || l, owner: (m && m[2]) || '', due: (m && m[3]) || '' };
    }),
    participants: lines(part).join(', ').split(',').map((x) => x.trim()).filter(Boolean),
  };
}

function analyse(promptId, text) {
  const md = promptId.endsWith('md');
  let parsed = null, error = '';
  if (md) {
    parsed = parseMarkdown(text);
    if (!parsed) error = 'missing section';
  } else {
    try { parsed = JSON.parse(extractJson(text)); } catch (e) { error = String(e.message).slice(0, 80); }
  }
  const ok = !!parsed && typeof parsed.execSummary === 'string' && parsed.execSummary.length > 0
    && Array.isArray(parsed.decisions) && Array.isArray(parsed.actionItems);
  return { ok, error, parsed };
}

async function chat(model, system, user, maxTokens = MAX_TOKENS) {
  const t0 = Date.now();
  const res = await fetch(BASE + '/chat/completions', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json', Authorization: 'Bearer lm-studio' },
    body: JSON.stringify({
      model, temperature: TEMP, max_tokens: maxTokens, stream: false,
      // A gondolkodás kikapcsolása modellcsaládonként más: a Gemma a reasoning_effort: "none"-t
      // érti; a Qwen 3.x-nél csak az üres <think> blokkal előtöltött asszisztens-üzenet hat.
      ...(REASONING === 'default' || /qwen/i.test(model) ? {} : { reasoning_effort: REASONING }),
      messages: [{ role: 'system', content: system }, { role: 'user', content: user },
        ...(REASONING === 'none' && /qwen/i.test(model) ? [{ role: 'assistant', content: '<think>\n\n</think>\n\n' }] : [])],
    }),
    signal: AbortSignal.timeout(30 * 60 * 1000),
  });
  const body = await res.json().catch(() => ({}));
  const secs = (Date.now() - t0) / 1000;
  const ch = (body.choices || [])[0] || {};
  return {
    httpStatus: res.status, secs,
    text: (ch.message && ch.message.content) || '',
    reasoning: (ch.message && (ch.message.reasoning_content || ch.message.reasoning)) || '',
    finish: ch.finish_reason || '', usage: body.usage || {}, stats: body.stats || {},
    error: body.error ? JSON.stringify(body.error).slice(0, 300) : '',
  };
}

// ---- Darabolt futás: az átiratot ~N perces részekre vágjuk, részenként jegyzet (map), majd a
// jegyzetekből végső összefoglaló (reduce). Prompt-azonosító: "chunk:<map>:<reduce>[:perc]".
const CHUNK_MIN_DEFAULT = Number(arg('chunk-min', 15));
function splitTranscript(text, minutes) {
  const paras = text.split(/\n\s*\n/).map((p) => p.trim()).filter(Boolean);
  const tsOf = (p) => { const m = p.match(/^`\[(?:(\d+):)?(\d+):(\d+)\]`/); return m ? (Number(m[1] || 0) * 3600 + Number(m[2]) * 60 + Number(m[3])) : null; };
  const chunks = []; let cur = [], start = 0;
  for (const p of paras) {
    const t = tsOf(p);
    if (t !== null && cur.length && t - start >= minutes * 60) { chunks.push(cur.join('\n\n')); cur = []; start = t; }
    if (!cur.length && t !== null) start = t;
    cur.push(p);
  }
  if (cur.length) chunks.push(cur.join('\n\n'));
  // egy nagyon rövid záró darabot az előzőhöz csapunk
  if (chunks.length > 1 && chunks[chunks.length - 1].length < 1500) { const last = chunks.pop(); chunks[chunks.length - 1] += '\n\n' + last; }
  return chunks;
}
async function chatChunked(model, spec, meeting, remind = false) {
  const [, mapId, redId, minStr] = spec.split(':');
  const minutes = Number(minStr || CHUNK_MIN_DEFAULT);
  const load = (id) => fs.readFileSync(path.join(HERE, 'prompts', id + '.md'), 'utf8').trim().replaceAll('{{NYELV}}', LANG);
  const transcript = fs.readFileSync(path.join(DATA, meeting + '.md'), 'utf8').trim();
  const ctxFile = path.join(DATA, meeting + '.ctx');
  const ctx = fs.existsSync(ctxFile) ? fs.readFileSync(ctxFile, 'utf8').trim() : '';
  const speakers = [...new Set([...transcript.matchAll(/\*\*(.+?)\*\*/g)].map((m) => m[1]))];
  const chunks = splitTranscript(transcript, minutes);
  const agg = { httpStatus: 200, secs: 0, text: '', reasoning: '', finish: 'stop', usage: { prompt_tokens: 0, completion_tokens: 0 }, stats: {}, error: '', notes: [] };
  for (let i = 0; i < chunks.length; ++i) {
    const user = (ctx ? 'Context / notes:\n' + ctx + '\n\n' : '') + `Part ${i + 1} of ${chunks.length}\n----\n` + chunks[i] + (remind ? REMINDER : '');
    const r = await chat(model, load(mapId), user, 1500);
    agg.secs += r.secs; agg.usage.prompt_tokens += r.usage.prompt_tokens || 0; agg.usage.completion_tokens += r.usage.completion_tokens || 0;
    agg.reasoning += r.reasoning;
    if (r.httpStatus !== 200 || r.error || !r.finish) return { ...agg, httpStatus: r.httpStatus, error: r.error || 'map step failed', finish: r.finish };
    agg.notes.push(`=== PART ${i + 1} of ${chunks.length} ===\n` + r.text.trim());
    await new Promise((res) => setTimeout(res, 4000));
  }
  const user = (ctx ? 'Context / notes:\n' + ctx + '\n\n' : '') + 'Speakers: ' + speakers.join(', ') + '\n\n' + agg.notes.join('\n\n') + (remind ? REMINDER : '');
  const r = await chat(model, load(redId), user, MAX_TOKENS);
  agg.secs += r.secs; agg.usage.prompt_tokens += r.usage.prompt_tokens || 0; agg.usage.completion_tokens += r.usage.completion_tokens || 0;
  agg.reasoning += r.reasoning;
  return { ...agg, httpStatus: r.httpStatus, error: r.error, finish: r.finish, text: r.text, chunks: chunks.length };
}

// Nyelvi emlékeztető az átirat után (a legutolsó, amit a modell olvas).
const REMINDER = '\n\n----\nReminder: write the output in Hungarian (magyarul), whatever the language of these instructions.';

const gpu = () => sh('rocm-smi', ['-u', '--showmeminfo', 'vram', 'gtt']).split('\n')
  .filter((l) => /GPU use|Used Memory/.test(l)).map((l) => l.replace(/\s+/g, ' ').trim()).join(' | ');

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
// VRAM / GTT bájtban (rocm-smi). A GTT növekedése = a modell kilóg a VRAM-ból a rendszermemóriába.
const mem = () => {
  const t = sh('rocm-smi', ['--showmeminfo', 'vram', 'gtt']);
  const n = (re) => { const m = t.match(re); return m ? Number(m[1]) : 0; };
  return { vramTotal: n(/VRAM Total Memory \(B\): (\d+)/), vram: n(/VRAM Total Used Memory \(B\): (\d+)/),
           gtt: n(/GTT Total Used Memory \(B\): (\d+)/) };
};
const GB = 1024 ** 3;
const noFit = new Set();   // modellek, amelyek ezzel a kontextussal nem férnek a VRAM-ba
const clean = (t) => t.replace(/\x1b\[[0-9;?]*[a-zA-Z]/g, '').replace(/\r/g, '\n');
const loaded = () => { try { return JSON.parse(sh('lms', ['ps', '--json'])); } catch { return null; } };
const serverUp = async () => { try { return (await fetch(BASE + '/models', { signal: AbortSignal.timeout(4000) })).ok; } catch { return false; } };

// A modell CSAK általunk, --parallel 1-gyel és a kért kontextussal betöltve futhat. Ha az LM Studio
// (pl. GPU-reset után) magától töltötte újra más beállítással, kiürítjük és újratöltjük.
async function ensureLoaded(model) {
  for (let attempt = 1; attempt <= 3; ++attempt) {
    if (!(await serverUp())) {
      console.log('  LM Studio szerver nem válaszol — indítás…');
      sh('lms', ['server', 'start']);
      await sleep(8000);
    }
    const ps = loaded();
    if (ps && ps.length === 1 && ps[0].modelKey === model && ps[0].parallel === 1 && ps[0].contextLength === CTX)
      return true;
    if (ps && ps.length) console.log('  nem a mi betöltésünk van bent → ürítés:', ps.map((m) => `${m.modelKey} ctx ${m.contextLength} par ${m.parallel}`).join(', '));
    sh('lms', ['unload', '--all']);
    await sleep(5000);
    const before = mem();
    const est = clean(sh('lms', ['load', model, '-c', String(CTX), '--parallel', '1', '--estimate-only', '-y']))
      .split('\n').filter((l) => /Estimated GPU/.test(l)).join(' ').trim();
    const t0 = Date.now();
    const out = sh('lms', ['load', model, '-c', String(CTX), '--parallel', '1', ...(GPU_RATIO ? ['--gpu', GPU_RATIO] : []), '-y'], { timeout: 10 * 60 * 1000 });
    console.log(`  load #${attempt}: ${/\[exit/.test(out) ? clean(out).trim().split('\n').slice(-2).join(' ') : 'ok'} (${((Date.now() - t0) / 1000).toFixed(0)} s) ${est} | ${gpu()}`);
    await sleep(2000);
    // VRAM-őr: ha a kártya gyakorlatilag tele van, vagy a GTT megugrott, a modell kilóg → nem futtatjuk
    // (lassú és összeomlik). Az lms becslése ezt alábecsüli, ezért a tényleges foglalást nézzük.
    const after = mem();
    if (after.vramTotal && (after.vram > after.vramTotal - 1.2 * GB || after.gtt - before.gtt > 0.4 * GB)) {
      console.log(`  NEM FÉR BE: VRAM ${(after.vram / GB).toFixed(1)} / ${(after.vramTotal / GB).toFixed(1)} GiB, GTT +${((after.gtt - before.gtt) / GB).toFixed(2)} GiB — modell kihagyva (kisebb --ctx kell)`);
      sh('lms', ['unload', '--all']);
      await sleep(6000);
      noFit.add(model);
      return false;
    }
  }
  return false;
}

// GPU-reset őr: ha a kernel a futás kezdete óta GPU-resetet naplózott, AZONNAL leállunk és ürítünk
// (a reset után az asztal is megakadhat; nem próbálkozunk tovább).
const resetCount = () => (sh('journalctl', ['-k', '-b', '--no-pager']).match(/GPU reset begin/g) || []).length;
const resetsAtStart = resetCount();
const gpuWasReset = () => resetCount() > resetsAtStart;

const rows = [];
let failStreak = 0;
outer:
for (const model of MODELS) {
  const todo = [];
  for (const p of PROMPTS) for (const m of MEETINGS) {
    const file = path.join(OUT, `${safe(model)}__${safe(p)}__${m}.json`);
    if (FORCE || !fs.existsSync(file)) todo.push([p, m, file]);
    else rows.push(JSON.parse(fs.readFileSync(file, 'utf8')));
  }
  if (!todo.length) continue;
  console.log(`\n=== ${model} (ctx ${CTX}, reasoning ${REASONING}) ===`);

  for (const [p, m, file] of todo) {
    if (noFit.has(model)) break;
    const english = !p.startsWith('hu');
    const chunked = p.startsWith('chunk:');
    const remind = p.endsWith('+L');
    const pFile = remind ? p.slice(0, -2) : p;
    const system = chunked ? '' : fs.readFileSync(path.join(HERE, 'prompts', pFile + '.md'), 'utf8').trim()
      .replaceAll('{{NYELV}}', english ? LANG : 'magyar');
    let r = null, tries = 0;
    for (; tries <= MAX_RETRY; ++tries) {
      if (gpuWasReset()) break;
      if (noFit.has(model) || !(await ensureLoaded(model))) { r = { httpStatus: 0, secs: 0, text: '', reasoning: '', finish: 'noload', usage: {}, stats: {}, error: noFit.has(model) ? 'does not fit in VRAM at this context' : 'model could not be loaded' }; break; }
      try {
        r = chunked ? await chatChunked(model, pFile, m, remind)
          : await chat(model, system, userPrompt(m, english) + (remind ? REMINDER : ''));
      }
      catch (e) { r = { httpStatus: 0, secs: 0, text: '', reasoning: '', finish: 'exception', usage: {}, stats: {}, error: String(e).slice(0, 300) }; }
      if (r.httpStatus === 200 && !r.error && r.finish) break;
      // Összeomlás / GPU-reset: várunk, ürítünk, és tiszta betöltéssel újrapróbáljuk.
      console.log(`  hiba (${r.error || r.finish || r.httpStatus})`);
      if (tries >= MAX_RETRY) break;
      await sleep(25000);
      sh('lms', ['unload', '--all']);
      await sleep(8000);
    }
    if (gpuWasReset() || !r) {
      console.log('\nGPU-RESET a futás alatt — leállás, a GPU ürítése. Nem folytatom.');
      process.exitCode = 2;
      sh('lms', ['unload', '--all']);
      break outer;
    }
    const failed = !(r.httpStatus === 200 && !r.error && r.finish);
    if (failed && /crash/i.test(r.error || '')) {
      console.log('\nA modell összeomlott — leállás (nincs újrapróbálás), a GPU ürítése.');
      process.exitCode = 2;
      sh('lms', ['unload', '--all']);
      break outer;
    }
    if (noFit.has(model)) { failStreak = 0; break; }
    failStreak = failed ? failStreak + 1 : 0;
    const a = analyse(pFile, r.text);
    const row = {
      model, prompt: p, meeting: m, ctx: CTX, temp: TEMP, maxTokens: MAX_TOKENS, reasoningMode: REASONING,
      secs: Number(r.secs.toFixed(1)), finish: r.finish, httpStatus: r.httpStatus, error: r.error, retries: tries,
      promptTokens: r.usage.prompt_tokens || 0, completionTokens: r.usage.completion_tokens || 0,
      reasoningChars: r.reasoning.length, valid: a.ok, parseError: a.error,
      decisions: a.parsed && Array.isArray(a.parsed.decisions) ? a.parsed.decisions.length : -1,
      actionItems: a.parsed && Array.isArray(a.parsed.actionItems) ? a.parsed.actionItems.length : -1,
      execChars: a.parsed && a.parsed.execSummary ? a.parsed.execSummary.length : 0,
      text: r.text, parsed: a.parsed, chunks: r.chunks || 0, notes: r.notes || undefined,
    };
    if (!failed) fs.writeFileSync(file, JSON.stringify(row, null, 2));   // a hibás futás újrafuttatható marad
    rows.push(row);
    console.log(`${p.padEnd(6)} ${m}  ${String(row.secs).padStart(6)} s  in ${row.promptTokens} out ${row.completionTokens} think ${row.reasoningChars}  ${row.finish.padEnd(7)} valid=${row.valid} dec=${row.decisions} act=${row.actionItems} exec=${row.execChars}${row.error ? ' ERR ' + row.error : ''}${row.parseError ? ' parse: ' + row.parseError : ''}`);
    if (failStreak >= 3) { console.log('\n3 egymást követő hiba — leállás.'); break outer; }
    await sleep(PAUSE_MS);
  }
  sh('lms', ['unload', '--all']);
  await sleep(6000);
  console.log('gpu after unload:', gpu());
}
sh('lms', ['unload', '--all']);

// Összesítő (a szöveg nélkül) — a bírálat külön lépés.
const summary = rows.map(({ text, parsed, ...rest }) => rest);
fs.writeFileSync(path.join(OUT, '_summary.json'), JSON.stringify(summary, null, 2));
console.log(`\n${rows.length} futás → ${OUT}`);
