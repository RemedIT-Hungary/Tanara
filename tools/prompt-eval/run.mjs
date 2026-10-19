#!/usr/bin/env node
// Prompt evaluator: summary prompts x local models x transcripts, through LM Studio.
//
// Source-marker variants: en7 (single-pass), chunk:map1:red3 (chunked); the app's built-in
// prompts (core/src/PromptLibrary.cpp, no rebuild): @single, chunk:@notes:@merge; for these
// the source coverage of the memo (TOPICS: intro, points, #### Arguments / Unresolved) is
// measured too (memoSources). LLM-free self-check of the marker parser: node selftest.mjs.
//
//   node run.mjs --models google/gemma-4-12b-qat,qwen/qwen3-coder-30b \
//                --prompts hu0,en1,en2 --meetings S,M,L [--ctx 49152] [--max-tokens 8000]
//                [--temp 0.2] [--lang Hungarian] [--tag r1] [--force]
//
// Loads the models ONE AFTER ANOTHER (lms load --parallel 1) and empties the GPU after the run.
// Input: .local/data/<id>.md (transcript) + .local/data/<id>.ctx (context note, optional).
// Output: .local/runs/<tag>/<model>__<prompt>__<meeting>.json  (+ a summary at the end).
// .local/ is private data (real transcripts); it is not version controlled.
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { memoSourceStats, sourceStats } from './sources.mjs';
import { builtinPrompt } from './builtin.mjs';

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
// The model's "thinking": none = off (reasoning_effort: "none"), default = the model's own setting.
const REASONING = arg('reasoning', 'none');
const PAUSE_MS = Number(arg('pause', 8)) * 1000;   // pause between runs (spares the GPU)
const MAX_RETRY = 0;                               // NO retry: a model crash = a GPU reset
// The fraction of the model put on the GPU (lms load --gpu). Empty = LM Studio decides (it can
// fill the card completely, starving the desktop); for a big model give e.g. 0.8.
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

// The user message is built the same way as in the app (SummaryService::buildUserPrompt);
// with an English label for the English prompts.
function userPrompt(meeting, english) {
  const transcript = fs.readFileSync(path.join(DATA, meeting + '.md'), 'utf8').trim();
  const ctxFile = path.join(DATA, meeting + '.ctx');
  const ctx = fs.existsSync(ctxFile) ? fs.readFileSync(ctxFile, 'utf8').trim() : '';
  let out = '';
  if (ctx) out += (english ? 'Context / notes:\n' : 'Kontextus / jegyzetek:\n') + ctx + '\n\n';
  return out + '----\n' + transcript;
}

// Lenient JSON extraction, as in the app (strip the code fence, first { ... last }).
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
      // Turning thinking off differs by model family: Gemma understands reasoning_effort: "none";
      // for Qwen 3.x only an assistant message prefilled with an empty <think> block has an effect.
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

// ---- Chunked run: the transcript is cut into ~N-minute parts, notes per part (map), then a
// final summary from the notes (reduce). Prompt id: "chunk:<map>:<reduce>[:minutes]".
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
  // a very short final chunk is merged into the previous one
  if (chunks.length > 1 && chunks[chunks.length - 1].length < 1500) { const last = chunks.pop(); chunks[chunks.length - 1] += '\n\n' + last; }
  return chunks;
}
async function chatChunked(model, spec, meeting, remind = false) {
  const [, mapId, redId, minStr] = spec.split(':');
  const minutes = Number(minStr || CHUNK_MIN_DEFAULT);
  const load = (id) => promptText(id, LANG);
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
    await coolDown();
  }
  const user = (ctx ? 'Context / notes:\n' + ctx + '\n\n' : '') + 'Speakers: ' + speakers.join(', ') + '\n\n' + agg.notes.join('\n\n') + (remind ? REMINDER : '');
  const r = await chat(model, load(redId), user, MAX_TOKENS);
  agg.secs += r.secs; agg.usage.prompt_tokens += r.usage.prompt_tokens || 0; agg.usage.completion_tokens += r.usage.completion_tokens || 0;
  agg.reasoning += r.reasoning;
  return { ...agg, httpStatus: r.httpStatus, error: r.error, finish: r.finish, text: r.text, chunks: chunks.length };
}

// A prompt's text: "@<id>" = the app's built-in prompt, otherwise prompts/<id>.md.
function promptText(id, lang) {
  if (id.startsWith('@')) return builtinPrompt(id.slice(1), lang).trim();
  return fs.readFileSync(path.join(HERE, 'prompts', id + '.md'), 'utf8').trim().replaceAll('{{NYELV}}', lang);
}

// The memo note from the output: for single-pass the part before SUMMARY, for chunked the part notes.
function memoNotes(r) {
  if (Array.isArray(r.notes) && r.notes.length) return r.notes.join('\n\n');
  const text = String(r.text || '');
  if (!/^\s*(#+\s*)?TOPICS\b/m.test(text)) return '';
  const cut = text.search(/^\s*(#+\s*)?SUMMARY\b|^\s*\{/m);
  return cut >= 0 ? text.slice(0, cut) : text;
}

// Language reminder after the transcript (the very last thing the model reads).
const REMINDER = '\n\n----\nReminder: write the output in Hungarian (magyarul), whatever the language of these instructions.';

const gpu = () => sh('rocm-smi', ['-u', '--showmeminfo', 'vram', 'gtt']).split('\n')
  .filter((l) => /GPU use|Used Memory/.test(l)).map((l) => l.replace(/\s+/g, ' ').trim()).join(' | ');

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
// VRAM / GTT in bytes (rocm-smi). GTT growth means the model spills out of VRAM into system memory.
const mem = () => {
  const t = sh('rocm-smi', ['--showmeminfo', 'vram', 'gtt']);
  const n = (re) => { const m = t.match(re); return m ? Number(m[1]) : 0; };
  return { vramTotal: n(/VRAM Total Memory \(B\): (\d+)/), vram: n(/VRAM Total Used Memory \(B\): (\d+)/),
           gtt: n(/GTT Total Used Memory \(B\): (\d+)/) };
};
const GB = 1024 ** 3;
const noFit = new Set();   // models that do not fit into VRAM with this context
const clean = (t) => t.replace(/\x1b\[[0-9;?]*[a-zA-Z]/g, '').replace(/\r/g, '\n');
const loaded = () => { try { return JSON.parse(sh('lms', ['ps', '--json'])); } catch { return null; } };
const serverUp = async () => { try { return (await fetch(BASE + '/models', { signal: AbortSignal.timeout(4000) })).ok; } catch { return false; } };

// The model may run ONLY if loaded by us, with --parallel 1 and the requested context. If LM
// Studio reloaded it by itself with other settings (e.g. after a GPU reset), we unload and reload it.
async function ensureLoaded(model) {
  for (let attempt = 1; attempt <= 3; ++attempt) {
    if (!(await serverUp())) {
      console.log('  LM Studio server not responding — starting it…');
      sh('lms', ['server', 'start']);
      await sleep(8000);
    }
    const ps = loaded();
    if (ps && ps.length === 1 && ps[0].modelKey === model && ps[0].parallel === 1 && ps[0].contextLength === CTX)
      return true;
    if (ps && ps.length) console.log('  a load that is not ours is in memory → unloading:', ps.map((m) => `${m.modelKey} ctx ${m.contextLength} par ${m.parallel}`).join(', '));
    sh('lms', ['unload', '--all']);
    await sleep(5000);
    const before = mem();
    const est = clean(sh('lms', ['load', model, '-c', String(CTX), '--parallel', '1', '--estimate-only', '-y']))
      .split('\n').filter((l) => /Estimated GPU/.test(l)).join(' ').trim();
    const t0 = Date.now();
    const out = sh('lms', ['load', model, '-c', String(CTX), '--parallel', '1', ...(GPU_RATIO ? ['--gpu', GPU_RATIO] : []), '-y'], { timeout: 10 * 60 * 1000 });
    console.log(`  load #${attempt}: ${/\[exit/.test(out) ? clean(out).trim().split('\n').slice(-2).join(' ') : 'ok'} (${((Date.now() - t0) / 1000).toFixed(0)} s) ${est} | ${gpu()}`);
    await sleep(2000);
    // VRAM guard: if the card is practically full, or GTT jumped, the model spills over, so we do
    // not run it (slow, and it crashes). lms underestimates this, so we look at the actual allocation.
    const after = mem();
    if (after.vramTotal && (after.vram > after.vramTotal - 1.2 * GB || after.gtt - before.gtt > 0.4 * GB)) {
      console.log(`  DOES NOT FIT: VRAM ${(after.vram / GB).toFixed(1)} / ${(after.vramTotal / GB).toFixed(1)} GiB, GTT +${((after.gtt - before.gtt) / GB).toFixed(2)} GiB — model skipped (needs a smaller --ctx)`);
      sh('lms', ['unload', '--all']);
      await sleep(6000);
      noFit.add(model);
      return false;
    }
  }
  return false;
}

// GPU reset guard: if the kernel logged a GPU reset since the run started, we stop and unload
// IMMEDIATELY (after a reset the desktop may stall too; we do not keep trying).
const resetCount = () => (sh('journalctl', ['-k', '-b', '--no-pager']).match(/GPU reset begin/g) || []).length;
const resetsAtStart = resetCount();
const gpuWasReset = () => resetCount() > resetsAtStart;

// Thermal watch: under load the card's memory approaches the critical limit (108 C). Before
// every request we wait for it to cool down, so long rounds do not sit at the limit either.
const MEM_HOT = Number(arg('mem-hot', 102)), MEM_COOL = Number(arg('mem-cool', 95));
const memTemp = () => { const m = sh('rocm-smi', ['-t']).match(/Sensor memory\) \(C\): ([\d.]+)/); return m ? Number(m[1]) : 0; };
async function coolDown() {
  let t = memTemp();
  if (t < MEM_HOT) return;
  const t0 = Date.now();
  while (t > MEM_COOL && Date.now() - t0 < 6 * 60 * 1000) { await sleep(10000); t = memTemp(); }
  console.log(`  (cool-down pause: ${((Date.now() - t0) / 1000).toFixed(0)} s, memory now ${t} °C)`);
}

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
    const system = chunked ? '' : promptText(pFile, english ? LANG : 'magyar');
    let r = null, tries = 0;
    for (; tries <= MAX_RETRY; ++tries) {
      if (gpuWasReset()) break;
      await coolDown();
      if (noFit.has(model) || !(await ensureLoaded(model))) { r = { httpStatus: 0, secs: 0, text: '', reasoning: '', finish: 'noload', usage: {}, stats: {}, error: noFit.has(model) ? 'does not fit in VRAM at this context' : 'model could not be loaded' }; break; }
      try {
        r = chunked ? await chatChunked(model, pFile, m, remind)
          : await chat(model, system, userPrompt(m, english) + (remind ? REMINDER : ''));
      }
      catch (e) { r = { httpStatus: 0, secs: 0, text: '', reasoning: '', finish: 'exception', usage: {}, stats: {}, error: String(e).slice(0, 300) }; }
      if (r.httpStatus === 200 && !r.error && r.finish) break;
      // Crash / GPU reset: we wait, unload, and retry with a clean load.
      console.log(`  hiba (${r.error || r.finish || r.httpStatus})`);
      if (tries >= MAX_RETRY) break;
      await sleep(25000);
      sh('lms', ['unload', '--all']);
      await sleep(8000);
    }
    if (gpuWasReset() || !r) {
      console.log('\nGPU RESET during the run — stopping, unloading the GPU. Not continuing.');
      process.exitCode = 2;
      sh('lms', ['unload', '--all']);
      break outer;
    }
    const failed = !(r.httpStatus === 200 && !r.error && r.finish);
    if (failed && /crash/i.test(r.error || '')) {
      console.log('\nThe model crashed — stopping (no retry), unloading the GPU.');
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
      // Source markers (`[t=mm:ss]`): marked / resolvable / total claims (en7, red3 prompts).
      sources: sourceStats(a.parsed, fs.readFileSync(path.join(DATA, m + '.md'), 'utf8')),
      // The memo's (TOPICS) markers: intro sentences + points, subsections (if the output has notes).
      memoSources: memoNotes(r) ? memoSourceStats(memoNotes(r), fs.readFileSync(path.join(DATA, m + '.md'), 'utf8')) : null,
      text: r.text, parsed: a.parsed, chunks: r.chunks || 0, notes: r.notes || undefined,
    };
    if (!failed) fs.writeFileSync(file, JSON.stringify(row, null, 2));   // a failed run stays re-runnable
    rows.push(row);
    console.log(`${p.padEnd(6)} ${m}  ${String(row.secs).padStart(6)} s  in ${row.promptTokens} out ${row.completionTokens} think ${row.reasoningChars}  ${row.finish.padEnd(7)} valid=${row.valid} dec=${row.decisions} act=${row.actionItems} exec=${row.execChars}${row.sources && row.sources.marked ? ` src=${row.sources.resolved}/${row.sources.items}` : ''}${row.error ? ' ERR ' + row.error : ''}${row.parseError ? ' parse: ' + row.parseError : ''}`);
    if (failStreak >= 3) { console.log('\n3 failures in a row — stopping.'); break outer; }
    await sleep(PAUSE_MS);
  }
  sh('lms', ['unload', '--all']);
  await sleep(6000);
  console.log('gpu after unload:', gpu());
}
sh('lms', ['unload', '--all']);

// Summary (without the text); the review is a separate step.
const summary = rows.map(({ text, parsed, ...rest }) => rest);
fs.writeFileSync(path.join(OUT, '_summary.json'), JSON.stringify(summary, null, 2));
console.log(`\n${rows.length} runs → ${OUT}`);
