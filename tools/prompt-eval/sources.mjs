// Forrás-jelölők (`[t=mm:ss]`) a kimenetben — ugyanaz a szabály, mint az appban
// (core/src/summary/SummarySources.cpp): idő → a legközelebbi bekezdés ±5 s-on belül.
// Csak mér (lefedettség), a pontozás a bírálóé.

const TIME = String.raw`\d{1,3}:\d{2}(?::\d{2})?`;
const MARKER = new RegExp(String.raw`\[\s*((?:t\s*=|u\d)[^\[\]\n]{0,80}|${TIME}(?:\s*[-–—]\s*${TIME})?(?:\s*[,;]\s*${TIME}(?:\s*[-–—]\s*${TIME})?)*)\s*\]`, 'g');
export const TOLERANCE_MS = 5000;

// "mm:ss" / "h:mm:ss" → ms; rossz idő (másodperc ≥ 60) → -1.
export function strictTime(s) {
  const m = String(s).trim().match(/^(\d{1,3}):(\d{2})(?::(\d{2}))?$/);
  if (!m) return -1;
  if (m[3] !== undefined) {
    if (Number(m[2]) >= 60 || Number(m[3]) >= 60) return -1;
    return (Number(m[1]) * 3600 + Number(m[2]) * 60 + Number(m[3])) * 1000;
  }
  if (Number(m[2]) >= 60) return -1;
  return (Number(m[1]) * 60 + Number(m[2])) * 1000;
}

// Egy szöveg jelölői: { text (jelölő nélkül), refs: [{start, end} | {id}] }.
export function extractMarkers(text) {
  const refs = [];
  const clean = String(text || '').replace(MARKER, (_, body) => {
    for (let item of body.split(/[,;]|\s+(?=t\s*=|u\d)/)) {
      item = item.trim();
      if (!item) continue;
      if (/^u\d+(-\d+)?$/.test(item)) { refs.push({ id: item }); continue; }
      if (item.startsWith('t')) { item = item.slice(1).trim(); if (!item.startsWith('=')) continue; item = item.slice(1).trim(); }
      const r = item.match(/^(.+?)\s*[-–—]\s*(\d.*)$/);
      const start = strictTime(r ? r[1] : item), end = r ? strictTime(r[2]) : start;
      if (start >= 0 && end >= 0) refs.push({ start: Math.min(start, end), end: Math.max(start, end) });
    }
    return '';
  }).replace(/[ \t]{2,}/g, ' ').replace(/[ \t]+([.,;:!?…])/g, '$1').trim();
  return { text: clean, refs };
}

// Az átirat bekezdéseinek kezdőideje (`[mm:ss]` **Beszélő** …); a vég a következő kezdete.
export function paragraphTimes(transcript) {
  const starts = [...String(transcript).matchAll(/^`\[((?:\d+:)?\d+:\d{2})\]`/gm)].map((m) => strictTime(m[1])).filter((t) => t >= 0);
  return starts.map((s, i) => ({ start: s, end: i + 1 < starts.length ? starts[i + 1] : s + 60000 }));
}

// Feloldható-e egy idő-hivatkozás a bekezdésekre (tartalmazó, vagy ±tűrésen belüli).
export function resolves(ref, paras) {
  if (ref.id !== undefined) return true;   // az appban a segments.json id-je; itt nem ellenőrizhető
  const from = ref.start, to = ref.end + 999;
  return paras.some((p) => (p.start <= to && p.end >= from)
    || (p.end < from ? from - p.end : p.start - to) <= TOLERANCE_MS);
}

// Mondatokra bontás (egyszerűsített): . ! ? … után, ha nagybetű / vég jön; a mondat utáni jelölő a mondaté.
export function sentences(text) {
  const out = [];
  const re = new RegExp(String.raw`[^.!?…]*[.!?…]+["”)']*(?:\s*${MARKER.source})*(?=\s+[\p{Lu}\d"„]|\s*$)|[^.!?…]+$`, 'gu');
  for (const m of String(text || '').matchAll(re)) if (m[0].trim()) out.push(m[0].trim());
  return out;
}

// A rövid forma forrás-lefedettsége: hány állítás (mondat / döntés / teendő) kapott jelölőt,
// és ebből hány oldható fel az átiratra (transcript nélkül csak a jelölt szám).
export function sourceStats(parsed, transcript) {
  if (!parsed) return null;
  const items = [
    ...sentences(parsed.execSummary),
    ...(Array.isArray(parsed.decisions) ? parsed.decisions : []),
    ...(Array.isArray(parsed.actionItems) ? parsed.actionItems.map((a) => (a && typeof a === 'object' ? a.text : a)) : []),
  ].map((t) => extractMarkers(t));
  const paras = transcript ? paragraphTimes(transcript) : null;
  const marked = items.filter((i) => i.refs.length > 0);
  const resolved = paras ? marked.filter((i) => i.refs.some((r) => resolves(r, paras))) : marked;
  return { items: items.length, marked: marked.length, resolved: resolved.length,
    coverage: items.length ? Number((resolved.length / items.length).toFixed(2)) : 0 };
}
