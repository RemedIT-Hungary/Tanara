# Slice contract — tags & tag suggestions

Spec: `design/handoff-label/design_handoff_tanara_tags/README.md` (T01–T16, decisions 1–6, data
sketch) and the Hungarian brief `design/cimkek/brief.md` (C01–C09). This file is the interface
between the slices that build it. Do not change it unilaterally; if something here cannot work,
implement the closest thing and say so in your report.

Waves: **wave 1** = `core` (no QML) and `controls` (QML + demo-only view-models) in parallel;
**wave 2** wires the controls to the core in the shell, library, recorder, import, pre-transcript
view, Tags window and Settings.

## Ground rules (all slices)

- Everything the user sees is Hungarian source text (`qsTr`), English comes from `i18n/tanara_en.ts`
  later; nobody edits the `.ts` file.
- No new colours. One new token: `Theme.radiusTag: 4` (controls slice adds it to `Theme.qml`).
- No compatibility with old data formats is required (only one user; audio is the source of
  truth). `meeting.json` and `tags.json` may change freely; migrations are not needed.
- Never read or write `~/.tanara` or `~/Tanara`; tests use `QTemporaryDir`, manual runs use
  `TANARA_HOME=<sandbox>`. Never call a real LM Studio / network endpoint from tests or from a
  build check; use an in-process fake HTTP server (see `tests/unit/test_connection_tester.cpp`,
  `tests/unit/test_llm_context.cpp` for the pattern). Never load a model on the GPU.
- New files are picked up by globs (`core/`, `gui/qml/*.qml`, `gui/qml/src/*`, `tests/`): no
  CMake edits. New QML types need no registration (`QML_ELEMENT`).
- Build in your worktree: `cmake -S . -B build -G Ninja && cmake --build build && ctest --test-dir build`.
  If a QML type is "not a type" after a rename: `rm -rf build/gui/qml/tanara_qml_autogen`.
- Commit on your branch with clear Hungarian commit messages; do not push; do not touch other
  worktrees or `~/projektek/tanara/build/`.

## File ownership

| Slice | Owns |
|---|---|
| **core** (wave 1) | `core/include/tanara/tags/**`, `core/src/tags/**`, `core/include/tanara/embedding/**`, `core/src/embedding/**`, additive changes in `Types.h`, `AppController`, `MeetingStore`, `MeetingLibrary`, `SettingsManager`, `ProviderRegistry`/`ProviderDescriptor`, `ReadinessModel`, `PromptLibrary`, `SummaryService` (post-summary hook), `tests/unit/test_tags*.cpp`, `test_embedding*.cpp`, `test_library.cpp` additions |
| **controls** (wave 1) | `gui/qml/Tag*.qml`, `gui/qml/Tags*.qml`, `gui/qml/src/Tag*`, `gui/qml/src/Tags*`, a "Címkék" section in `Gallery.qml`, `Theme.radiusTag`, `tests/ui/test_tag*.cpp`, `tests/ui/test_tags*.cpp` |
| wave 2 (later) | `MeetingHeader.qml`, `LibrarySidebar.qml`, `LibraryItem.qml`, `LibraryListModel`, `ShellActions`, `ShellBridge`, `Main.qml`, `PreTranscriptView.qml`, `ShellImportDialog.qml`, `Recorder*.qml`/`RecorderViewModel`, `Settings*`, `gui/src/**`, plus wiring inside the controls' view-models |

Nobody edits another slice's files. A core change the controls slice needs: write it in the
report, do not do it.

## Core API (core slice implements; wave 2 and the view-models build on it)

All in namespace `tanara`, headers under `core/include/tanara/tags/` and `.../embedding/`.
Reachable from `AppContext::instance()->controller()`.

### Data (`tags/TagTypes.h`)

```cpp
struct Tag { QString id; QString name; QDateTime createdAt; };      // id: QUuid without braces
struct TagUsage { Tag tag; int meetingCount = 0; QDateTime firstUsedAt, lastUsedAt; };
enum class TagSource { Manual, Suggestion, Llm, Bulk };
enum class SuggestionSource { Similar, Cooccur, Llm };
enum class ReasonKind { Participant, Terms, Title };
struct SuggestionReason { ReasonKind kind; QStringList values; };
struct MeetingRef { QString meetingId; QString title; QDateTime startedAt; };
struct TagSuggestion {
    QString tagId;                 // empty when isNew
    QString name;
    bool isNew = false;            // LLM-invented name, not in the set yet
    SuggestionSource source = SuggestionSource::Similar;
    double score = 0.0;            // 0..1, for ordering only
    QVector<SuggestionReason> reasons;
    QVector<MeetingRef> similarMeetings;   // "Hasonló megbeszélés" links
};
struct TagProfile {                // derived, read-only
    QString tagId;
    int meetingCount = 0;
    QVector<QPair<QString,int>> topParticipants;   // name, in how many of the tag's meetings
    QStringList topTerms;                          // ≤ 12
    QVector<QPair<QString,int>> cooccurring;       // tagId, count
    QVector<MeetingRef> meetings;                  // newest first
};
```

`Meeting` (Types.h) gets `QStringList tagIds;` persisted in `meeting.json` as `"tags": [...]`.
`LibraryEntry` gets `QStringList tagIds, tagNames;` and `bool tagMatch` + `QString tagMatchName`
(search hit in a tag). `LibraryQuery` gets `QStringList tags; bool tagsAll = false; bool untagged = false;`
(tags = ids; `tagsAll` = AND, else OR) and `isEmpty()` accounts for them. Search text also
matches tag names (accent/case-insensitive, `textfold`). `MeetingLibrary::tagOptions()` returns
`QVector<TagUsage>` for the filter popover and `untaggedCount()`.

### Name normalisation (`tags/TagNames.h`, free functions)

`QString normalizeTagName(const QString&)` — NFC, trimmed, inner whitespace collapsed, for
display; `QString tagKey(const QString&)` — folded (`textfold::fold`), lower-case, spaces and
punctuation removed, for equality; `bool nearDuplicate(const QString& a, const QString& b)` —
same key, or Damerau-Levenshtein ≤ 1 on keys of length ≥ 5 (≤ 2 for length ≥ 10).

### `TagService` (QObject; `AppController::tags()`)

Storage: `<metadataDir>/tags.json` `{ "version": 1, "tags": [{id,name,createdAt}], "rejected": [{meetingId, tagId?, name?}] }`
written atomically (same pattern as `PeopleStore`). Per-meeting assignment lives in
`meeting.json` (`Meeting::tagIds`) and goes through `MeetingStore::saveMeeting`.

| Member | Meaning |
|---|---|
| `QVector<TagUsage> all(Sort = LastUsed) const` | `Sort { LastUsed, Alpha, MostUsed }` |
| `Tag tag(const QString& id) const` | empty id when unknown |
| `Tag byName(const QString& name) const` | by `tagKey` equality |
| `QVector<Tag> similarNames(const QString& name, int limit = 3) const` | `nearDuplicate` hits, excluding exact key match |
| `QVector<TagInputRow> inputRows(const QString& typed, int limit = 5) const` | the input popover rows in display order: `TagInputRow { enum Kind { Recent, Match, New, NearDuplicate, ForceNew } kind; Tag tag; int meetingCount; int matchStart, matchLen; }` — empty text → recents; typed → matches (prefix first, then substring; accent/case-insensitive) + `New` row unless an exact key match exists; near-duplicate → `NearDuplicate` (existing, selected) then `ForceNew` |
| `Tag create(const QString& name)` | returns the existing tag when the key already exists |
| `bool rename(const QString& id, const QString& name)` | false when the new key belongs to another tag |
| `void merge(const QString& fromId, const QString& keepId)` | re-tags every meeting, removes `fromId`; afterwards `inputRows(oldName)` offers the kept tag |
| `void remove(const QString& id)` | meetings keep their other tags; rejected entries for it are dropped |
| `QStringList tagsOf(const QString& meetingId) const` | ids in the meeting's order |
| `Tag addTag(const QString& meetingId, const QString& nameOrId, TagSource)` | creates by name when needed; no-op when already on it |
| `void removeTag(const QString& meetingId, const QString& id)` | |
| `void setTags(const QString& meetingId, const QStringList& ids, TagSource)` | |
| `void bulkAdd(const QStringList& meetingIds, const QString& id)` / `bulkRemove(...)` | one undo step for the caller (see undo below) |
| `QVector<Tag> recent(int limit = 5) const` | by last use |
| `TagProfile profile(const QString& id) const` | computed from meetings + `MeetingProfiles` |
| `QVector<QPair<QString,int>> cooccurring(const QString& id) const` | |
| `void reject(const QString& meetingId, const TagSuggestion&)` | stored per (meeting, tagId or name key) |
| `bool isRejected(const QString& meetingId, const QString& tagIdOrName) const` | |
| `int rejectedCount() const` / `void clearRejected()` | Settings "Elutasított javaslatok: 12 · Visszaállítás" |
| signals `tagsChanged()`, `meetingTagsChanged(QString meetingId)`, `rejectedChanged()` | |

Undo: `TagService` exposes `QString beginGroup(const QString& label)` / `endGroup()` and
`bool canUndo() const`, `QString undoLabel() const`, `void undo()`: every mutating call is
recorded; a group is one step. Keep it simple (a stack of inverse operations, capped at 50).

### Similarity (`tags/MeetingProfiles.h`)

`MeetingProfiles` (QObject, owned by the controller) builds and caches one **profile** per
meeting from `meeting.json` + the transcript lines (`loadTranscriptLines(folder)`), stored as
`<meeting folder>/profile.json` and rebuilt when the transcript's mtime changes:

- `participants`: named speakers (speakerMap values + the user's own name), weighted by rarity
  across the library (a person present in > 60 % of meetings weighs ~0);
- `titleWords`: `meetingnotes::titleWords`;
- `terms`: ≤ 60 characteristic terms. Tokenise the transcript (fold, drop digits/short/stop words
  — reuse `meetingnotes` word lists), light Hungarian suffix stripping (strip the longest
  matching of a fixed suffix list: `-ban/-ben/-nak/-nek/-val/-vel/-ból/-ből/-ról/-ről/-hoz/-hez/-höz/-ba/-be/-ra/-re/-on/-en/-ön/-t/-k/-i/-ja/-je/-ok/-ek/-ök/-unk/-ünk/-nál/-nél/-ig/-ért/-kat/-ket/-at/-et/-ot/-öt` applied once, only when the stem keeps ≥ 4 chars),
  weight = tf × log(N / df) over the library; **rare terms whose stems are within
  Damerau-Levenshtein 1 (length ≥ 6) are merged** (mishearings: "remedi" ~ "remedit"). Keep the
  original most frequent spelling as the display form.

`QVector<SimilarHit> similar(const QString& meetingId, int limit = 8) const` with
`SimilarHit { QString meetingId; double score; QVector<SuggestionReason> reasons; }`:
score = 0.45·participants (weighted Jaccard) + 0.40·terms (cosine over weights) + 0.15·title
(`meetingnotes::titleSimilarity`); reasons list the shared participants, the top 3 shared terms
(display form) and the title when it contributed. Profiles are built lazily on a worker thread
(`QtConcurrent` or a `QThread`), never on the GUI thread; `profileReady(meetingId)` signal.

### Embeddings (`embedding/`)

```cpp
struct EmbeddingRequest { QStringList texts; QString model; };
class EmbeddingJob : public QObject { signals: void finished(QVector<QVector<float>> vectors); void failed(QString error); public: virtual void cancel() = 0; };
class IEmbeddingProvider { virtual QString name() const = 0; virtual EmbeddingJob* embed(const EmbeddingRequest&) = 0; };
```

- `OpenAiCompatibleEmbeddingProvider` — `POST {baseUrl}/embeddings` `{model, input:[...]}` →
  `data[].embedding`; batches of ≤ 16 texts; same `ProviderConfig` fields as the LLM path
  (baseUrl, model, apiKey, extraHeaders, onExchange). Register in a new
  `EmbeddingProviderRegistry` (same shape as `LlmProviderRegistry`), `ProviderKind::Embedding`,
  descriptor id `"openai-compat-embedding"` (fields: baseUrl Url, model Combo dynamicOptions;
  probe `/models` listsModels) and `"tanara-hosted-embedding"` (AuthMode::Login, no fields;
  the gateway route is `/v1/embeddings` behind the same login as the LLM route — the server
  side is a separate task, so the client must fail cleanly with the structured cloud error).
- `AppSettings`: `QString embeddingProviderId;` (`""` = none/"alap"), `QMap<QString, ProviderConfig> embeddingConfigs;`,
  `ProviderConfig embeddingSelected() const`, `bool tagSuggestions = true;`,
  `bool llmTagSuggestions = true;`. `SettingsManager` persists them like the LLM ones.
- `EmbeddingIndex` (QObject): per meeting `<folder>/text.embeddings.bin` — header
  `{magic "TNEMB1", model name, dim, chunkCount}` then chunks `{startMs, endMs, float[dim]}`.
  Chunking: utterance-aligned windows of ~1500 chars. Meeting vector = normalised mean.
  `bool has(meetingId)`, `QVector<SimilarHit> similar(meetingId, limit)` (cosine of meeting
  vectors; reasons: `ReasonKind::Terms` with the top shared terms from `MeetingProfiles` so the
  "Miért?" popover still has text), `void invalidateAll()` on model change.
- `EmbeddingPreparer` (QObject, one at a time): prepares every meeting with a transcript and no
  (or stale/other-model) index. `state()` → `EmbeddingState { enum Status { Idle, Running, Error, Done } status; QString provider, model; int prepared, total; QString error; QDateTime lastRun; int etaSec; }`,
  `start()`, `cancel()`, `resume()`, signal `stateChanged()`. Persist `lastRun`/`model` in
  `<metadataDir>/embedding-state.json`. The GUI may keep working meanwhile; a meeting that
  gets a transcript later is embedded in the background when a provider is configured.

### `TagSuggester` (`tags/TagSuggester.h`)

`QVector<TagSuggestion> suggest(const QString& meetingId)`: neighbours = `MeetingProfiles::similar`
fused with `EmbeddingIndex::similar` by **reciprocal rank fusion** (k = 60) when the index has
the meeting; each neighbour's tags vote with its fused score; a tag's vote is divided by
`log(2 + its global meeting share × 10)` so tags on everything do not win; drop tags already
on the meeting and rejected ones; keep votes ≥ 0.15 of the best, max 6, `source = Similar`,
reasons merged from the voting neighbours, `similarMeetings` = up to 3 voters.
`QVector<TagSuggestion> cooccur(const QString& meetingId, const QString& justAddedTagId)`:
tags that appear with it in ≥ 50 % of its meetings (≥ 2 meetings), `source = Cooccur`, label
"<Tag> mellé gyakran".

LLM step (`tags/LlmTagSuggester.h`): after a summary finishes and `llmTagSuggestions` is on,
`AppController` runs one chat call with a new `PromptLibrary` prompt `"tags"` (English, like the
others): the summary text + a numbered list of candidate tags (the `suggest()` result plus the
top-20 most used tags, each with its one-line profile: top participants, top terms, 2 example
titles). The model answers **plain lines**: `pick: 3, 7` and `new: Name1; Name2` (no JSON —
json_schema is unreliable locally). Parse leniently; `pick` → existing tags with
`source = Llm`; `new` → `isNew` suggestions (max 2, skipped when `nearDuplicate` of an existing
tag — then that tag is suggested instead). Reasoning off as in the summary path.

### `AppController` additions

| Member | Meaning |
|---|---|
| `TagService* tags()`, `MeetingProfiles* profiles()`, `EmbeddingIndex* embeddings()`, `EmbeddingPreparer* embeddingPreparer()` | |
| `void requestTagSuggestions(const QString& meetingId)` | async; emits `tagSuggestionsComputing(meetingId)` then `tagSuggestionsReady(meetingId, QVector<TagSuggestion>)`. Called by the shell when a meeting is shown / its transcript arrives; cached per meeting until tags, transcript or the index change |
| `void requestCooccurSuggestions(const QString& meetingId, const QString& tagId)` | emits `tagSuggestionsReady` with `source = Cooccur` |
| `void requestLlmTagSuggestions(const QString& meetingId)` | emits the same signals with `source = Llm`; runs automatically after `summaryReady` when the setting is on |
| `void setEmbeddingProvider(...)` | via settings; a model change → `EmbeddingIndex::invalidateAll()` + preparer restart |
| `QVector<TagSuggestion> pendingTagSuggestions(const QString& meetingId) const` | last result (any source) for a view-model that connects late |

`ReadinessModel`: embedding is never a blocker; `canRun` is unchanged.

### Tests (core)

`test_tags_service` (CRUD, key equality, near-duplicates, merge, undo groups, rejected),
`test_tags_profiles` (suffix stripping, mishearing merge, similarity ordering on a 6-meeting
fixture with fictional transcripts, rarity weighting of participants), `test_tags_suggester`
(voting, co-occurrence, rejected/applied filtering, RRF when a fake index is present),
`test_embedding` (provider against a fake HTTP server, index file round-trip, preparer
progress/cancel/error/resume, model-change invalidation), `test_library` additions (tag filter
any/all/untagged, search in tag names, `tagOptions`). `test_llm_tag_suggester` (prompt
assembly, lenient parsing).

## Controls (controls slice implements; wave 2 wires)

All components tolerate `null` models and render fictional sample data alone (for `--qml-shot`
and `Gallery.qml`). Never ship real names.

| QML | Props / signals |
|---|---|
| `TagChip.qml` | `text`, `kind: "applied" \| "suggested" \| "llmNew" \| "overflow" \| "partial"`, `compact: bool` (22 px), `removable: bool` (× shown on hover/focus for applied; always for suggested), `countText` ("1/3" for partial), `toolTipText`; signals `clicked()`, `removeRequested()`. Sizes/colours per the handoff "Tokens" table; max width 180 → elide + tooltip. `#` glyph in Plex Mono 12, "+" for suggested, sparkles icon + "ÚJ" badge for llmNew |
| `TagInput.qml` | the inline field (24 px, `standalone: bool` → 28 px) + `TagInputPopover.qml` (300 px; `compact` → 250 px, 28 px rows, max 3). Props: `model` (a `TagInputModel`), `compact`, `placeholderText`; signals `tagChosen(string name, bool isNew)`, `backspaceOnEmpty()`, `closed()`; `function open()`. Keys per spec (Enter, ↑↓, Esc, Backspace). Section headers "LEGUTÓBB HASZNÁLT" / "HASONLÓ MÁR VAN", match highlight `warnSoft`, counts mono, footer hint |
| `TagField.qml` | multi-chip field (36 px) for step 1 / import / bulk panel: `tags` (list of `{id,name}`), `placeholderText`, `countText` for partial chips; signals `addRequested(string name, bool isNew)`, `removeRequested(string id)`; embeds `TagInput` |
| `TagRow.qml` | the header row (26 px, never wraps): `tags`, `suggestions` (list of `{id,name,isNew,source}`), `suggestionLabel` ("Javasolt" / "<Tag> mellé gyakran" / "Az összefoglaló alapján" with ✦), `computing: bool`, `editable: bool`; shows ≤ 2 suggestions + "+N", "Mind", "(i) Miért?"; signals `addRequested(name,isNew)`, `removeRequested(id)`, `acceptRequested(index)`, `rejectRequested(index)`, `acceptAllRequested()`, `whyRequested()`, `tagClicked(id)`; `T` opens the input when the row has focus |
| `TagWhyPopover.qml` | 420 px, per suggestion: chip, Hozzáadás (primary 26 px), Nem illik ide, reasons grid `130px | 1fr`, similar-meeting links; footer sentence. Props `suggestions` (with `reasons[{kind,values}]`, `similarMeetings[{meetingId,title,dateText}]`); signals `acceptRequested(index)`, `rejectRequested(index)`, `meetingRequested(meetingId)` |
| `TagsWindow.qml` | the manager window 960 × 660, structure of `PeopleWindow.qml` (list 288 px: search, count, sort; detail: tile, name, meta, Átnevezés / Összevonás… / delete; info line; JELLEMZŐ RÉSZTVEVŐK, JELLEMZŐ KIFEJEZÉSEK (round `sunken` pills, no "#"), GYAKRAN EGYÜTT, MEGBESZÉLÉSEK + "Megnyitás a könyvtárban szűrőként"); `TagsMergeDialog.qml` (500 px), delete confirmation, empty state T13. `demoState`: `"T10" | "T11" | "T12" | "T13" | "rename"` |

View-models (`gui/qml/src/`, `QML_ELEMENT`, namespace `tanara_qml`), **demo data in wave 1**, a
`controller` property (QObject*) that wave 2 connects to the core API above:

- `TagInputModel` — `text` (rw), `rows` (list of `{kind, id, name, count, matchStart, matchLen}`),
  `selectedRow` (rw), `move(delta)`, `chooseSelected()` → emits `chosen(name, isNew)`;
  without controller: a fictional set of 12 tags (use the names from the handoff screenshots).
- `MeetingTagsModel` — `meetingId` (rw), `tags`, `suggestions`, `suggestionLabel`,
  `suggestionSource`, `computing`, `canUndo`, `undoLabel`; `add(name)`, `remove(id)`,
  `accept(index)`, `reject(index)`, `acceptAll()`, `undo()`, `whyData()`; `demoState`:
  `"none" | "few" | "many" | "computing" | "similar" | "cooccur" | "llm"` (the C03–C04 rows of T01).
- `TagsViewModel` + `TagListModel` (manager; mirror `PeopleViewModel`/`PeopleListModel`):
  `filter`, `sort`, `count`, `selectedId`, `detail` (name, meta, participants, terms,
  cooccurring, meetings), `rename(id, name)`, `mergeCandidates(id)`, `merge(from, keep)`,
  `remove(id)`, `toastText`; `demoState` as the window.

Tests: `tests/ui/test_tag_input_model.cpp`, `test_meeting_tags_model.cpp`, `test_tags_view_model.cpp`
(demo paths + the controller-facing seams mocked), and a `--qml-shot` smoke for `TagsWindow`
and the Gallery section (see `test_job_views_render.cpp`).

Screenshots for the report: `gui/qml/shoot.sh 'Gallery:1280x1400:section="tags"'` (add a
`section` prop to `Gallery.qml` that scrolls to / shows only that section) and
`'TagsWindow:960x660:demoState="T10"'`.

## Wave 2 placement (for reference; owned later)

Header: `TagRow` under the meta line, above the tabs, on every tab; before transcript it moves
into step 1 as a `TagField` + suggestion line. Library row: third line `#A  #B  +1` (12 px muted
text, not chips), filter popover (ÁLLAPOT / CÍMKE with Bármelyik / Mindegyik, search, counts,
"Címke nélkül", "Címkék kezelése…"), search hit line "címke: **#X**", multi-select with
checkboxes + right-pane bulk panel. Sidebar footer: Személyek · Címkék · ⚙. Recorder: chip row
under the title, `Ctrl+T`, before and during recording, also collapsed; not in the pill. Import
dialog: `TagField` under Cím / Mikor készült. Settings › Szolgáltatások: collapsed one-line
cards for configured roles + the **Beágyazás** card (Nincs (alap) / Helyi végpont / Tanara
Cloud, KÖNYVTÁR ELŐKÉSZÍTÉSE progress/error/done, model-change warning before saving, "Mentés és
újraindítás"), CÍMKEJAVASLATOK switches, "Elutasított javaslatok: N · Visszaállítás".
