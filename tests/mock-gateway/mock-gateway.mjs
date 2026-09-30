#!/usr/bin/env node
// Tanara mock-gateway — a Cloud-gateway API v1 szerződés (docs/cloud-gateway-api-v1.md)
// futtatható referenciája. Nincs függősége (Node 20+). A kliens ezzel fejleszthető és
// tesztelhető a valódi gateway nélkül.
//
//   node tests/mock-gateway/mock-gateway.mjs [--port 8300] [--balance 5000]
//        [--auto-approve] [--min-client 0.1.0]
//        [--soniox-key KEY]  (STT passthrough a valódi Sonioxra; enélkül KANNÁZOTT átirat)
//        [--llm http://localhost:1234/v1]  (LLM passthrough; ha nem elérhető → kannázott)
//
// Device-flow jóváhagyás: a kliens által kapott verification_uri (http://127.0.0.1:PORT/device?code=XXXX)
// böngészőben egy „Jóváhagyás” gombot mutat; --auto-approve mellett azonnal jóvá van hagyva.
import http from 'node:http';
import https from 'node:https';
import { randomBytes, randomUUID } from 'node:crypto';

const args = process.argv.slice(2);
const opt = (k, d) => { const i = args.indexOf(k); return i >= 0 ? args[i + 1] : d; };
const PORT = +opt('--port', 8300);
const MIN_CLIENT = opt('--min-client', '0.1.0');
const AUTO_APPROVE = args.includes('--auto-approve');
const SONIOX_KEY = opt('--soniox-key', process.env.MOCK_SONIOX_KEY || '');
const LLM_UPSTREAM = opt('--llm', process.env.MOCK_LLM_UPSTREAM || 'http://localhost:1234/v1');

// ---- állapot (memória) ------------------------------------------------------
const state = {
  balance: +opt('--balance', 5000),
  devices: new Map(),      // device_code -> {user_code, approved, api_key, created}
  keys: new Map(),         // api_key -> {email}
  files: new Map(),        // id -> {size, name, created}
  transcriptions: new Map(), // id -> {status, file_id, model, diarization, created, done_at}
  usage: [],
};
// Egy előre ismert kulcs a gyors kézi teszthez (X-Tanara-Client nélkül is megy).
state.keys.set('tk_test_123', { email: 'test@example.com' });

// ---- tarifa (KANNÁZOTT; a valódi gateway statisztikából becsül) ----------------
const TARIFF = {
  stt: { 'tanara/stt-accurate': 100, 'tanara/stt-fast': 20, 'soniox/stt-async-v5': 100, 'groq/whisper-large-v3-turbo': 20 }, // kredit / felvett óra
  llm: { 'tanara/summary-accurate': 60, 'tanara/summary-fast': 8, 'anthropic/claude-sonnet-4.5': 60, 'google/gemini-2.5-flash': 8 }, // kredit / óra meeting
};
const RESOLVE = { 'tanara/stt-accurate': 'soniox/stt-async-v5', 'tanara/stt-fast': 'groq/whisper-large-v3-turbo',
                  'tanara/summary-accurate': 'anthropic/claude-sonnet-4.5', 'tanara/summary-fast': 'google/gemini-2.5-flash' };

const MODELS = [
  m('tanara/stt-accurate',  'stt', { tier: 'accurate', virtual: true,  diarization: true,  languages: null, price: { per_hour_credits: 100 } }),
  m('tanara/stt-fast',      'stt', { tier: 'fast',     virtual: true,  diarization: false, languages: null, hidden_for_languages: ['hu'], price: { per_hour_credits: 20 } }),
  m('soniox/stt-async-v5',  'stt', { tier: null, virtual: false, diarization: true,  languages: null, price: { per_hour_credits: 100 } }),
  m('groq/whisper-large-v3-turbo', 'stt', { tier: null, virtual: false, diarization: false, languages: null, hidden_for_languages: ['hu'], price: { per_hour_credits: 20 } }),
  m('tanara/summary-accurate', 'llm', { tier: 'accurate', virtual: true,  price: { per_1k_tokens_credits: 0.6 } }),
  m('tanara/summary-fast',     'llm', { tier: 'fast',     virtual: true,  price: { per_1k_tokens_credits: 0.08 } }),
  m('anthropic/claude-sonnet-4.5', 'llm', { tier: null, virtual: false, price: { per_1k_tokens_credits: 0.6 } }),
  m('google/gemini-2.5-flash',     'llm', { tier: null, virtual: false, price: { per_1k_tokens_credits: 0.08 } }),
];
function m(id, kind, tanara) { return { id, object: 'model', owned_by: 'tanara', tanara: { kind, ...tanara } }; }

// ---- segédek ---------------------------------------------------------------
const semverLt = (a, b) => { const p = s => String(s).split('.').map(n => parseInt(n, 10) || 0); const [x, y] = [p(a), p(b)];
  for (let i = 0; i < 3; i++) { if ((x[i] || 0) !== (y[i] || 0)) return (x[i] || 0) < (y[i] || 0); } return false; };
const json = (res, code, body, extra = {}) => { res.writeHead(code, { 'content-type': 'application/json', 'x-tanara-min-client': MIN_CLIENT, ...extra }); res.end(JSON.stringify(body)); };
const err = (res, code, ecode, extra = {}) => json(res, code, { error: { code: ecode, message: ecode, ...extra } });
const readBody = req => new Promise(r => { const c = []; req.on('data', d => c.push(d)); req.on('end', () => r(Buffer.concat(c))); });
const readJson = async req => { try { return JSON.parse((await readBody(req)).toString('utf8') || '{}'); } catch { return {}; } };
const auth = req => { const h = req.headers.authorization || ''; const k = h.startsWith('Bearer ') ? h.slice(7) : ''; return state.keys.has(k) ? k : null; };
const log = (...a) => console.log(new Date().toISOString().slice(11, 19), ...a);

function charge(kind, model, hours, note) {
  const perHour = (TARIFF[kind][model] ?? TARIFF[kind][RESOLVE[model]] ?? 50);
  const credits = Math.max(1, Math.round(perHour * hours));
  if (state.balance < credits) return { ok: false, credits };
  state.balance -= credits; state.usage.push({ at: Date.now(), kind, model, hours, credits, note });
  log(`  ↳ terhelés ${credits} kredit (${kind} ${model}, ${hours.toFixed(2)} h) → egyenleg ${state.balance}`);
  return { ok: true, credits };
}

// ---- kannázott átirat (magyar, diarizált, ~20 s) -----------------------------
function cannedTokens(diarization) {
  const words = [['Sziasztok,', 1], ['kezdjük', 1], ['a', 1], ['megbeszélést.', 1], ['Rendben,', 2], ['nálam', 2], ['a', 2], ['MuseumPlus', 2],
    ['migráció', 2], ['a', 2], ['téma.', 2], ['Döntés:', 1], ['jövő', 1], ['héten', 1], ['szállítunk.', 1], ['Teendő:', 2], ['Dompa', 2], ['megírja', 2], ['a', 2], ['tesztet.', 2]];
  let t = 400; return words.map(([w, sp]) => { const d = 300 + w.length * 45; const tok = { text: (t > 400 ? ' ' : '') + w, start_ms: t, end_ms: t + d, confidence: 0.94 + Math.random() * 0.05 };
    if (diarization) tok.speaker = sp; t += d + 120; return tok; });
}

// ---- passthrough segéd (Soniox / LLM) ---------------------------------------
function proxy(req, res, base, headers, bodyBuf) {
  const u = new URL(base + req.url.replace(/^\/v1/, ''));
  const lib = u.protocol === 'https:' ? https : http;
  const p = lib.request(u, { method: req.method, headers: { ...req.headers, host: u.host, ...headers } }, up => {
    const h = { ...up.headers, 'x-tanara-min-client': MIN_CLIENT }; delete h['transfer-encoding'];
    res.writeHead(up.statusCode, h); up.pipe(res);
  });
  p.on('error', e => err(res, 502, 'upstream_error', { detail: String(e) }));
  if (bodyBuf) p.end(bodyBuf); else req.pipe(p);
}

// ---- router ------------------------------------------------------------------
const server = http.createServer(async (req, res) => {
  const url = new URL(req.url, `http://127.0.0.1:${PORT}`);
  const path = url.pathname;
  log(req.method, path);

  // Min-client kapu (csak ha a kliens küld verziót)
  const cv = (req.headers['x-tanara-client'] || '').split('/')[0];
  if (cv && semverLt(cv, MIN_CLIENT)) return err(res, 426, 'client_too_old', { min_client: MIN_CLIENT });

  // --- device-flow ---
  if (req.method === 'POST' && path === '/v1/auth/device/code') {
    const device_code = randomBytes(16).toString('hex'); const user_code = randomBytes(2).toString('hex').toUpperCase();
    state.devices.set(device_code, { user_code, approved: AUTO_APPROVE, api_key: null, created: Date.now() });
    return json(res, 200, { device_code, user_code, verification_uri: `http://127.0.0.1:${PORT}/device?code=${user_code}`, interval: 2, expires_in: 600 });
  }
  if (req.method === 'POST' && path === '/v1/auth/device/token') {
    const { device_code } = await readJson(req); const d = state.devices.get(device_code);
    if (!d) return err(res, 400, 'invalid_device_code');
    if (Date.now() - d.created > 600_000) return err(res, 400, 'expired_token');
    if (!d.approved) return err(res, 400, 'authorization_pending');
    if (!d.api_key) { d.api_key = 'tk_' + randomBytes(12).toString('hex'); state.keys.set(d.api_key, { email: 'adam@example.com' }); }
    return json(res, 200, { api_key: d.api_key, email: 'adam@example.com' });
  }
  if (path === '/device') {   // böngészős jóváhagyó oldal
    const code = url.searchParams.get('code'); const dev = [...state.devices.values()].find(d => d.user_code === code);
    if (req.method === 'POST' && dev) { dev.approved = true; res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' }); return res.end('<h2>Jóváhagyva ✓</h2><p>Visszatérhetsz a Tanarába.</p>'); }
    res.writeHead(dev ? 200 : 404, { 'content-type': 'text/html; charset=utf-8' });
    return res.end(dev ? `<h2>Tanara Cloud (mock)</h2><p>Eszköz-kód: <b>${code}</b></p><form method=post><button>Jóváhagyás</button></form>` : '<h2>Ismeretlen kód</h2>');
  }

  // --- innentől auth kell ---
  const key = auth(req);
  if (!key) return err(res, 401, 'unauthorized');

  if (req.method === 'GET' && path === '/v1/account')
    return json(res, 200, { email: state.keys.get(key).email, balance_credits: state.balance, dashboard_url: `http://127.0.0.1:${PORT}/dashboard` });
  if (req.method === 'GET' && path === '/v1/models') {
    // LLM-upstream modelljei is bekerülnek Expert-hez (ha elérhető) — best-effort, rövid timeout.
    let extra = [];
    try { extra = await fetchUpstreamModels(); } catch {}
    return json(res, 200, { object: 'list', data: [...MODELS, ...extra] });
  }
  if (req.method === 'POST' && path === '/v1/estimate') {
    const b = await readJson(req); const hours = Math.max(0.01, (+b.duration_ms || 0) / 3_600_000);
    const breakdown = [];
    if (b.task === 'transcribe' || b.task === 'both') breakdown.push({ item: 'stt', model: b.stt_model, credits: Math.round((TARIFF.stt[b.stt_model] ?? 100) * hours) });
    if (b.task === 'summarize' || b.task === 'both')  breakdown.push({ item: 'llm', model: b.llm_model, credits: Math.round((TARIFF.llm[b.llm_model] ?? 60) * hours) });
    const est = breakdown.reduce((s, x) => s + x.credits, 0);
    return json(res, 200, { credits_estimate: est, credits_low: Math.round(est * 0.8), credits_high: Math.round(est * 1.3), balance_credits: state.balance, breakdown, enough: state.balance >= est });
  }

  // --- STT passthrough (Soniox-alak) ---
  if (SONIOX_KEY && (path.startsWith('/v1/files') || path.startsWith('/v1/transcriptions'))) {
    if (req.method === 'POST' && path === '/v1/transcriptions') {   // virtuális modell feloldása + terhelés
      const b = await readJson(req); b.model = RESOLVE[b.model] ? 'stt-async-v5' : (b.model.replace(/^soniox\//, '') || 'stt-async-v5');
      const c = charge('stt', 'tanara/stt-accurate', 0.5, 'passthrough'); if (!c.ok) return err(res, 402, 'insufficient_credit', { balance: state.balance, needed: c.credits, topup_url: `http://127.0.0.1:${PORT}/dashboard` });
      const buf = Buffer.from(JSON.stringify(b)); return proxy(req, res, 'https://api.soniox.com/v1', { authorization: `Bearer ${SONIOX_KEY}`, 'content-length': buf.length }, buf);
    }
    return proxy(req, res, 'https://api.soniox.com/v1', { authorization: `Bearer ${SONIOX_KEY}` });
  }
  // --- STT kannázott ---
  if (req.method === 'POST' && path === '/v1/files') {
    const body = await readBody(req); const id = 'file_' + randomUUID().slice(0, 8);
    state.files.set(id, { size: body.length, created: Date.now() }); return json(res, 200, { id, size: body.length });
  }
  if (req.method === 'POST' && path === '/v1/transcriptions') {
    const b = await readJson(req); if (!state.files.has(b.file_id)) return err(res, 400, 'unknown_file');
    const hours = Math.max(0.05, state.files.get(b.file_id).size / 8_000_000);   // ~64 kbps mp3 → óra-becslés
    const c = charge('stt', b.model || 'tanara/stt-accurate', hours, 'canned'); if (!c.ok) return err(res, 402, 'insufficient_credit', { balance: state.balance, needed: c.credits, topup_url: `http://127.0.0.1:${PORT}/dashboard` });
    const id = 'tr_' + randomUUID().slice(0, 8); state.transcriptions.set(id, { status: 'queued', file_id: b.file_id, diarization: !!b.enable_speaker_diarization, created: Date.now() });
    return json(res, 200, { id, status: 'queued' });
  }
  let mm;
  if (req.method === 'GET' && (mm = path.match(/^\/v1\/transcriptions\/([^/]+)$/))) {
    const t = state.transcriptions.get(mm[1]); if (!t) return err(res, 404, 'not_found');
    const age = Date.now() - t.created; t.status = age < 1500 ? 'queued' : age < 4000 ? 'processing' : 'completed';
    return json(res, 200, { id: mm[1], status: t.status, error_message: '' });
  }
  if (req.method === 'GET' && (mm = path.match(/^\/v1\/transcriptions\/([^/]+)\/transcript$/))) {
    const t = state.transcriptions.get(mm[1]); if (!t) return err(res, 404, 'not_found');
    return json(res, 200, { id: mm[1], tokens: cannedTokens(t.diarization) });
  }
  if (req.method === 'DELETE' && (mm = path.match(/^\/v1\/(files|transcriptions)\/([^/]+)$/))) {
    (mm[1] === 'files' ? state.files : state.transcriptions).delete(mm[2]); res.writeHead(204, { 'x-tanara-min-client': MIN_CLIENT }); return res.end();
  }

  // --- LLM ---
  if (req.method === 'POST' && path === '/v1/chat/completions') {
    const body = await readBody(req); let b = {}; try { b = JSON.parse(body.toString('utf8')); } catch {}
    const c = charge('llm', b.model || 'tanara/summary-accurate', 0.5, 'chat'); if (!c.ok) return err(res, 402, 'insufficient_credit', { balance: state.balance, needed: c.credits, topup_url: `http://127.0.0.1:${PORT}/dashboard` });
    const upstreamOk = await fetchUpstreamModels().then(() => true).catch(() => false);
    if (upstreamOk) { b.model = resolveLlm(b.model); const buf = Buffer.from(JSON.stringify(b)); return proxy(req, res, LLM_UPSTREAM, { 'content-length': buf.length, authorization: 'Bearer lm-studio' }, buf); }
    // kannázott JSON-összefoglaló (a SummaryService JSON-sémája szerint)
    const content = JSON.stringify({ execSummary: 'A csapat a MuseumPlus migrációt egyeztette; a szállítás jövő hétre került.', decisions: ['Jövő héten szállítunk.'], actionItems: [{ text: 'Teszt megírása', owner: 'Dompa', due: '' }], participants: ['Ádám', 'Tamás'] });
    return json(res, 200, { id: 'chatcmpl_mock', object: 'chat.completion', model: b.model, choices: [{ index: 0, message: { role: 'assistant', content }, finish_reason: 'stop' }], usage: { prompt_tokens: 1200, completion_tokens: 90, total_tokens: 1290 } });
  }
  if (path === '/dashboard') { res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' }); return res.end(`<h2>Tanara Cloud dashboard (mock)</h2><p>Egyenleg: <b>${state.balance}</b> kredit</p><form method=post action=/topup><button>+5000 kredit</button></form><pre>${JSON.stringify(state.usage, null, 1)}</pre>`); }
  if (req.method === 'POST' && path === '/topup') { state.balance += 5000; res.writeHead(302, { location: '/dashboard' }); return res.end(); }
  return err(res, 404, 'not_found');
});

function resolveLlm(model) { const r = RESOLVE[model] || model; return r.startsWith('tanara/') ? 'google/gemma-4-12b' : r.includes('/') && !r.startsWith('google/gemma') ? 'google/gemma-4-12b' : r; }
function fetchUpstreamModels() {
  return new Promise((resolve, reject) => {
    const r = http.get(LLM_UPSTREAM + '/models', { timeout: 800 }, up => { let d = ''; up.on('data', c => d += c); up.on('end', () => { try { resolve((JSON.parse(d).data || []).map(x => ({ id: x.id, object: 'model', owned_by: 'local', tanara: { kind: 'llm', tier: null, virtual: false, price: { per_1k_tokens_credits: 0.05 } } }))); } catch (e) { reject(e); } }); });
    r.on('error', reject); r.on('timeout', () => { r.destroy(); reject(new Error('timeout')); });
  });
}

server.listen(PORT, '127.0.0.1', () => log(`mock-gateway: http://127.0.0.1:${PORT}/v1  egyenleg=${state.balance}  min-client=${MIN_CLIENT}  STT=${SONIOX_KEY ? 'soniox-passthrough' : 'kannázott'}  LLM=${LLM_UPSTREAM} (fallback: kannázott)  auto-approve=${AUTO_APPROVE}  teszt-kulcs=tk_test_123`));
