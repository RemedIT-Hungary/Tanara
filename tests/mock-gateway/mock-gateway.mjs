#!/usr/bin/env node
// Tanara mock-gateway: a runnable reference of the Tanara Cloud gateway contract
// (docs/cloud-gateway-api.yaml, 1.2.0). No dependencies (Node 20+). The client can be
// developed and tested with it without the real gateway.
//
//   node tests/mock-gateway/mock-gateway.mjs [--port 8300] [--auto-approve]
//
// State switches (at startup; at run time: POST /__mock/config {key: value}):
//   --balance 10.00            net balance in USD (the ledger is net; the client sees it per vat_mode)
//   --vat-mode gross|reverse_charge   (default: gross = net x 1.27)
//   --low-threshold 2.00       low-balance threshold (net USD)
//   --min-client 0.1.0         an older X-Tanara-Client gets 426
//   --terms-pending            new terms announced (effective within 14 days): preliminary dialog
//   --terms-required           new terms in effect, not accepted: 403 on the processing calls
//   --maintenance              503 maintenance (with a window) on the processing calls
//   --upstream-down            503 upstream_unavailable on the processing calls
//   --suspended admin|payment_dispute   403 account_suspended
//   --rate-limited             429 rate_limited (Retry-After: 30) on the processing calls
//   --spend-limit              429 spend_limit_reached on the processing calls
//   --internal-error           500 internal_error on the processing calls
//   --refund                   the transcription ends with an upstream error: automatic write-back
//   --fail-chat-after N        503 upstream_unavailable after N successful chat calls (partial failure)
//   --deny                     the "user" denies the device flow (access_denied)
//   --topup-available          online top-up exists (default: none: 409 / "Write to us")
//   --notice "text" [--notice-level info|warning|critical]
//   --trial awaiting_email|awaiting_card|granted|denied_card_used|unavailable
//   --soniox-key KEY           STT passthrough to the real Soniox (without it: CANNED transcript)
//   --llm http://localhost:1234/v1 | off   LLM passthrough (unreachable / off: canned)
//
// Capability endpoints (x-status: draft in the contract; deterministic fake results):
//   POST /v1/embeddings          OpenAI shape; the vector is derived from the text's hash
//   POST /v1/voice/embeddings    multipart (file + model); the vector is derived from the audio bytes' hash
//   POST /v1/voice/identify      vectors + candidates (the client's, as opaque refs) -> best match per probe
//   POST /v1/summaries           asynchronous: 202 + id; GET /v1/summaries/{id} is `processing` on the
//                                first poll and `completed` (canned result) from the second
//   POST /v1/tags/suggest        picks the candidates whose name occurs in the title / summary; #Word = new
//   GET  /v1/models?capability=  the catalog filtered by capability
// Error branches: 401 (no key), 402 insufficient_balance, 422 invalid_input (empty / malformed input),
// and the usual --rate-limited / --maintenance ... switches.
//
// Waitlist (POST /v1/waitlist): 202; a disposable domain (e.g. @mailinator.com) gets 400
// disposable_email; the --rate-limited / --maintenance / --upstream-down / --internal-error
// switches apply here too.
//
// Device-flow approval: verification_uri_complete (http://127.0.0.1:PORT/device?code=XXXX-XXXX)
// shows "Approve" / "Deny" buttons in a browser; with --auto-approve it is approved at once.
// Test key: tk_test_123 (works without X-Tanara-Client too).
import http from 'node:http';
import https from 'node:https';
import { createHash, randomBytes, randomUUID } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { writeFileSync, unlinkSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const args = process.argv.slice(2);
const opt = (k, d) => { const i = args.indexOf(k); return i >= 0 && i + 1 < args.length ? args[i + 1] : d; };
const flag = k => args.includes(k);
const PORT = +opt('--port', 8300);
const BASE = `http://127.0.0.1:${PORT}`;
const USD = 1_000_000;                       // 1 USD = 1 000 000 micros
const VAT_RATE = 0.27;

// ---- switches (can also be set at run time: POST /__mock/config) ----
const cfg = {
  vat_mode: opt('--vat-mode', 'gross'),
  low_threshold: Math.round(+opt('--low-threshold', 2) * USD),
  min_client: opt('--min-client', '0.1.0'),
  auto_approve: flag('--auto-approve'),
  deny: flag('--deny'),
  terms_pending: flag('--terms-pending'),
  terms_required: flag('--terms-required'),
  maintenance: flag('--maintenance'),
  upstream_down: flag('--upstream-down'),
  suspended: opt('--suspended', null),        // null | 'admin' | 'payment_dispute'
  rate_limited: flag('--rate-limited'),
  spend_limit: flag('--spend-limit'),
  internal_error: flag('--internal-error'),
  refund: flag('--refund'),
  fail_chat_after: opt('--fail-chat-after', null) === null ? null : +opt('--fail-chat-after'),
  topup_available: flag('--topup-available'),
  notice: opt('--notice', null),
  notice_level: opt('--notice-level', 'info'),
  trial: opt('--trial', 'granted'),
};
const SONIOX_KEY = opt('--soniox-key', process.env.MOCK_SONIOX_KEY || '');
const LLM_UPSTREAM = opt('--llm', process.env.MOCK_LLM_UPSTREAM || 'http://localhost:1234/v1');

// ---- state (memory) ----
const TERMS = { accepted: '2026-06-01', current: '2026-06-01', upcoming: '2026-11-01' };
const state = {
  balance: Math.round(+opt('--balance', 10) * USD),   // NET micros
  low_since: null,
  terms_accepted: TERMS.accepted,
  devices: new Map(),        // device_code -> {user_code, approved, denied, cancelled, api_key, created, info}
  keys: new Map(),           // api_key -> {email, key_id}
  files: new Map(),          // id -> {size, duration_ms, upstream_id?}
  transcriptions: new Map(), // id -> {status, file_id, model, diarization, created, charge, refunded, content_deleted, job_id, upstream_id?}
  usage: [],                 // {at, kind, model, amount, job_id, summary_mode, request_id, refunded}
  chat_ok: 0,                // number of successful chat calls (--fail-chat-after)
  waitlist: [],              // POST /v1/waitlist sign-ups
  summaries: new Map(),      // id -> {mode, polls, job_id, charge, created}
};
state.keys.set('tk_test_123', { email: 'test@example.com', key_id: 'key_test' });

const TEXT_DIM = 64, VOICE_DIM = 192;

// ---- catalog (NET prices; converted on output per vat_mode) ----
// STT: hourly rate. LLM: per 1M tokens (the gateway multiplier is already included).
const MODELS = [
  model('tanara/stt-accurate', 'stt', 'accurate', true, false, 'Pontos átírás', { per_hour: 0.315 }, { diarization: true }),
  model('tanara/stt-fast', 'stt', 'fast', true, false, 'Gyors átírás', { per_hour: 0.063 }, { diarization: false, hidden_for_languages: ['hu'] }),
  model('soniox/stt-async-v5', 'stt', null, false, true, 'Soniox v5', { per_hour: 0.315 }, { diarization: true }),
  model('groq/whisper-large-v3', 'stt', null, false, true, 'Whisper large v3 (Groq)', { per_hour: 0.094 }, { diarization: false }),
  model('groq/whisper-large-v3-turbo', 'stt', null, false, true, 'Whisper v3 turbo (Groq)', { per_hour: 0.063 }, { diarization: false, hidden_for_languages: ['hu'] }),
  model('tanara/summary-accurate', 'llm', 'accurate', true, false, 'Pontos összefoglaló', { in: 9.0, out: 45.0 }),
  model('tanara/summary-fast', 'llm', 'fast', true, false, 'Gyors összefoglaló', { in: 0.9, out: 7.5 }),
  model('anthropic/claude-sonnet-4.5', 'llm', null, false, true, 'Claude Sonnet 4.5', { in: 9.0, out: 45.0 }),
  model('google/gemini-2.5-flash', 'llm', null, false, true, 'Gemini 2.5 Flash', { in: 0.9, out: 7.5 }),
  model('tanara/text-embedding', 'text_embedding', 'accurate', true, false, 'Szöveg-beágyazás', { in: 0.02, out: 0 }, { dim: TEXT_DIM }),
  model('tanara/voice-embedding', 'voice_embedding', 'accurate', true, false, 'Hang-beágyazás', { per_hour: 0.05 }, { dim: VOICE_DIM }),
];
// GET /v1/models?capability=: the model kinds a capability uses.
const CAPABILITY_KINDS = { transcription: ['stt'], chat: ['llm'], summarization: ['llm'], 'tag-suggestion': ['llm'],
  'text-embedding': ['text_embedding'], 'voice-embedding': ['voice_embedding'], 'participant-identification': ['voice_embedding'] };
const RESOLVE = { 'tanara/stt-accurate': 'soniox/stt-async-v5', 'tanara/stt-fast': 'groq/whisper-large-v3-turbo',
                  'tanara/summary-accurate': 'anthropic/claude-sonnet-4.5', 'tanara/summary-fast': 'google/gemini-2.5-flash' };
function model(id, kind, tier, virtual, expert, display_name, price, extra = {}) {
  return { id, kind, tier, virtual, expert, display_name, price, languages: null, hidden_for_languages: [], ...extra };
}
const findModel = id => MODELS.find(m => m.id === id);
const priceOf = m => m.price.per_hour !== undefined ? 'hour' : 'tokens';
const sttModel = id => { const m = findModel(id); return m && m.kind === 'stt' ? m : findModel('tanara/stt-accurate'); };
const llmModel = id => { const m = findModel(id); return m && m.kind === 'llm' ? m : findModel('tanara/summary-accurate'); };

// ---- money ----
const disp = net => cfg.vat_mode === 'reverse_charge' ? net : Math.round(net * (1 + VAT_RATE));  // net to displayed
const money = net => ({ amount_micros: disp(net), currency: 'USD' });
const usdNet = x => Math.round(x * USD);
function modelJson(m) {
  const price = priceOf(m) === 'hour' ? { per_hour: money(usdNet(m.price.per_hour)) }
    : { per_1m_input_tokens: money(usdNet(m.price.in)), per_1m_output_tokens: money(usdNet(m.price.out)) };
  const t = { kind: m.kind, tier: m.tier, virtual: m.virtual, expert: m.expert, display_name: m.display_name, languages: m.languages, hidden_for_languages: m.hidden_for_languages, price };
  if (m.kind === 'stt') t.diarization = !!m.diarization;
  if (m.dim) t.dim = m.dim;
  return { id: m.id, object: 'model', owned_by: 'tanara', tanara: t };
}
const sttCharge = (m, duration_ms) => Math.round(usdNet(m.price.per_hour) * duration_ms / 3_600_000);
const llmCost = (m, inTok, outTok) => Math.round((usdNet(m.price.in) * inTok + usdNet(m.price.out) * outTok) / 1_000_000);
const lowBalance = () => state.balance < cfg.low_threshold;
function bookBalanceChange() {
  if (lowBalance()) { if (!state.low_since) state.low_since = new Date().toISOString(); } else state.low_since = null;
}

// ---- texts (Accept-Language) ----
const MSG = {
  unauthorized: ['Hiányzó, érvénytelen vagy visszavont API-kulcs.', 'Missing, invalid or revoked API key.'],
  account_suspended: ['A fiók fel van függesztve.', 'The account is suspended.'],
  terms_acceptance_required: ['A feldolgozáshoz el kell fogadnod az új ÁSZF-et.', 'Accept the new Terms to continue processing.'],
  client_too_old: ['A kliens túl régi, frissítés szükséges.', 'The client is too old, an update is required.'],
  rate_limited: ['Túl sok kérés érkezett.', 'Too many requests.'],
  spend_limit_reached: ['Elérted ennek az eszköznek a költési limitjét.', 'This device reached its spending limit.'],
  maintenance: ['Tervezett karbantartás.', 'Planned maintenance.'],
  upstream_unavailable: ['A feldolgozó szolgáltatás átmenetileg nem elérhető.', 'The processing service is temporarily unavailable.'],
  internal_error: ['Váratlan hiba történt.', 'An unexpected error occurred.'],
  insufficient_balance: ['Nincs elég egyenleg.', 'Not enough balance.'],
  validation_error: ['Érvénytelen kérés.', 'Invalid request.'],
  not_found: ['Nem található.', 'Not found.'],
  authorization_pending: ['Jóváhagyásra vár.', 'Waiting for approval.'],
  access_denied: ['A kapcsolódást elutasították.', 'The request was denied.'],
  expired_token: ['A kód lejárt.', 'The code expired.'],
  invalid_device_code: ['Ismeretlen eszköz-kód.', 'Unknown device code.'],
  topup_unavailable: ['Online feltöltés még nincs, írj nekünk.', 'There is no online top-up yet, contact us.'],
  not_completed: ['Az átírás még nem készült el.', 'The transcription is not completed yet.'],
  invalid_input: ['A bemenet nem feldolgozható.', 'The input cannot be processed.'],
  disposable_email: ['Eldobható e-mail címmel nem lehet feliratkozni.', 'Disposable e-mail addresses are not accepted.'],
  terms_summary: ['Új visszatérítési szabályok; pontosított adatkezelés.', 'New refund rules; clarified data handling.'],
};
const lang = req => ((req.headers['accept-language'] || 'en').toLowerCase().startsWith('hu') ? 0 : 1);
const text = (req, code) => (MSG[code] || [code, code])[lang(req)];

// ---- helpers ----
const semverLt = (a, b) => { const p = s => String(s).split('.').map(n => parseInt(n, 10) || 0); const [x, y] = [p(a), p(b)];
  for (let i = 0; i < 3; i++) { if ((x[i] || 0) !== (y[i] || 0)) return (x[i] || 0) < (y[i] || 0); } return false; };
const newRequestId = () => 'req_' + randomBytes(5).toString('hex').toUpperCase();
const baseHeaders = req => ({ 'x-tanara-min-client': cfg.min_client, 'x-tanara-request-id': req.requestId });
function json(req, res, code, body, extra = {}) {
  res.writeHead(code, { 'content-type': 'application/json; charset=utf-8', ...baseHeaders(req), ...extra });
  res.end(JSON.stringify(body));
}
function noContent(req, res) { res.writeHead(204, baseHeaders(req)); res.end(); }
function err(req, res, status, code, extra = {}, headers = {}) {
  log(`  ↳ ${status} ${code} (${req.requestId})`);
  json(req, res, status, { error: { code, message: text(req, code), request_id: req.requestId,
    support_url: `${BASE}/support?request_id=${req.requestId}`, ...extra } }, headers);
}
function chargeHeaders(net) {
  return { 'x-tanara-charge-micros': String(disp(net)), 'x-tanara-balance-micros': String(disp(state.balance)), 'x-tanara-currency': 'USD' };
}
function chargeInfo(chargeId, net, jobId) {
  return { charge_id: chargeId, charge: money(net), balance: money(state.balance), low_balance: lowBalance(),
           balance_empty: state.balance <= 0, vat_mode: cfg.vat_mode, job_id: jobId || null };
}
const readBody = req => new Promise(r => { const c = []; req.on('data', d => c.push(d)); req.on('end', () => r(Buffer.concat(c))); });
const parseJson = buf => { try { return JSON.parse(buf.toString('utf8') || '{}'); } catch { return null; } };
const auth = req => { const h = req.headers.authorization || ''; const k = h.startsWith('Bearer ') ? h.slice(7) : ''; return state.keys.has(k) ? k : null; };
const log = (...a) => console.log(new Date().toISOString().slice(11, 19), ...a);
const fmt = net => `$${(disp(net) / USD).toFixed(4)}`;

// Errors that block the processing calls (charge, estimate, top-up link) in one place.
function processingBlock(req, res, { topup = false } = {}) {
  if (cfg.suspended && !(topup && cfg.suspended === 'payment_dispute'))
    return err(req, res, 403, 'account_suspended', { suspension_reason: cfg.suspended }), true;
  if (cfg.terms_required && state.terms_accepted !== TERMS.upcoming)
    return err(req, res, 403, 'terms_acceptance_required', { terms_version: TERMS.upcoming, terms_url: `${BASE}/terms/${TERMS.upcoming}` }), true;
  if (topup) return false;
  if (cfg.rate_limited) return err(req, res, 429, 'rate_limited', { retry_after: 30 }, { 'retry-after': '30' }), true;
  if (cfg.spend_limit) {
    const resets = new Date(); resets.setUTCHours(24, 0, 0, 0);
    return err(req, res, 429, 'spend_limit_reached', { limit: { period: 'daily', amount: money(usdNet(5)), resets_at: resets.toISOString() },
      settings_url: `${BASE}/dashboard/keys?key_id=key_test` }), true;
  }
  if (cfg.maintenance) {
    const s = new Date(Date.now() - 5 * 60_000), e = new Date(Date.now() + 25 * 60_000);
    return err(req, res, 503, 'maintenance', { retry_after: 1500, window: { starts_at: s.toISOString(), ends_at: e.toISOString() } }, { 'retry-after': '1500' }), true;
  }
  if (cfg.upstream_down) return err(req, res, 503, 'upstream_unavailable', { retry_after: 60 }, { 'retry-after': '60' }), true;
  if (cfg.internal_error) return err(req, res, 500, 'internal_error'), true;
  return false;
}
function insufficient(req, res, neededNet, basis) {
  return err(req, res, 402, 'insufficient_balance', { balance: money(state.balance), needed: money(neededNet), needed_basis: basis,
    topup_url: cfg.topup_available ? `${BASE}/dashboard/topup` : null, contact_url: `${BASE}/contact` });
}

// Recording length: ffprobe if available; otherwise a size-based estimate (~128 kbps).
function measureDurationMs(buf) {
  const p = join(tmpdir(), `tanara-mock-${randomUUID()}.bin`);
  try { writeFileSync(p, buf);
    const out = execFileSync('ffprobe', ['-v', 'error', '-show_entries', 'format=duration', '-of', 'csv=p=0', p], { timeout: 5000 }).toString().trim();
    const s = parseFloat(out); if (s > 0) return Math.round(s * 1000);
  } catch { /* no ffprobe / not audio */ } finally { try { unlinkSync(p); } catch {} }
  return Math.max(1000, Math.round(buf.length / 16));
}
// Extracts the file part from the multipart body (simple, for a single-file upload).
function multipartFile(buf, ctype) {
  const m = /boundary=(?:"([^"]+)"|([^;]+))/.exec(ctype || ''); if (!m) return buf;
  const b = Buffer.from('--' + (m[1] || m[2]));
  const start = buf.indexOf(b); if (start < 0) return buf;
  const hdrEnd = buf.indexOf('\r\n\r\n', start); if (hdrEnd < 0) return buf;
  const end = buf.indexOf(b, hdrEnd); return buf.subarray(hdrEnd + 4, end > 0 ? end - 2 : buf.length);
}

// ---- canned transcript (Hungarian, diarized, ~20 s) ----
function cannedTokens(diarization) {
  const words = [['Sziasztok,', 1], ['kezdjük', 1], ['a', 1], ['megbeszélést.', 1], ['Rendben,', 2], ['nálam', 2], ['a', 2], ['Muzeo', 2],
    ['migráció', 2], ['a', 2], ['téma.', 2], ['Döntés:', 1], ['jövő', 1], ['héten', 1], ['szállítunk.', 1], ['Teendő:', 2], ['Dompa', 2], ['megírja', 2], ['a', 2], ['tesztet.', 2]];
  let t = 400; return words.map(([w, sp]) => { const d = 300 + w.length * 45; const tok = { text: (t > 400 ? ' ' : '') + w, start_ms: t, end_ms: t + d, confidence: 0.95 };
    if (diarization) tok.speaker = sp; t += d + 120; return tok; });
}
// Canned LLM reply by the request's shape (the client's parsers: JSON / topic markdown / analysis / reduce).
function cannedCompletion(b, summaryMode) {
  const user = (b.messages || []).filter(m => m.role === 'user').map(m => m.content).join('\n');
  // Quick summary, long meeting: a note per part ("PART k of n" at the start of the part).
  const part = /^PART (\d+) of (\d+)/m.exec(user);
  if (part)
    return `TOPICS\n### MuseumPlus migráció (${part[1]}. rész)\n- A migráció állapotát egyeztették.\n\nDECISIONS\n- Jövő héten szállítunk.\n\nOPEN\n- none\n\nACTIONS\n- Teszt megírása — Bence`;
  const topic = /(?:ELEMZENDŐ TÉMA|TOPIC TO ANALYSE): (.*)/.exec(user);
  if (topic) {
    const t = topic[1] || 'Téma';
    return `A(z) „${t}” témát a csapat röviden egyeztette.\n\n## Döntések\n- Jövő héten szállítunk.\n\n## Teendők\n- Teszt megírása — Bence`;
  }
  if (user.includes('Témánkénti elemzések:') || user.includes('Analyses per topic:'))
    return 'A csapat a Muzeo migrációt egyeztette; a szállítás jövő hétre került.\n\n## Teendők\n- Teszt megírása — Bence';
  if (summaryMode === 'complex')
    return '## Muzeo migráció\nA migráció állapota és a szállítás időpontja.\n\n## Tesztelés\nKi írja meg a teszteket.';
  return JSON.stringify({ execSummary: 'A csapat a MuseumPlus migrációt egyeztette; a szállítás jövő hétre került.', decisions: ['Jövő héten szállítunk.'],
    actionItems: [{ text: 'Teszt megírása', owner: 'Dompa', due: '' }], participants: ['Ádám', 'Tamás'] });
}

// ---- upstream (Soniox / LLM) helper ----
function upstream(method, url, headers, body, timeout = 120000) {
  return new Promise((resolve, reject) => {
    const u = new URL(url); const lib = u.protocol === 'https:' ? https : http;
    const r = lib.request(u, { method, headers: { ...headers, host: u.host }, timeout }, up => {
      const c = []; up.on('data', d => c.push(d)); up.on('end', () => resolve({ status: up.statusCode, headers: up.headers, body: Buffer.concat(c) }));
    });
    r.on('error', reject); r.on('timeout', () => { r.destroy(); reject(new Error('timeout')); });
    if (body) r.end(body); else r.end();
  });
}
let llmUpOk = null, llmUpCheckedAt = 0;
async function llmUpstreamAvailable() {
  if (LLM_UPSTREAM === 'off') return false;
  if (Date.now() - llmUpCheckedAt < 10_000 && llmUpOk !== null) return llmUpOk;
  llmUpCheckedAt = Date.now();
  try { const r = await upstream('GET', LLM_UPSTREAM + '/models', {}, null, 800); llmUpOk = r.status === 200; } catch { llmUpOk = false; }
  return llmUpOk;
}
function resolveLlm(id) { const r = RESOLVE[id] || id; return r.startsWith('google/gemma') ? r : 'google/gemma-4-12b'; }

// ---- estimate ----
function estimate(b) {
  const breakdown = []; let exact = 0, llmEst = 0, required = 0, spent = 0;
  const dur = Math.max(0, +b.duration_ms || 0);
  if (b.task === 'transcribe' || b.task === 'both') {
    const m = sttModel(b.stt_model); const c = sttCharge(m, dur); exact += c;
    required = Math.max(required, spent + c); spent += c;
    breakdown.push({ item: 'stt', model: b.stt_model || m.id, tier: m.tier, summary_mode: null, amount: money(c), exact: true });
  }
  if (b.task === 'summarize' || b.task === 'both') {
    const m = llmModel(b.llm_model); const mode = b.summary_mode || 'quick';
    let groups = Array.isArray(b.llm_calls) ? b.llm_calls : null;
    if (!groups) {  // statistics: transcript characters (or ~900 characters/minute) and the mode
      const chars = +b.transcript_chars || Math.round(dur / 60000 * 900) || 2000;
      groups = mode === 'complex' ? [{ count: 1, input_chars: chars, max_tokens: 4000 }, { count: null, input_chars: chars, max_tokens: 4000 }, { count: 1, input_chars: 4000, max_tokens: 2000 }]
                                  : [{ count: 1, input_chars: chars, max_tokens: 8000 }];
    }
    for (const g of groups) {
      const n = g.count === null || g.count === undefined ? 5 : Math.max(0, +g.count);   // "statistics": 5 topics
      const inTok = Math.ceil((+g.input_chars || 0) / 4), maxT = Math.max(1, +g.max_tokens || 1);
      const perCall = llmCost(m, inTok, Math.round(Math.min(maxT, 900))), hold = llmCost(m, inTok, maxT);
      for (let i = 0; i < n; i++) { required = Math.max(required, spent + hold); spent += perCall; llmEst += perCall; }
    }
    breakdown.push({ item: 'llm', model: b.llm_model || m.id, tier: m.tier, summary_mode: mode, amount: money(llmEst), exact: false });
  }
  const est = exact + llmEst;
  const after = state.balance - est;
  return { estimate: money(est), low: money(exact + Math.round(llmEst * 0.7)), high: money(exact + Math.round(llmEst * 1.5)),
    balance: money(state.balance), required: money(required), enough: state.balance >= required,
    balance_after: money(after), low_balance_after: after < cfg.low_threshold, vat_mode: cfg.vat_mode, breakdown };
}

// ---- account -----------------------------------------------------------------------------
function account(key) {
  const hl = m => Math.max(0, state.balance) / usdNet(m.price.per_hour);
  const notices = [];
  if (cfg.notice) notices.push({ id: 'n_cfg', level: cfg.notice_level, message: cfg.notice, url: `${BASE}/status` });
  const terms = { accepted_version: state.terms_accepted, current_version: TERMS.current, current_url: `${BASE}/terms/${TERMS.current}`, upcoming: null };
  if (cfg.terms_pending && !cfg.terms_required)
    terms.upcoming = { version: TERMS.upcoming, effective_from: new Date(Date.now() + 10 * 86400_000).toISOString(),
                       url: `${BASE}/terms/${TERMS.upcoming}`, summary: MSG.terms_summary[0] };
  if (cfg.terms_required) { terms.current_version = TERMS.upcoming; terms.current_url = `${BASE}/terms/${TERMS.upcoming}`; }
  return { email: state.keys.get(key).email, balance: money(state.balance), vat_mode: cfg.vat_mode, low_balance_threshold: money(cfg.low_threshold),
    low_balance: lowBalance(), low_balance_since: lowBalance() ? state.low_since : null, balance_empty: state.balance <= 0,
    hours_left: { fast: +hl(findModel('tanara/stt-fast')).toFixed(2), accurate: +hl(findModel('tanara/stt-accurate')).toFixed(2) },
    topup_available: cfg.topup_available, contact_url: `${BASE}/contact`, dashboard_url: `${BASE}/dashboard`, usage_url: `${BASE}/dashboard/usage`,
    trial: cfg.trial, terms, notices };
}

// ---- router ------------------------------------------------------------------------------
const server = http.createServer(async (req, res) => {
  req.requestId = newRequestId();
  const url = new URL(req.url, BASE);
  const path = url.pathname;
  log(req.method, path, req.headers['x-tanara-job-id'] ? `job=${req.headers['x-tanara-job-id'].slice(0, 8)}` : '', req.requestId);
  try { await route(req, res, url, path); }
  catch (e) { log('  ↳ belső hiba:', e); if (!res.headersSent) err(req, res, 500, 'internal_error'); }
});

async function route(req, res, url, path) {
  // --- mock control (not part of the contract) ---
  if (path === '/__mock/config' && req.method === 'POST') {
    const b = parseJson(await readBody(req)) || {};
    for (const [k, v] of Object.entries(b)) {
      if (k === 'balance') { state.balance = Math.round(+v * USD); bookBalanceChange(); }
      else if (k === 'terms_accepted') state.terms_accepted = v;
      else if (k === 'low_threshold') cfg.low_threshold = Math.round(+v * USD);
      else if (k in cfg) cfg[k] = v;
    }
    if ('fail_chat_after' in b) state.chat_ok = 0;
    log('  ↳ mock-config:', JSON.stringify(b));
    return json(req, res, 200, { cfg, balance_net_micros: state.balance });
  }
  if (path === '/__mock/state' && req.method === 'GET')
    return json(req, res, 200, { cfg, balance_net_micros: state.balance, terms_accepted: state.terms_accepted,
      transcriptions: [...state.transcriptions.entries()].map(([id, t]) => ({ id, ...t })), usage: state.usage, waitlist: state.waitlist,
      devices: [...state.devices.values()].map(d => ({ user_code: d.user_code, approved: d.approved, denied: d.denied, cancelled: d.cancelled, info: d.info })) });

  // --- browser pages (the mock's "web") ---
  if (path === '/device') return devicePage(req, res, url);
  if (path.startsWith('/dashboard') || path === '/contact' || path === '/support' || path.startsWith('/terms/') || path === '/status' || path === '/download')
    return webPage(req, res, url, path);
  if (req.method === 'POST' && path === '/topup') { state.balance += usdNet(10 / (cfg.vat_mode === 'gross' ? 1 + VAT_RATE : 1)); bookBalanceChange();
    res.writeHead(302, { location: '/dashboard' }); return res.end(); }

  // --- min-client gate (only if the client sends a version) ---
  const cv = (req.headers['x-tanara-client'] || '').split('/')[0];
  if (cv && semverLt(cv, cfg.min_client))
    return err(req, res, 426, 'client_too_old', { min_client: cfg.min_client, download_url: `${BASE}/download` });

  // --- waitlist ("Coming soon" panel), no auth ---
  if (req.method === 'POST' && path === '/v1/waitlist') return waitlist(req, res);

  // --- device flow ---
  if (req.method === 'POST' && path === '/v1/auth/device/code') {
    const b = parseJson(await readBody(req)) || {};
    const device_code = randomBytes(16).toString('hex');
    const raw = randomBytes(4).toString('hex').toUpperCase(); const user_code = raw.slice(0, 4) + '-' + raw.slice(4);
    state.devices.set(device_code, { user_code, approved: cfg.auto_approve && !cfg.deny, denied: cfg.deny, cancelled: false, api_key: null, created: Date.now(),
      info: { device_name: b.device_name || null, platform: b.platform || null, client_version: b.client_version || null } });
    return json(req, res, 200, { device_code, user_code, verification_uri: `${BASE}/device`,
      verification_uri_complete: `${BASE}/device?code=${user_code}`, interval: 1, expires_in: 600 });
  }
  if (req.method === 'POST' && path === '/v1/auth/device/token') {
    const b = parseJson(await readBody(req)) || {}; const d = state.devices.get(b.device_code);
    if (!d) return err(req, res, 400, 'invalid_device_code');
    if (d.denied || d.cancelled) return err(req, res, 400, 'access_denied');
    if (Date.now() - d.created > 600_000) return err(req, res, 400, 'expired_token');
    if (!d.approved) return err(req, res, 400, 'authorization_pending');
    if (d.api_key) return err(req, res, 400, 'invalid_device_code');   // the key is shown only once
    d.api_key = 'tk_' + randomBytes(12).toString('hex');
    const key_id = 'key_' + randomBytes(4).toString('hex');
    state.keys.set(d.api_key, { email: 'anna@example.com', key_id });
    return json(req, res, 200, { api_key: d.api_key, key_id, email: 'anna@example.com' });
  }
  if (req.method === 'POST' && path === '/v1/auth/device/cancel') {
    const b = parseJson(await readBody(req)) || {}; const d = state.devices.get(b.device_code);
    if (d && !d.api_key) d.cancelled = true;
    return noContent(req, res);
  }

  // --- from here on auth is required ---
  const key = auth(req);
  if (!key) return err(req, res, 401, 'unauthorized');

  if (req.method === 'POST' && path === '/v1/auth/logout') { if (key !== 'tk_test_123') state.keys.delete(key); return noContent(req, res); }

  // Follow-up calls (a running job) also work when suspended and without terms.
  let mm;
  if (req.method === 'DELETE' && (mm = path.match(/^\/v1\/files\/([^/]+)$/))) return deleteFile(req, res, mm[1]);
  if (req.method === 'GET' && (mm = path.match(/^\/v1\/transcriptions\/([^/]+)$/))) return getTranscription(req, res, mm[1]);
  if (req.method === 'GET' && (mm = path.match(/^\/v1\/transcriptions\/([^/]+)\/transcript$/))) return getTranscript(req, res, mm[1]);
  if (req.method === 'DELETE' && (mm = path.match(/^\/v1\/transcriptions\/([^/]+)$/))) return deleteTranscription(req, res, mm[1]);
  if (req.method === 'GET' && (mm = path.match(/^\/v1\/summaries\/([^/]+)$/))) return getSummary(req, res, mm[1]);

  if (req.method === 'POST' && path === '/v1/account/terms-acceptance') {
    const b = parseJson(await readBody(req)) || {};
    const a0 = account(key);
    const allowed = [a0.terms.current_version, a0.terms.upcoming ? a0.terms.upcoming.version : null].filter(Boolean);
    if (!allowed.includes(b.version))
      return err(req, res, 400, 'validation_error', { fields: { version: { code: 'unknown_version', message: text(req, 'validation_error') } } });
    state.terms_accepted = b.version;
    log(`  ↳ ÁSZF elfogadva: ${b.version}`);
    return json(req, res, 200, { terms: account(key).terms });
  }

  // Account-level suspension (the follow-up calls and logout above are already served).
  if (cfg.suspended && !(path === '/v1/account/topup-link' && cfg.suspended === 'payment_dispute'))
    return err(req, res, 403, 'account_suspended', { suspension_reason: cfg.suspended });

  if (req.method === 'GET' && path === '/v1/account') return json(req, res, 200, account(key));
  if (req.method === 'GET' && path === '/v1/models') {
    let extra = [];
    if (await llmUpstreamAvailable()) {
      try { const r = await upstream('GET', LLM_UPSTREAM + '/models', {}, null, 800);
        extra = (JSON.parse(r.body.toString()).data || []).map(x => modelJson(model(x.id, 'llm', null, false, true, x.id + ' (helyi)', { in: 0.1, out: 0.3 }))); } catch {}
    }
    const cap = url.searchParams.get('capability');
    if (cap && !CAPABILITY_KINDS[cap]) return err(req, res, 422, 'invalid_input', { fields: { capability: { code: 'unknown_capability', message: text(req, 'invalid_input') } } });
    const kinds = cap ? CAPABILITY_KINDS[cap] : null;
    const data = [...MODELS.map(modelJson), ...extra].filter(x => !kinds || kinds.includes(x.tanara.kind));
    return json(req, res, 200, { object: 'list', data });
  }
  if (req.method === 'POST' && path === '/v1/account/topup-link') {
    if (processingBlock(req, res, { topup: true })) return;
    if (!cfg.topup_available) return err(req, res, 409, 'topup_unavailable', { contact_url: `${BASE}/contact` });
    return json(req, res, 200, { url: `${BASE}/dashboard/topup?handoff=${randomBytes(6).toString('hex')}`, expires_at: new Date(Date.now() + 300_000).toISOString() });
  }
  if (req.method === 'POST' && path === '/v1/estimate') {
    if (processingBlock(req, res)) return;
    const b = parseJson(await readBody(req));
    if (!b || !['transcribe', 'summarize', 'both'].includes(b.task) || !Number.isFinite(+b.duration_ms))
      return err(req, res, 400, 'validation_error', { fields: { task: { code: 'required', message: text(req, 'validation_error') } } });
    const e = estimate(b);
    log(`  ↳ becslés ${b.task}: $${(e.estimate.amount_micros / USD).toFixed(4)} required=$${(e.required.amount_micros / USD).toFixed(4)} enough=${e.enough}`);
    return json(req, res, 200, e);
  }
  if (req.method === 'POST' && path === '/v1/files') return uploadFile(req, res);
  if (req.method === 'POST' && path === '/v1/transcriptions') return createTranscription(req, res);
  if (req.method === 'POST' && path === '/v1/chat/completions') return chat(req, res);
  if (req.method === 'POST' && path === '/v1/embeddings') return textEmbeddings(req, res);
  if (req.method === 'POST' && path === '/v1/voice/embeddings') return voiceEmbeddings(req, res);
  if (req.method === 'POST' && path === '/v1/voice/identify') return voiceIdentify(req, res);
  if (req.method === 'POST' && path === '/v1/summaries') return createSummary(req, res);
  if (req.method === 'POST' && path === '/v1/tags/suggest') return suggestTags(req, res);
  return err(req, res, 404, 'not_found');
}

// ---- waitlist ----
const DISPOSABLE = ['mailinator.com', 'guerrillamail.com', '10minutemail.com', 'yopmail.com', 'tempmail.com'];
async function waitlist(req, res) {
  if (cfg.rate_limited) return err(req, res, 429, 'rate_limited', { retry_after: 30 }, { 'retry-after': '30' });
  if (cfg.maintenance || cfg.upstream_down) return err(req, res, 503, cfg.maintenance ? 'maintenance' : 'upstream_unavailable', { retry_after: 60 }, { 'retry-after': '60' });
  if (cfg.internal_error) return err(req, res, 500, 'internal_error');
  const b = parseJson(await readBody(req));
  const fields = {};
  const email = b && typeof b.email === 'string' ? b.email.trim() : '';
  if (!/^[^@\s]+@[^@\s]+\.[^@\s]+$/.test(email)) fields.email = { code: 'invalid_email', message: text(req, 'validation_error') };
  if (!b || b.consent !== true) fields.consent = { code: 'required', message: text(req, 'validation_error') };
  const enumBad = (v, allowed) => v !== undefined && v !== null && !allowed.includes(v);
  if (b && enumBad(b.use_case, ['meetings', 'interviews', 'audio_files', 'other'])) fields.use_case = { code: 'invalid_value', message: text(req, 'validation_error') };
  if (b && b.meeting_languages !== undefined && (!Array.isArray(b.meeting_languages) || b.meeting_languages.some(x => !['hu', 'en', 'other'].includes(x))))
    fields.meeting_languages = { code: 'invalid_value', message: text(req, 'validation_error') };
  if (b && enumBad(b.platform, ['windows', 'linux', 'macos', 'unknown'])) fields.platform = { code: 'invalid_value', message: text(req, 'validation_error') };
  if (b && enumBad(b.ui_language, ['hu', 'en'])) fields.ui_language = { code: 'invalid_value', message: text(req, 'validation_error') };
  if (Object.keys(fields).length) return err(req, res, 400, 'validation_error', { fields });
  if (DISPOSABLE.includes(email.split('@')[1].toLowerCase()))
    return err(req, res, 400, 'disposable_email', { fields: { email: { code: 'disposable_email', message: text(req, 'disposable_email') } } });
  state.waitlist.push({ at: new Date().toISOString(), ...b, email });
  log(`  ↳ várólista: ${email} (${b.source || '-'}, ${b.platform || '-'}, ${b.client_version || '-'}, use_case=${b.use_case ?? '-'}, nyelvek=${(b.meeting_languages || []).join('/') || '-'})`);
  res.writeHead(202, baseHeaders(req)); return res.end();
}

// ---- STT ---------------------------------------------------------------------------------
async function uploadFile(req, res) {
  if (processingBlock(req, res)) return;
  const body = await readBody(req);
  const audio = multipartFile(body, req.headers['content-type']);
  const duration_ms = measureDurationMs(audio);
  const f = { size: audio.length, duration_ms, job_id: req.headers['x-tanara-job-id'] || null };
  const id = 'file_' + randomUUID().slice(0, 8);
  if (SONIOX_KEY) {
    const up = await upstream('POST', 'https://api.soniox.com/v1/files', { authorization: `Bearer ${SONIOX_KEY}`, 'content-type': req.headers['content-type'], 'content-length': body.length }, body);
    if (up.status >= 300) return err(req, res, 503, 'upstream_unavailable', { retry_after: 60 });
    f.upstream_id = JSON.parse(up.body.toString()).id;
  }
  state.files.set(id, f);
  log(`  ↳ fájl ${id}: ${audio.length} bájt, ${(duration_ms / 1000).toFixed(1)} s`);
  return json(req, res, 200, { id, size: audio.length, duration_ms });
}
async function createTranscription(req, res) {
  if (processingBlock(req, res)) return;
  const b = parseJson(await readBody(req)) || {};
  const f = state.files.get(b.file_id);
  if (!f) return err(req, res, 400, 'validation_error', { fields: { file_id: { code: 'unknown_file', message: text(req, 'not_found') } } });
  const m = sttModel(b.model); const c = sttCharge(m, f.duration_ms);
  if (state.balance <= 0 || state.balance < c) return insufficient(req, res, c, 'charge');
  const jobId = req.headers['x-tanara-job-id'] || null;
  let upstream_id = null;
  if (SONIOX_KEY) {
    const body = { ...b, model: 'stt-async-v5', file_id: f.upstream_id };
    const buf = Buffer.from(JSON.stringify(body));
    const up = await upstream('POST', 'https://api.soniox.com/v1/transcriptions', { authorization: `Bearer ${SONIOX_KEY}`, 'content-type': 'application/json', 'content-length': buf.length }, buf);
    if (up.status >= 300) return err(req, res, 503, 'upstream_unavailable', { retry_after: 60 });
    upstream_id = JSON.parse(up.body.toString()).id;
  }
  state.balance -= c; bookBalanceChange();
  const id = 'tr_' + randomUUID().slice(0, 8); const charge_id = 'ch_' + randomBytes(4).toString('hex');
  state.transcriptions.set(id, { status: 'queued', file_id: b.file_id, model: m.id, diarization: !!b.enable_speaker_diarization, created: Date.now(),
    charge: c, charge_id, refunded: false, refund_fail: !!cfg.refund, content_deleted: false, job_id: jobId, upstream_id, error_code: null });
  state.usage.push({ at: new Date().toISOString(), kind: 'stt', model: m.id, amount: disp(c), job_id: jobId, summary_mode: null, request_id: req.requestId, refunded: false, charge_id });
  log(`  ↳ terhelés ${fmt(c)} (stt ${m.id}, ${(f.duration_ms / 1000).toFixed(1)} s) → egyenleg ${fmt(state.balance)}`);
  return json(req, res, 200, { id, status: 'queued', tanara: chargeInfo(charge_id, c, jobId) }, chargeHeaders(c));
}
function jobInfo(t) {
  const info = { charge_id: t.charge_id, charge: money(t.charge), refunded: t.refunded, balance: money(state.balance), error_code: t.error_code,
    content_deleted: t.content_deleted, job_id: t.job_id, record_expires_at: new Date(t.created + 30 * 86400_000).toISOString() };
  if (t.refunded) info.refund = money(t.charge);
  return info;
}
async function getTranscription(req, res, id) {
  const t = state.transcriptions.get(id); if (!t) return err(req, res, 404, 'not_found');
  if (t.status !== 'completed' && t.status !== 'error') {
    if (SONIOX_KEY && t.upstream_id) {
      const up = await upstream('GET', `https://api.soniox.com/v1/transcriptions/${t.upstream_id}`, { authorization: `Bearer ${SONIOX_KEY}` });
      const u = JSON.parse(up.body.toString() || '{}'); t.status = u.status === 'error' ? 'error' : u.status || t.status;
    } else {
      const age = Date.now() - t.created;
      t.status = age < 1200 ? 'queued' : age < 2500 ? 'processing' : t.refund_fail ? 'error' : 'completed';
    }
    if (t.status === 'error' && !t.refunded) {   // automatic write-back, visible in the same reply
      t.error_code = 'upstream_error'; t.refunded = true; state.balance += t.charge; bookBalanceChange();
      const u = state.usage.find(x => x.charge_id === t.charge_id); if (u) u.refunded = true;
      log(`  ↳ visszaírás ${fmt(t.charge)} (${id}) → egyenleg ${fmt(state.balance)}`);
    }
  }
  return json(req, res, 200, { id, status: t.status, error_message: t.status === 'error' ? text(req, 'upstream_unavailable') : null, tanara: jobInfo(t) });
}
async function getTranscript(req, res, id) {
  const t = state.transcriptions.get(id); if (!t || t.content_deleted) return err(req, res, 404, 'not_found');
  if (t.status !== 'completed') return err(req, res, 409, 'not_completed');
  if (SONIOX_KEY && t.upstream_id) {
    const up = await upstream('GET', `https://api.soniox.com/v1/transcriptions/${t.upstream_id}/transcript`, { authorization: `Bearer ${SONIOX_KEY}` });
    return json(req, res, up.status, JSON.parse(up.body.toString() || '{}'));
  }
  return json(req, res, 200, { id, tokens: cannedTokens(t.diarization) });
}
async function deleteTranscription(req, res, id) {
  const t = state.transcriptions.get(id);
  if (t && !t.content_deleted) { t.content_deleted = true;
    if (SONIOX_KEY && t.upstream_id) upstream('DELETE', `https://api.soniox.com/v1/transcriptions/${t.upstream_id}`, { authorization: `Bearer ${SONIOX_KEY}` }).catch(() => {}); }
  return noContent(req, res);
}
async function deleteFile(req, res, id) {
  const f = state.files.get(id);
  if (f && SONIOX_KEY && f.upstream_id) upstream('DELETE', `https://api.soniox.com/v1/files/${f.upstream_id}`, { authorization: `Bearer ${SONIOX_KEY}` }).catch(() => {});
  state.files.delete(id);
  return noContent(req, res);
}

// ---- LLM ---------------------------------------------------------------------------------
async function chat(req, res) {
  if (processingBlock(req, res)) return;
  const b = parseJson(await readBody(req));
  if (!b || !Array.isArray(b.messages)) return err(req, res, 400, 'validation_error', { fields: { messages: { code: 'required', message: text(req, 'validation_error') } } });
  const m = llmModel(b.model);
  const promptChars = b.messages.reduce((s, x) => s + String(x.content || '').length, 0);
  const promptTok = Math.ceil(promptChars / 4), maxT = Math.max(1, +b.max_tokens || 2048);
  const hold = llmCost(m, promptTok, maxT);
  if (state.balance <= 0 || state.balance < hold) return insufficient(req, res, hold, 'hold');
  if (cfg.fail_chat_after !== null && cfg.fail_chat_after !== undefined && state.chat_ok >= +cfg.fail_chat_after)
    return err(req, res, 503, 'upstream_unavailable', { retry_after: 60 }, { 'retry-after': '60' });
  const summaryMode = req.headers['x-tanara-summary-mode'] || null;
  let content, usage;
  if (await llmUpstreamAvailable()) {
    const buf = Buffer.from(JSON.stringify({ ...b, model: resolveLlm(b.model) }));
    try {
      const up = await upstream('POST', LLM_UPSTREAM + '/chat/completions', { 'content-type': 'application/json', 'content-length': buf.length, authorization: 'Bearer lm-studio' }, buf, 600000);
      const u = JSON.parse(up.body.toString());
      if (up.status < 300) { content = u.choices?.[0]?.message?.content ?? ''; usage = u.usage; }
    } catch { /* falls back to canned */ }
  }
  if (content === undefined) { content = cannedCompletion(b, summaryMode); usage = { prompt_tokens: promptTok, completion_tokens: Math.ceil(content.length / 4) }; }
  usage = { prompt_tokens: usage?.prompt_tokens ?? promptTok, completion_tokens: usage?.completion_tokens ?? 100 };
  usage.total_tokens = usage.prompt_tokens + usage.completion_tokens;
  const c = llmCost(m, usage.prompt_tokens, usage.completion_tokens);
  state.balance -= c; bookBalanceChange(); state.chat_ok++;
  const jobId = req.headers['x-tanara-job-id'] || null; const charge_id = 'ch_' + randomBytes(4).toString('hex');
  state.usage.push({ at: new Date().toISOString(), kind: 'llm', model: m.id, amount: disp(c), job_id: jobId, summary_mode: summaryMode, request_id: req.requestId, refunded: false, charge_id });
  log(`  ↳ terhelés ${fmt(c)} (llm ${m.id}, ${usage.prompt_tokens}+${usage.completion_tokens} tok, ${summaryMode || '-'}) → egyenleg ${fmt(state.balance)}`);
  return json(req, res, 200, { id: 'chatcmpl_' + randomBytes(4).toString('hex'), object: 'chat.completion', model: b.model || m.id,
    choices: [{ index: 0, message: { role: 'assistant', content }, finish_reason: 'stop' }],
    usage: { ...usage, tanara_charge: chargeInfo(charge_id, c, jobId) } }, chargeHeaders(c));
}

// ---- capability endpoints (x-status: draft) ------------------------------------------------
// A deterministic unit vector from a seed (the same input -> the same vector, every run).
function hashVector(seed, dim) {
  const out = []; let block = Buffer.alloc(0); let i = 0;
  while (out.length < dim) {
    if (i + 4 > block.length) { block = createHash('sha256').update(seed).update(String(out.length)).digest(); i = 0; }
    out.push(block.readInt32LE(i) / 2147483648); i += 4;
  }
  const n = Math.sqrt(out.reduce((s, x) => s + x * x, 0)) || 1;
  return out.map(x => +(x / n).toFixed(6));
}
const invalid = (req, res, field, code = 'invalid_value') =>
  err(req, res, 422, 'invalid_input', { fields: { [field]: { code, message: text(req, 'invalid_input') } } });
// Books one charge (net micros) and returns the response headers + the `tanara` object.
function book(req, kind, modelId, c, extra = {}) {
  state.balance -= c; bookBalanceChange();
  const jobId = req.headers['x-tanara-job-id'] || null; const charge_id = 'ch_' + randomBytes(4).toString('hex');
  state.usage.push({ at: new Date().toISOString(), kind, model: modelId, amount: disp(c), job_id: jobId, summary_mode: extra.summary_mode || null, request_id: req.requestId, refunded: false, charge_id });
  log(`  ↳ terhelés ${fmt(c)} (${kind} ${modelId}) → egyenleg ${fmt(state.balance)}`);
  return { headers: chargeHeaders(c), tanara: chargeInfo(charge_id, c, jobId) };
}
// Multipart parts by name: {name: {data: Buffer, filename}} (simple, enough for the client's uploads).
function multipartParts(buf, ctype) {
  const m = /boundary=(?:"([^"]+)"|([^;]+))/.exec(ctype || ''); if (!m) return {};
  const b = Buffer.from('--' + (m[1] || m[2])); const parts = {};
  let at = buf.indexOf(b);
  while (at >= 0) {
    const hdrStart = at + b.length + 2; const hdrEnd = buf.indexOf('\r\n\r\n', hdrStart); if (hdrEnd < 0) break;
    const head = buf.subarray(hdrStart, hdrEnd).toString('utf8');
    const next = buf.indexOf(b, hdrEnd); if (next < 0) break;
    const name = /name="([^"]+)"/.exec(head); const filename = /filename="([^"]+)"/.exec(head);
    if (name) parts[name[1]] = { data: buf.subarray(hdrEnd + 4, next - 2), filename: filename ? filename[1] : null };
    at = next;
  }
  return parts;
}

async function textEmbeddings(req, res) {
  if (processingBlock(req, res)) return;
  const b = parseJson(await readBody(req));
  const input = b && (typeof b.input === 'string' ? [b.input] : b.input);
  if (!Array.isArray(input) || !input.length || input.some(x => typeof x !== 'string')) return invalid(req, res, 'input', 'required');
  const m = findModel('tanara/text-embedding');
  const tokens = Math.ceil(input.reduce((s, x) => s + x.length, 0) / 4);
  const c = Math.max(1, llmCost(m, tokens, 0));   // at least 1 micro: a call is never free
  if (state.balance <= 0 || state.balance < c) return insufficient(req, res, c, 'charge');
  const { headers } = book(req, 'text_embedding', m.id, c);
  return json(req, res, 200, { object: 'list', model: b.model && b.model !== 'default' ? b.model : m.id,
    data: input.map((t, index) => ({ object: 'embedding', index, embedding: hashVector('text:' + t, TEXT_DIM) })),
    usage: { prompt_tokens: tokens, total_tokens: tokens } }, headers);
}

async function voiceEmbeddings(req, res) {
  if (processingBlock(req, res)) return;
  const parts = multipartParts(await readBody(req), req.headers['content-type']);
  const file = parts.file; const modelId = parts.model ? parts.model.data.toString('utf8') : 'tanara/voice-embedding';
  if (!file || !file.data.length) return invalid(req, res, 'file', 'required');
  const m = findModel(modelId);
  if (!m || m.kind !== 'voice_embedding') return invalid(req, res, 'model', 'unknown_model');
  const duration_ms = Math.max(1000, Math.round(file.data.length / 4));   // ~32 kbps Opus
  const c = sttCharge(m, duration_ms);
  if (state.balance <= 0 || state.balance < c) return insufficient(req, res, c, 'charge');
  const { headers, tanara } = book(req, 'voice_embedding', m.id, c);
  return json(req, res, 200, { object: 'voice_embedding', model: m.id, dim: m.dim,
    embedding: hashVector(createHash('sha256').update(file.data).digest('hex') + m.id, m.dim), tanara }, headers);
}

async function voiceIdentify(req, res) {
  if (processingBlock(req, res)) return;
  const b = parseJson(await readBody(req));
  if (!b || !Array.isArray(b.probes) || !b.probes.length) return invalid(req, res, 'probes', 'required');
  if (!Array.isArray(b.candidates)) return invalid(req, res, 'candidates', 'required');
  const cos = (a, x) => a.length === x.length ? a.reduce((s, v, i) => s + v * x[i], 0) : null;
  const threshold = typeof b.threshold === 'number' ? b.threshold : 0.5;
  // The client's matching rule: per candidate per model the max cosine, then the mean over the shared models.
  const score = (probe, cand) => { let sum = 0, n = 0;
    for (const v of probe.vectors || []) { let best = null;
      for (const p of cand.prints || []) if (p.model === v.model) { const c = cos(v.embedding || [], p.embedding || []); if (c !== null && (best === null || c > best)) best = c; }
      if (best !== null) { sum += best; n++; } }
    return n ? sum / n : -1; };
  const results = b.probes.map(p => {
    const ranked = b.candidates.map(c => ({ candidate: c.ref, score: +score(p, c).toFixed(6) })).sort((x, y) => y.score - x.score);
    const top = ranked[0];
    return { probe: p.id, match: top && top.score >= threshold ? top.candidate : null, score: top ? top.score : -1, ranked };
  });
  const c = usdNet(0.0005) * b.probes.length;
  if (state.balance <= 0 || state.balance < c) return insufficient(req, res, c, 'charge');
  const { headers, tanara } = book(req, 'voice_identify', 'tanara/voice-embedding', c);
  return json(req, res, 200, { results, tanara }, headers);
}

async function createSummary(req, res) {
  if (processingBlock(req, res)) return;
  const b = parseJson(await readBody(req));
  if (!b || !Array.isArray(b.lines) || !b.lines.length) return invalid(req, res, 'lines', 'required');
  if (b.mode && !['quick', 'complex'].includes(b.mode)) return invalid(req, res, 'mode');
  const m = llmModel(b.model);
  const chars = b.lines.reduce((s, l) => s + String(l.text || '').length, 0) + String(b.context_note || '').length;
  const c = llmCost(m, Math.ceil(chars / 4), b.mode === 'complex' ? 4000 : 1500);
  if (state.balance <= 0 || state.balance < c) return insufficient(req, res, c, 'charge');
  const { headers, tanara } = book(req, 'summary', m.id, c, { summary_mode: b.mode || 'quick' });
  const id = 'sum_' + randomUUID().slice(0, 8);
  state.summaries.set(id, { mode: b.mode || 'quick', polls: 0, model: m.id, speakers: [...new Set(b.lines.map(l => l.speaker).filter(Boolean))] });
  return json(req, res, 202, { id, status: 'queued', tanara }, headers);
}
async function getSummary(req, res, id) {
  const t = state.summaries.get(id); if (!t) return err(req, res, 404, 'not_found');
  t.polls++;
  if (t.polls < 2) return json(req, res, 200, { id, status: 'processing' });
  const analyses = t.mode === 'complex' ? [
    { topic_id: 't1', title: 'Muzeo migráció', detail: 'A migráció állapotát egyeztették.', decisions: ['Jövő héten szállítunk.'], open_questions: [], action_items: [{ text: 'Teszt megírása', owner: 'Dompa', due: '' }] },
    { topic_id: 't2', title: 'Tesztelés', detail: 'Ki írja meg a teszteket.', decisions: [], open_questions: ['Ki review-zza?'], action_items: [] }] : [];
  const result = { model: t.model,
    summary: { exec_summary: 'A csapat a MuseumPlus migrációt egyeztette; a szállítás jövő hétre került.', decisions: ['Jövő héten szállítunk.'],
      open_questions: [], action_items: [{ text: 'Teszt megírása', owner: 'Dompa', due: '' }], participants: t.speakers },
    topics: analyses.map(a => ({ id: a.topic_id, title: a.title, summary: a.detail })), analyses,
    markdown: '## Vezetői összefoglaló\nA csapat a MuseumPlus migrációt egyeztette.' };
  return json(req, res, 200, { id, status: 'completed', result });
}

async function suggestTags(req, res) {
  if (processingBlock(req, res)) return;
  const b = parseJson(await readBody(req));
  if (!b || typeof b.summary !== 'string' || !b.summary.trim()) return invalid(req, res, 'summary', 'required');
  if (b.candidates !== undefined && !Array.isArray(b.candidates)) return invalid(req, res, 'candidates');
  const hay = `${b.title || ''}\n${b.summary}`.toLowerCase();
  const picks = (b.candidates || []).filter(c => c.name && hay.includes(String(c.name).toLowerCase()))
    .map(c => ({ ref: c.ref, name: c.name, is_new: false, score: 0.9 }));
  const fresh = [...new Set([...b.summary.matchAll(/#([\p{L}\d][\p{L}\d-]*)/gu)].map(x => x[1]))]
    .slice(0, Math.max(0, +(b.max_new ?? 2))).map(name => ({ ref: null, name, is_new: true, score: 0.5 }));
  const m = llmModel(b.model);
  const c = llmCost(m, Math.ceil(hay.length / 4), 100);
  if (state.balance <= 0 || state.balance < c) return insufficient(req, res, c, 'charge');
  const { headers, tanara } = book(req, 'tags', m.id, c);
  return json(req, res, 200, { suggestions: [...picks, ...fresh], tanara }, headers);
}

// ---- browser pages ----
const page = (res, code, title, body) => { res.writeHead(code, { 'content-type': 'text/html; charset=utf-8' });
  res.end(`<!doctype html><meta charset=utf-8><title>${title}</title><body style="font-family:sans-serif;max-width:720px;margin:2em auto">${body}</body>`); };
async function devicePage(req, res, url) {
  const code = url.searchParams.get('code'); const dev = [...state.devices.values()].find(d => d.user_code === code);
  if (req.method === 'POST' && dev) {
    const b = (await readBody(req)).toString();
    if (dev.cancelled) return page(res, 200, 'Tanara', '<h2>A kérést visszavonták.</h2>');
    if (b.includes('deny')) { dev.denied = true; return page(res, 200, 'Tanara', '<h2>Elutasítva</h2>'); }
    dev.approved = true; return page(res, 200, 'Tanara', '<h2>Jóváhagyva ✓</h2><p>Visszatérhetsz a Tanarába.</p>');
  }
  if (!dev) return page(res, code ? 404 : 200, 'Tanara', `<h2>Eszköz jóváhagyása (mock)</h2><form><input name=code placeholder="XXXX-XXXX"><button>Tovább</button></form>`);
  return page(res, 200, 'Tanara', `<h2>Tanara Cloud (mock) — eszköz jóváhagyása</h2><p>Kód: <b>${code}</b></p>
    <p>Eszköz: ${dev.info.device_name || '?'} · ${dev.info.platform || '?'} · ${dev.info.client_version || '?'}</p>
    ${dev.cancelled ? '<p>A kérést visszavonták.</p>' : `<form method=post><button name=a value=approve>Jóváhagyás</button> <button name=a value=deny>Elutasítás</button></form>`}`);
}
function webPage(req, res, url, path) {
  const usageRows = Object.values(state.usage.reduce((g, u) => { const k = u.job_id || u.charge_id; (g[k] ||= { job: u.job_id, calls: 0, amount: 0, kind: u.kind, mode: u.summary_mode, refunded: false });
    g[k].calls++; g[k].amount += u.refunded ? 0 : u.amount; g[k].refunded ||= u.refunded; return g; }, {}))
    .map(r => `<tr><td>${r.kind}${r.mode ? ' · ' + r.mode : ''}</td><td>${r.calls} hívás</td><td>$${(r.amount / USD).toFixed(4)}</td><td>${r.refunded ? 'visszaírva' : ''}</td><td><code>${r.job || '-'}</code></td></tr>`).join('');
  if (path.startsWith('/terms/')) return page(res, 200, 'ÁSZF', `<h2>ÁSZF ${path.slice(7)} (mock)</h2><p>${MSG.terms_summary[0]}</p>`);
  if (path === '/support') return page(res, 200, 'Support', `<h2>Support (mock)</h2><p>Hibaazonosító: <b>${url.searchParams.get('request_id') || '-'}</b></p>`);
  if (path === '/contact') return page(res, 200, 'Kapcsolat', '<h2>Írj nekünk (mock)</h2><p>support@tanara.example</p>');
  if (path === '/download') return page(res, 200, 'Letöltés', '<h2>Tanara letöltése (mock)</h2>');
  if (path === '/status') return page(res, 200, 'Állapot', `<h2>Szolgáltatás-állapot (mock)</h2><p>${cfg.notice || 'Minden rendben.'}</p>`);
  return page(res, 200, 'Dashboard', `<h2>Tanara Cloud dashboard (mock)</h2><p>Egyenleg: <b>$${(disp(state.balance) / USD).toFixed(2)}</b> (${cfg.vat_mode})</p>
    <form method=post action=/topup><button>+$10 feltöltés</button></form><h3>Használati napló (futásonként)</h3><table border=1 cellpadding=4>${usageRows}</table>`);
}

server.listen(PORT, '127.0.0.1', () => log(`mock-gateway 1.2.0: ${BASE}/v1  egyenleg(nettó)=$${(state.balance / USD).toFixed(2)}  vat=${cfg.vat_mode}  min-client=${cfg.min_client}  ` +
  `STT=${SONIOX_KEY ? 'soniox-passthrough' : 'kannázott'}  LLM=${LLM_UPSTREAM}${LLM_UPSTREAM === 'off' ? '' : ' (fallback: kannázott)'}  auto-approve=${cfg.auto_approve}  teszt-kulcs=tk_test_123`));
