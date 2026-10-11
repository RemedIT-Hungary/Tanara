// Az app BEÉPÍTETT összefoglaló-promptjai (core/src/PromptLibrary.cpp) a kiértékelőhöz,
// újrafordítás nélkül: a nyers literálokból (R"PROMPT(…)PROMPT") ugyanúgy állnak össze, mint a
// C++ compose()-ban. Prompt-azonosító a run.mjs-ben: "@single", "@notes", "@merge"
// (pl. --prompts @single  vagy  chunk:@notes:@merge).
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const CPP = path.join(HERE, '..', '..', 'core', 'src', 'PromptLibrary.cpp');

export function builtinParts(cpp = fs.readFileSync(CPP, 'utf8')) {
  const parts = {};
  for (const m of cpp.matchAll(/const char\* const (k\w+) = R"PROMPT\(([\s\S]*?)\)PROMPT";/g)) parts[m[1]] = m[2];
  // A hívásonkénti forrás-leírás (%SOURCE_OF%): compose(kX, "…") a promptBuiltin-ben.
  const sourceOf = {};
  for (const m of cpp.matchAll(/compose\((k\w+),\s*"((?:[^"\\]|\\.)*)"\)/g)) sourceOf[m[1]] = m[2].replace(/\\"/g, '"');
  return { parts, sourceOf };
}

const IDS = { single: 'kSingle', notes: 'kNotes', merge: 'kMerge' };

// A beépített prompt összeállítva (a {{LANGUAGE}} a megadott nyelvre cserélve).
export function builtinPrompt(id, language = 'Hungarian', cpp) {
  const { parts, sourceOf } = builtinParts(cpp);
  const key = IDS[id];
  if (!key || !parts[key]) throw new Error('ismeretlen beépített prompt: ' + id);
  let s = parts[key];
  s = s.replaceAll('%SOURCE_MARKERS%', (parts.kSourceMarkers || '').replaceAll('%SOURCE_OF%', sourceOf[key] || ''));
  s = s.replaceAll('%INPUT%', parts.kInput || '');
  s = s.replaceAll('%TOPICS_FORMAT%', parts.kTopicsFormat || '');
  s = s.replaceAll('%TOPICS_RULES%', parts.kTopicsRules || '');
  s = s.replaceAll('%EXAMPLES%', parts.kClassifyExamples || '');
  s = s.replaceAll('%WRITING%', parts.kWriting || '');
  s = s.replaceAll('%CONTEXT_RULE%', parts.kContextRule || '');
  return s.replaceAll('{{LANGUAGE}}', language) + '\n';
}
