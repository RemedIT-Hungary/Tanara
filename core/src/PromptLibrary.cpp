#include "tanara/PromptLibrary.h"
#include "tanara/Paths.h"
#include "tanara/library/TextFold.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHash>

namespace tanara {

// A beépített promptok ANGOLUL (nem felhasználói szöveg; a mérés szerint az angol prompt adta
// a legkevesebb kitalált elemet). A kimenet nyelve a {{LANGUAGE}} helyére kerül (angol
// nyelvnév, lásd summaryLanguageName). A szövegek nyers literálok (R"PROMPT(…)PROMPT"), hogy
// a prompt-kiértékelő (tools/prompt-eval) változtatás nélkül ki tudja olvasni őket.
namespace {

// Az osztályozási példák (en6): a „megbeszélték” ≠ „eldöntötték”, és a felelős az, aki
// MEGCSINÁLJA, nem aki kérte — ez a két leggyakoribb megbízhatósági hiba.
const char* const kClassifyExamples = R"PROMPT(HOW TO CLASSIFY (the examples are in English only to show the logic; they are not from this meeting)
- "Anna: Let's move the release to Friday. — Ben: OK, Friday then."  →  decision: the release moves to Friday.
- "Anna: We could also try the new framework. — Ben: Maybe, let's see."  →  open question: whether to try the new framework. NOT a decision.
- "Ben: We won't need a separate test server after all. — Anna: Agreed."  →  decision: no separate test server is needed.
- "Anna: Ben, can you send me the price by Monday? — Ben: Sure."  →  action item: send the price to Anna, owner Ben (he does it), due Monday. Anna asked, so Anna is NOT the owner.
- "Ben: I'll get access once the director signs the request."  →  open question: access depends on the director's approval. NOT a decision that access is granted.)PROMPT";

// A memó-jegyzet (TOPICS) formája és szabályai — a „notes” és a „single” közös része.
const char* const kTopicsFormat = R"PROMPT(TOPICS
### [mm:ss-mm:ss] short title of the subject
- one point per line: what was said about it, with the concrete names, numbers, dates and reasons; who proposed, objected or reported something when that matters
(one ### block per subject, in the order discussed; the times are where the subject starts and ends))PROMPT";

const char* const kTopicsRules = R"PROMPT(- TOPICS is the memo someone reads to learn what happened: cover every subject up to the last minute, with 2 to 8 points each depending on how much was said.
- A subject is one theme of the discussion and usually lasts a few minutes, rarely more than 10. Split a long discussion into its parts (for example the goal, the cost, who does what, the schedule) instead of writing one long block, but do not start a new block when the same point merely continues after a short aside. 15 minutes of meeting usually contain 2 to 5 subjects. Write the titles in {{LANGUAGE}}. Each point is a short self-contained sentence with the specifics; do not write vague points such as "they discussed the details".
- Things merely shown, explained or reported (for example during a demo or a status update) belong in TOPICS, not among the decisions or tasks.)PROMPT";

const char* const kInput = R"PROMPT(- The transcript was produced by automatic speech recognition: expect misheard words, broken sentences and filler. Speaker labels are mostly right. A label such as "Beszélő 2" means the speaker was not identified.
%CONTEXT_RULE%)PROMPT";

// A felhasználó megjegyzése megbízható: a benne adott javítás („X helyesen: Y”) a kimenetben is érvényes.
const char* const kContextRule = R"PROMPT(- The context notes come from the user and are reliable: when they correct a name or term ("X is really Y"), use the corrected form everywhere in your output.)PROMPT";

const char* const kWriting = R"PROMPT(- Write fluent, correct {{LANGUAGE}}. Do not copy garbled or misheard words from the transcript: restore the intended word from context (and from the glossary when one is given), or leave the detail out when you cannot tell what was meant. Keep product names and technical terms in their original form.)PROMPT";

// Hosszabb megbeszélés: részenkénti jegyzet (map1 + a memó-követelmény).
const char* const kNotes = R"PROMPT(You take notes on ONE PART of a longer meeting, from a speech-to-text transcript. The notes are used twice: they become the detailed memo of the meeting, and a later step builds the executive summary, decisions, open questions and action items from the notes of all parts. So they must be complete, concrete and factual for this part.

INPUT
- Optional context notes about the whole meeting, the subject the previous part ended with (if any), then one part of the transcript. Each paragraph has the form: `[mm:ss]` **Speaker** text
%INPUT%

OUTPUT
Plain text with exactly these four headings, in this order. No text before the first heading and none after the last line, no code fence.

%TOPICS_FORMAT%

DECISIONS
- [mm:ss] what was explicitly agreed or settled (a decision not to do something also counts)

OPEN
- [mm:ss] what was proposed, considered or left pending without agreement, or depends on an approval or on missing information

ACTIONS
- [mm:ss] task — the person who will DO it, exactly as in the speaker labels, or ? when unclear — the deadline, only if one was stated

Under DECISIONS, OPEN and ACTIONS write a single line "- none" when there is nothing.

%EXAMPLES%

RULES
%TOPICS_RULES%
- If this part begins by continuing the subject the previous part ended with, the first block covers only those first minutes and has exactly that title; the subjects that follow get their own blocks.
- Use only what was said in this part. Do not guess. When you are not sure something was agreed, put it under OPEN, not DECISIONS.
- Skip small talk and digressions.
%WRITING%
- Write all notes in {{LANGUAGE}}. The headings TOPICS, DECISIONS, OPEN and ACTIONS stay in English exactly as shown.)PROMPT";

// A részjegyzetek összegzése (red2: „válogass, ne másolj”; a valódi döntés megállapodás).
const char* const kMerge = R"PROMPT(You write the final meeting minutes from notes. The notes were taken part by part, in order, from a speech-to-text transcript of one meeting. Each part has the sections TOPICS (the subjects and what was said about them), DECISIONS, OPEN and ACTIONS. The reader also gets the TOPICS notes as the detailed memo of the meeting, so your output is the short form of the same meeting and must agree with the notes. The DECISIONS, OPEN and ACTIONS notes are over-inclusive on purpose: your job is to SELECT and merge, not to copy everything.

OUTPUT
Return exactly one JSON object with the keys in this order, and nothing else: no code fence, no commentary before or after.
{
  "decisions": ["{{LANGUAGE}} sentence"],
  "openQuestions": ["{{LANGUAGE}} sentence"],
  "actionItems": [{"text": "{{LANGUAGE}} task", "owner": "name", "due": ""}],
  "execSummary": "{{LANGUAGE}} text"
}

FIELD RULES
- decisions: keep a DECISIONS note only if it records an agreement between the participants about what will or will not be done. Drop everything else, even if a part listed it as a decision: descriptions of how a product or feature works, things shown in a demo, status remarks, opinions, one person's plan that nobody confirmed. Merge duplicates; when a later part changes an earlier decision keep only the final state. One self-contained sentence each, without the time. Most meetings have between 0 and 8 real decisions; a longer list means non-decisions slipped in. Use [] when there are none.
- openQuestions: at most 5: the unsettled matters that affect what happens next (pending approvals, choices not yet made, missing information). Drop minor points and points a later part settled. Use [] when there are none.
- actionItems: keep an ACTIONS note only if it names a concrete task someone took on or was asked to do; merge duplicates; drop vague or empty ones. "text" is specific, says what is to be done, and is not phrased as a command to the reader. "owner" is the person who will do the task as written in the notes, or "" when the notes say "?". "due" only when the notes state one, otherwise "".
- execSummary: written last, in {{LANGUAGE}} like the notes: what the meeting was about and what came out of it, written for someone who was not there. Cover the substantial topics of ALL parts in order, including the last parts, and name the concrete things (systems, products, numbers, dates, people). Use 4 to 12 sentences, depending on how much real content there is.

QUALITY RULES
- Use only what is in the notes. Do not add anything, do not turn an OPEN item into a decision, and do not invent owners or deadlines. A wrong decision is worse than a missing one.
%CONTEXT_RULE%
- Write fluent, correct {{LANGUAGE}}. Every string value is in {{LANGUAGE}}; the JSON keys stay exactly as shown above.
- The JSON must be complete and valid: escape double quotes inside strings and use no trailing commas.)PROMPT";

// Egy részből álló (rövid) megbeszélés: memó-jegyzet + a rövid forma EGY hívásban (en6 + TOPICS).
const char* const kSingle = R"PROMPT(You write meeting minutes from a speech-to-text transcript in two steps: first the detailed notes of the meeting (the memo), then the short form as one JSON object (executive summary, decisions, open questions, action items). Both describe the same meeting and must agree.

INPUT
- Optional context notes, then the transcript. Each paragraph has the form: `[mm:ss]` **Speaker** text
%INPUT%

OUTPUT
First the heading TOPICS with the notes, then the heading SUMMARY with exactly one JSON object. Nothing before TOPICS, nothing after the JSON, no code fence.

%TOPICS_FORMAT%

SUMMARY
{
  "execSummary": "{{LANGUAGE}} text",
  "decisions": ["{{LANGUAGE}} sentence"],
  "openQuestions": ["{{LANGUAGE}} sentence"],
  "actionItems": [{"text": "{{LANGUAGE}} task", "owner": "name", "due": ""}]
}

%EXAMPLES%

NOTES RULES
%TOPICS_RULES%
- Skip small talk and digressions.

FIELD RULES
- execSummary: what the meeting was about and what came out of it, written for someone who was not there. Cover every substantial topic in the order it was discussed, up to the end of the meeting, and name the concrete things (systems, products, numbers, dates, people). Use 3 to 10 sentences, depending on how much real content there is.
- decisions: only what the participants explicitly agreed on or settled, including decisions not to do something. One self-contained sentence each. Use [] when there are none.
- openQuestions: at most 5: things proposed, considered or left pending without agreement that affect what happens next. Anything you are not sure was agreed goes here, not into decisions. Use [] when there are none.
- actionItems: only tasks that someone took on or was clearly asked to do. "text" is specific, says what is to be done, and is not phrased as a command to the reader. "owner" is the person who will DO the task, written exactly as in the speaker labels, or "" when it is not clear who does it. "due" is filled only when a time was stated, otherwise "".

QUALITY RULES
- Use only what was said. Do not guess and do not add advice or interpretation. A wrong decision or a wrong owner is worse than a missing one.
- Length follows content, not duration.
%WRITING%
- Write the notes and every JSON string value in {{LANGUAGE}}. The headings TOPICS and SUMMARY and the JSON keys stay exactly as shown above.
- The JSON must be complete and valid: escape double quotes inside strings and use no trailing commas.)PROMPT";

// Témánkénti elemzés, 1. kör: téma-kinyerés (`## cím` + összegzés — a parser erre illeszt).
const char* const kTopic = R"PROMPT(You are an analyst. Identify the SEPARATE TOPICS of the meeting in the speech-to-text transcript (subjects that were actually discussed).
OUTPUT FORMAT — exactly this and nothing else (no introduction, no JSON, no code fence): each topic starts with a line that begins with `## ` and the short TITLE of the topic, followed by a summary of the topic in 1–2 sentences. Example (in English only to show the form):
## Delivery deadlines
The team discussed the Q3 delay and the recovery plan.

## Budget
They reviewed the budget overrun and the options to cover it.

Rules:
1. The number of topics follows the content: few topics (even 1) for a casual or thin conversation, more for an information-dense meeting. Do not over-split and do NOT invent topics.
2. Return only the `## Title` + summary blocks, nothing else.
3. Write all text in {{LANGUAGE}}.
%CONTEXT_RULE%)PROMPT";

// Témánkénti elemzés, 2. kör. A magyar szakaszcímekre illeszt a parser — ezért maradnak magyarul.
const char* const kAnalysis = R"PROMPT(You are a precise note-taker. From the FULL transcript, analyse ONLY the parts about the given TOPIC.
OUTPUT FORMAT — markdown, exactly like this (no JSON, no code fence):
First one paragraph that summarises the topic (without a heading). After it — ONLY when there is real content — these sections may follow:
## Döntések
- one decision per line
## Nyitott kérdések
- one open question per line
## Teendők
- the task — Owner (deadline)
(Owner and deadline are optional; leave them out when unknown.)
Rules:
1. Do NOT invent anything. When the topic has no real decision, open question or task, leave that section out (no empty heading). Focus only on the given topic.
2. A decision is only what the participants explicitly agreed on. Anything proposed, considered or pending goes under `## Nyitott kérdések`. The owner is the person who will DO the task, not the one who asked for it.
3. Write all text in {{LANGUAGE}} — but the section headings `## Döntések`, `## Nyitott kérdések` and `## Teendők` stay EXACTLY like this, in Hungarian.
%CONTEXT_RULE%)PROMPT";

// SZŰK feladat: KIZÁRÓLAG egy rövid vezetői összefoglaló — a teendők összevonását NEM az LLM
// végzi (azt a kód deduplikálja a per-téma elemzésekből), így a kimenet modellfüggetlenül stabil.
const char* const kReduce = R"PROMPT(You are a precise note-taker. From the per-topic analyses, write ONE global executive summary of the whole conversation in 2–4 sentences.
Return ONLY this paragraph and nothing else: no heading, no list, no tasks, no decisions, no JSON, no code fence, no explanation, no reasoning. Write it in {{LANGUAGE}}.
%CONTEXT_RULE%)PROMPT";

QString compose(const char* tmpl)
{
    QString s = QString::fromUtf8(tmpl);
    s.replace(QStringLiteral("%INPUT%"), QString::fromUtf8(kInput));
    s.replace(QStringLiteral("%TOPICS_FORMAT%"), QString::fromUtf8(kTopicsFormat));
    s.replace(QStringLiteral("%TOPICS_RULES%"), QString::fromUtf8(kTopicsRules));
    s.replace(QStringLiteral("%EXAMPLES%"), QString::fromUtf8(kClassifyExamples));
    s.replace(QStringLiteral("%WRITING%"), QString::fromUtf8(kWriting));
    s.replace(QStringLiteral("%CONTEXT_RULE%"), QString::fromUtf8(kContextRule));
    return s + QLatin1Char('\n');
}

} // namespace

QString promptBuiltin(const QString& id)
{
    if (id == QLatin1String("single") || id == QLatin1String("simple"))   // "simple": a régi azonosító
        return compose(kSingle);
    if (id == QLatin1String("notes"))    return compose(kNotes);
    if (id == QLatin1String("merge"))    return compose(kMerge);
    if (id == QLatin1String("topic"))    return compose(kTopic);
    if (id == QLatin1String("analysis")) return compose(kAnalysis);
    if (id == QLatin1String("reduce"))   return compose(kReduce);
    return QString();
}

QString promptFilePath(const QString& id, const QString& metadataDir)
{
    const QString dir = paths::resolveMetadataDir(metadataDir);   // TANARA_HOME-tudatos
    return QDir(dir).filePath(QStringLiteral("prompts/%1.md").arg(id));
}

namespace {
QString readPromptFile(const QString& path)
{
    QFile f(path);
    if (f.exists() && f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString::fromUtf8(f.readAll()).trimmed();
    return QString();
}
} // namespace

QString promptDefault(const QString& id, const QString& metadataDir)
{
    QString text = readPromptFile(promptFilePath(id, metadataDir));
    // A rövid megbeszélés egylépéses promptjának régi fájlneve is érvényes (prompts/simple.md).
    if (text.isEmpty() && id == QLatin1String("single"))
        text = readPromptFile(promptFilePath(QStringLiteral("simple"), metadataDir));
    return text.isEmpty() ? promptBuiltin(id) : text;
}

QString summaryLanguageName(const QString& language)
{
    const QString lang = language.trimmed();
    if (lang.isEmpty()) return QStringLiteral("Hungarian");
    // A gyakori nyelvek magyar / natív / kódnevei → angol név (az angol promptot így követik
    // a modellek megbízhatóan). Minden más változatlanul megy tovább.
    static const QHash<QString, QString> names{
        {QStringLiteral("magyar"), QStringLiteral("Hungarian")},
        {QStringLiteral("magyarul"), QStringLiteral("Hungarian")},
        {QStringLiteral("hungarian"), QStringLiteral("Hungarian")},
        {QStringLiteral("hu"), QStringLiteral("Hungarian")},
        {QStringLiteral("angol"), QStringLiteral("English")},
        {QStringLiteral("angolul"), QStringLiteral("English")},
        {QStringLiteral("english"), QStringLiteral("English")},
        {QStringLiteral("en"), QStringLiteral("English")},
        {QStringLiteral("nemet"), QStringLiteral("German")},
        {QStringLiteral("nemetul"), QStringLiteral("German")},
        {QStringLiteral("deutsch"), QStringLiteral("German")},
        {QStringLiteral("german"), QStringLiteral("German")},
        {QStringLiteral("de"), QStringLiteral("German")},
        {QStringLiteral("francia"), QStringLiteral("French")},
        {QStringLiteral("francais"), QStringLiteral("French")},
        {QStringLiteral("french"), QStringLiteral("French")},
        {QStringLiteral("spanyol"), QStringLiteral("Spanish")},
        {QStringLiteral("espanol"), QStringLiteral("Spanish")},
        {QStringLiteral("spanish"), QStringLiteral("Spanish")},
        {QStringLiteral("olasz"), QStringLiteral("Italian")},
        {QStringLiteral("italiano"), QStringLiteral("Italian")},
        {QStringLiteral("italian"), QStringLiteral("Italian")},
        {QStringLiteral("lengyel"), QStringLiteral("Polish")},
        {QStringLiteral("polski"), QStringLiteral("Polish")},
        {QStringLiteral("cseh"), QStringLiteral("Czech")},
        {QStringLiteral("szlovak"), QStringLiteral("Slovak")},
        {QStringLiteral("roman"), QStringLiteral("Romanian")},
        {QStringLiteral("horvat"), QStringLiteral("Croatian")},
        {QStringLiteral("szerb"), QStringLiteral("Serbian")},
        {QStringLiteral("szloven"), QStringLiteral("Slovenian")},
        {QStringLiteral("ukran"), QStringLiteral("Ukrainian")},
        {QStringLiteral("orosz"), QStringLiteral("Russian")},
        {QStringLiteral("holland"), QStringLiteral("Dutch")},
        {QStringLiteral("portugal"), QStringLiteral("Portuguese")},
        {QStringLiteral("sved"), QStringLiteral("Swedish")},
        {QStringLiteral("finn"), QStringLiteral("Finnish")},
        {QStringLiteral("torok"), QStringLiteral("Turkish")},
        {QStringLiteral("japan"), QStringLiteral("Japanese")},
        {QStringLiteral("kinai"), QStringLiteral("Chinese")},
    };
    const QString key = textfold::foldQuery(lang).trimmed();
    const auto it = names.constFind(key);
    return it != names.constEnd() ? it.value() : lang;
}

QString languageReminder(const QString& language)
{
    const QString user = language.trimmed().isEmpty() ? QStringLiteral("magyar") : language.trimmed();
    const QString name = summaryLanguageName(user);
    // Ha a felhasználó a saját nyelvén adta meg (pl. „magyar”), a natív alak is szerepel —
    // a modell így akkor is érti, ha az angol név félreérthető volna.
    const QString both = name.compare(user, Qt::CaseInsensitive) == 0
        ? name : QStringLiteral("%1 (%2)").arg(name, user);
    return QStringLiteral(
        "\n\n----\nReminder: write the output in %1, even though the instructions are in English. "
        "Keep the headings and JSON keys exactly as specified.").arg(both);
}

QString applySummaryLanguage(QString prompt, const QString& language)
{
    QString lang = language.trimmed();
    if (lang.isEmpty())
        lang = QStringLiteral("magyar");

    const bool hasEnglishVar = prompt.contains(QStringLiteral("{{LANGUAGE}}"));
    const bool hasNativeVar  = prompt.contains(QStringLiteral("{{NYELV}}"));
    if (hasEnglishVar || hasNativeVar) {
        prompt.replace(QStringLiteral("{{LANGUAGE}}"), summaryLanguageName(lang));
        prompt.replace(QStringLiteral("{{NYELV}}"), lang);
        return prompt;
    }

    // Placeholder nélküli (saját/fájl) prompt: magyar célnyelvnél nem nyúlunk hozzá
    // (visszafelé kompatibilis), más célnyelvnél direktívát fűzünk a végére.
    if (summaryLanguageName(lang) == QLatin1String("Hungarian"))
        return prompt;
    return prompt + QStringLiteral(
        "\nFONTOS: a kimenet szövege KIZÁRÓLAG %1 nyelven íródjon. A strukturális "
        "szakaszcímek (pl. `## Döntések`, `## Teendők`) változatlanul magyarul maradnak.\n")
        .arg(lang);
}

QVector<PromptVariable> promptVariables()
{
    return {
        { QStringLiteral("{{LANGUAGE}}"),
          QCoreApplication::translate("PromptLibrary", "az összefoglaló nyelve angolul (pl. Hungarian)") },
        { QStringLiteral("{{NYELV}}"),
          QCoreApplication::translate("PromptLibrary", "az összefoglaló nyelve, ahogy a beállításban áll") },
    };
}

PromptOutputFormat promptOutputFormat(const QString& id)
{
    PromptOutputFormat f;
    if (id == QLatin1String("single") || id == QLatin1String("simple")) {
        f.kind = QStringLiteral("json");   // jegyzet-fej + JSON; a Beállítások JSON-ként mutatja
        f.summary = QStringLiteral("TOPICS (memó) + SUMMARY: execSummary, decisions[], openQuestions[], actionItems[]");
        f.body = QStringLiteral(
            "TOPICS\n"
            "### [mm:ss-mm:ss] <téma címe>\n"
            "- <pont>\n"
            "\n"
            "SUMMARY\n"
            "{\n"
            "  \"execSummary\": string,\n"
            "  \"decisions\": [ string, … ],\n"
            "  \"openQuestions\": [ string, … ],\n"
            "  \"actionItems\": [\n"
            "    { \"text\": string, \"owner\": string, \"due\": string }, …\n"
            "  ]\n"
            "}");
    } else if (id == QLatin1String("notes")) {
        f.kind = QStringLiteral("text");
        f.summary = QStringLiteral("TOPICS, DECISIONS, OPEN, ACTIONS");
        f.body = QStringLiteral(
            "TOPICS\n"
            "### [mm:ss-mm:ss] <téma címe>\n"
            "- <pont>\n"
            "\n"
            "DECISIONS\n"
            "- [mm:ss] <döntés>  |  - none\n"
            "\n"
            "OPEN\n"
            "- [mm:ss] <nyitott kérdés>  |  - none\n"
            "\n"
            "ACTIONS\n"
            "- [mm:ss] <teendő> — <felelős vagy ?> — <határidő>  |  - none");
    } else if (id == QLatin1String("merge")) {
        // A vezetői összefoglaló a VÉGÉN: a (magyar) listák után a Gemma is a célnyelven írja.
        f.kind = QStringLiteral("json");
        f.summary = QStringLiteral("decisions[], openQuestions[], actionItems[], execSummary");
        f.body = QStringLiteral(
            "{\n"
            "  \"decisions\": [ string, … ],\n"
            "  \"openQuestions\": [ string, … ],\n"
            "  \"actionItems\": [\n"
            "    { \"text\": string, \"owner\": string, \"due\": string }, …\n"
            "  ],\n"
            "  \"execSummary\": string\n"
            "}");
    } else if (id == QLatin1String("topic")) {
        f.kind = QStringLiteral("markdown");
        f.summary = QCoreApplication::translate("PromptLibrary", "## Cím + 1–2 mondat, témánként");
        f.body = QCoreApplication::translate("PromptLibrary",
            "## <a téma címe>\n"
            "<1–2 mondatos összegzés>\n"
            "\n"
            "## <a következő téma címe>\n"
            "<1–2 mondatos összegzés>");
    } else if (id == QLatin1String("analysis")) {
        f.kind = QStringLiteral("markdown");
        f.summary = QCoreApplication::translate("PromptLibrary",
                                                "összegzés, ## Döntések, ## Nyitott kérdések, ## Teendők");
        f.body = QCoreApplication::translate("PromptLibrary",
            "<egy bekezdés összegzés a témáról>\n"
            "\n"
            "## Döntések\n"
            "- <egy döntés soronként>\n"
            "\n"
            "## Nyitott kérdések\n"
            "- <egy nyitott kérdés soronként>\n"
            "\n"
            "## Teendők\n"
            "- <a teendő szövege> — <felelős> (<határidő>)");
    } else if (id == QLatin1String("reduce")) {
        f.kind = QStringLiteral("text");
        f.summary = QCoreApplication::translate("PromptLibrary", "egy bekezdés");
        f.body = QCoreApplication::translate("PromptLibrary", "<2–4 mondatos vezetői összefoglaló>");
    }
    return f;
}


} // namespace tanara
