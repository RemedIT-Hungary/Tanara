// Source markers (`[t=mm:ss]`) in the output: the same rule as in the app
// (core/src/summary/SummarySources.cpp): a time maps to the nearest paragraph within +-5 s.
// It only measures (coverage); scoring is the reviewer's job.

const TIME = String.raw`\d{1,3}:\d{2}(?::\d{2})?`;
const MARKER = new RegExp(String.raw`\[\s*((?:t\s*=|u\d)[^\[\]\n]{0,80}|${TIME}(?:\s*[-–—]\s*${TIME})?(?:\s*[,;]\s*${TIME}(?:\s*[-–—]\s*${TIME})?)*)\s*\]`, 'g');
export const TOLERANCE_MS = 5000;

// "mm:ss" / "h:mm:ss" -> ms; a bad time (seconds >= 60) -> -1.
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

// One text's markers: { text (without markers), refs: [{start, end} | {id}] }.
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

// Start times of the transcript's paragraphs (`[mm:ss]` **Speaker** ...); the end is the next one's start.
export function paragraphTimes(transcript) {
  const starts = [...String(transcript).matchAll(/^`\[((?:\d+:)?\d+:\d{2})\]`/gm)].map((m) => strictTime(m[1])).filter((t) => t >= 0);
  return starts.map((s, i) => ({ start: s, end: i + 1 < starts.length ? starts[i + 1] : s + 60000 }));
}

// Whether a time reference resolves to the paragraphs (containing, or within the tolerance).
export function resolves(ref, paras) {
  if (ref.id !== undefined) return true;   // in the app, the id from segments.json; cannot be checked here
  const from = ref.start, to = ref.end + 999;
  return paras.some((p) => (p.start <= to && p.end >= from)
    || (p.end < from ? from - p.end : p.start - to) <= TOLERANCE_MS);
}

// Sentence splitting (simplified): after . ! ? ... when a capital letter / the end follows; a marker after a sentence belongs to that sentence.
export function sentences(text) {
  const out = [];
  const re = new RegExp(String.raw`[^.!?…]*[.!?…]+["”)']*(?:\s*${MARKER.source})*(?=\s+[\p{Lu}\d"„]|\s*$)|[^.!?…]+$`, 'gu');
  for (const m of String(text || '').matchAll(re)) if (m[0].trim()) out.push(m[0].trim());
  return out;
}

// Source coverage of the short form: how many claims (sentence / decision / open question /
// action item) got a marker, and how many of those resolve to the transcript (without a
// transcript, only the marked count).
export function sourceStats(parsed, transcript) {
  if (!parsed) return null;
  const items = [
    ...sentences(parsed.execSummary),
    ...(Array.isArray(parsed.decisions) ? parsed.decisions : []),
    ...(Array.isArray(parsed.openQuestions) ? parsed.openQuestions : []),
    ...(Array.isArray(parsed.actionItems) ? parsed.actionItems.map((a) => (a && typeof a === 'object' ? a.text : a)) : []),
  ].map((t) => extractMarkers(t));
  const paras = transcript ? paragraphTimes(transcript) : null;
  const marked = items.filter((i) => i.refs.length > 0);
  const resolved = paras ? marked.filter((i) => i.refs.some((r) => resolves(r, paras))) : marked;
  return { items: items.length, marked: marked.length, resolved: resolved.length,
    coverage: items.length ? Number((resolved.length / items.length).toFixed(2)) : 0 };
}

// The memo note (TOPICS) structure, as in the app (SummaryPipeline::parseNotes): `### title`,
// then the intro (lines without bullets), the points (`- ...`), and the sub-headings
// (`#### Arguments`, `#### Unresolved`). -> [{ title, intro, points: [{ text, group }] }]
export function parseTopics(notes) {
  const topics = [];
  let cur = null, group = '', introOpen = false, fresh = false, inTopics = false;
  for (const raw of String(notes || '').split('\n')) {
    const line = raw.trim();
    if (!line || line.startsWith('```')) continue;
    if (/^(TOPICS|T[ÉE]M[ÁA]K)\s*:?$/i.test(line.replace(/[#*]/g, '').trim())) { inTopics = true; continue; }
    if (/^(DECISIONS|OPEN|ACTIONS|SUMMARY)\s*:?$/i.test(line.replace(/[#*]/g, '').trim())) { inTopics = false; continue; }
    if (!inTopics) continue;
    if (line.startsWith('####') && cur) {
      const h = line.replace(/^#+\s*/, '').replace(/[*:]+$/g, '').trim().toLowerCase();
      group = /^(arguments?|elhangzott érvek|érvek)$/.test(h) ? 'arguments'
        : /^(unresolved|open questions?|nyitott kérdés(ek)?)$/.test(h) ? 'open' : h;
      fresh = true; introOpen = false;
      continue;
    }
    if (line.startsWith('#')) {
      cur = { title: line.replace(/^#+\s*/, '').trim(), intro: '', points: [] };
      topics.push(cur); group = ''; introOpen = true; fresh = false;
      continue;
    }
    if (!cur) { cur = { title: '', intro: '', points: [] }; topics.push(cur); }
    const b = line.match(/^(?:[-*•–]|\d+[.)])\s+(.*)$/);
    if (!b && introOpen) { cur.intro += (cur.intro ? ' ' : '') + line; continue; }
    if (!b && cur.points.length && !fresh) { cur.points[cur.points.length - 1].text += ' ' + line; continue; }
    cur.points.push({ text: b ? b[1].trim() : line, group });
    introOpen = false; fresh = false;
  }
  return topics;
}

// Source coverage of the memo: how many of the intro sentences and points got a (resolvable)
// marker; the number of subsections (arguments / open).
export function memoSourceStats(notes, transcript) {
  const topics = parseTopics(notes);
  const items = [];
  const groups = { arguments: 0, open: 0, other: 0 };
  for (const t of topics) {
    for (const s of sentences(t.intro)) items.push(extractMarkers(s));
    for (const p of t.points) {
      items.push(extractMarkers(p.text));
      if (p.group === 'arguments' || p.group === 'open') ++groups[p.group];
      else if (p.group) ++groups.other;
    }
  }
  const paras = transcript ? paragraphTimes(transcript) : null;
  const marked = items.filter((i) => i.refs.length > 0);
  const resolved = paras ? marked.filter((i) => i.refs.some((r) => resolves(r, paras))) : marked;
  return { topics: topics.length, items: items.length, marked: marked.length, resolved: resolved.length,
    coverage: items.length ? Number((resolved.length / items.length).toFixed(2)) : 0, groups };
}
