#pragma once
//
// SummaryPipeline — a gyors összefoglaló TISZTA (I/O- és LLM-mentes) építőkövei.
//
// A folyamat minden hosszra ugyanaz:
//   1. darabolás: az átirat ~15 perces részekre, bekezdés-határon (rövid megbeszélés = 1 rész;
//      a túl rövid utolsó rész az előzőhöz olvad) — splitTranscript,
//   2. jegyzet részenként (LLM, "notes" prompt): TOPICS / DECISIONS / OPEN / ACTIONS — parseNotes,
//   3. memó: a részek TOPICS-jegyzeteiből DETERMINISZTIKUSAN (a részhatáron folytatódó tárgy
//      egy szakasz marad) — assembleMemo,
//   4. összegzés (LLM, "merge" prompt): vezetői összefoglaló + döntések + nyitott kérdések +
//      teendők a jegyzetekből (válogatás, nem másolás) — renderNotesForMerge + parseMergeJson.
// Egy részből álló megbeszélésnél a 2+4 EGY hívás ("single" prompt): TOPICS + SUMMARY-JSON —
// parseSingle. A vezénylés (hívások sorrendje, gyorsítótár, megszakítás) a SummaryService-ben él.
//
#include "tanara/Types.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara {
namespace summarypipe {

// Egy rész célhossza és a záró rész minimuma (ennél rövidebb utolsó rész az előzőhöz olvad).
constexpr qint64 kDefaultPartMs = 15 * 60 * 1000;
// A nyitott kérdések felső korlátja az összegzés után (a hosszú lista a mérés szerint árt).
constexpr int kMaxOpenQuestions = 5;
// Az egymást követő, azonos tárgyú memó-szakaszok legfeljebb eddig a hosszig olvadnak össze.
constexpr qint64 kMaxMemoJoinMs = 20 * 60 * 1000;

struct TranscriptPart {
    int     index = 0;          // 0-tól
    qint64  startMs = 0;        // az első bekezdés kezdete
    qint64  endMs = 0;          // az utolsó bekezdés vége
    QString markdown;           // a rész bekezdései (`[mm:ss]` **Beszélő** szöveg)
};

// A beszéd-blokkok (MergedTranscript::segments) időrendben, ~partMs-es részekre. Egy bekezdés
// sosem vágódik ketté. A rész akkor zárul, ha a következő bekezdés legalább partMs-sel a rész
// kezdete után indul; a partMs/2-nél rövidebb (vagy 1500 karakternél kisebb) utolsó rész az
// előzőhöz olvad. Üres bemenet → üres lista.
QVector<TranscriptPart> splitTranscript(const QVector<Utterance>& segments,
                                        qint64 partMs = kDefaultPartMs);

// A beszéd-blokkok legfeljebb maxChars hosszú (markdown) részekre, sorrendben, bekezdés-
// határon (a kontextushoz igazított újrabontáshoz). Az indexek 0-tól. Üres lista, ha egyetlen
// bekezdés önmagában is hosszabb maxChars-nál (vagy üres a bemenet).
QVector<TranscriptPart> splitTranscriptByChars(const QVector<Utterance>& segments, int maxChars);

// Egy rész jegyzete (a "notes" lépés kimenete, értelmezve).
struct PartNotes {
    int     index = 0;
    qint64  startMs = 0;
    qint64  endMs = 0;
    QVector<MemoSection> topics;   // a rész tárgyai (a memó nyersanyaga)
    QStringList decisions;         // "[mm:ss] szöveg" alakban, ahogy a modell írta
    QStringList open;
    QStringList actions;           // "[mm:ss] teendő — felelős — határidő"
    bool isEmpty() const { return topics.isEmpty() && decisions.isEmpty() && open.isEmpty() && actions.isEmpty(); }
};

// "mm:ss" / "h:mm:ss" (ill. "mmm:ss") → ms; értelmezhetetlen → -1.
qint64 parseTimestamp(const QString& s);
// ms → "mm:ss" (a perc nem fordul át órába — az átirat is így írja).
QString formatTimestamp(qint64 ms);

// A jegyzet-szöveg hibatűrő értelmezése. Elfogadja a fejlécek változatait (## TOPICS, **TOPICS**,
// TOPICS:, magyar TÉMÁK/DÖNTÉSEK/NYITOTT/TEENDŐK), a kódkerítést, a fejléc előtti prózát. A
// tárgy-cím `###`/`##` sor (opcionális [mm:ss-mm:ss] időkerettel) vagy félkövér sor; a
// tárgy-cím nélküli pontok névtelen szakaszba kerülnek. A „- none” sorok kimaradnak. A
// hiányzó időket a rész kerete és a szomszédos tárgyak alapján tölti ki. Ha a szövegben nincs
// felismerhető fejléc, a sorai egyetlen tárgy pontjai lesznek (nem dobjuk el a részt).
PartNotes parseNotes(const QString& raw, const TranscriptPart& part);

// A memó a részek TOPICS-jegyzeteiből: időrendben összefűzve; az egymás után következő,
// ugyanarról szóló szakaszok (azonos vagy nagyon hasonló cím — tipikusan a részhatáron
// folytatódó tárgy) egy szakasszá olvadnak (a pontok sorban, ismétlés nélkül, az időkeret
// kitágul) — de csak kMaxMemoJoinMs hosszig; azon túl azonos címmel, külön szakasz marad.
// A cím nélküli szakasz az előzőhöz csatlakozik (ha van).
QVector<MemoSection> assembleMemo(const QVector<PartNotes>& parts);

// Az összegző lépés bemenete: a beszélők listája + a részek normalizált jegyzetei
// ("=== PART k of n [mm:ss-mm:ss] ===" fejjel, TOPICS / DECISIONS / OPEN / ACTIONS).
QString renderNotesForMerge(const QVector<PartNotes>& parts, const QStringList& speakers);

// A rövid forma mezői (a merge / single lépés JSON-ja).
struct MergeResult {
    bool        ok = false;
    QString     error;            // ok == false: emberi (magyar) magyarázat a hibáról
    QString     execSummary;
    QStringList decisions;
    QStringList openQuestions;
    QVector<ActionItem> actionItems;
};

// Megengedő JSON-kinyerés: kódkerítés le, a próza előtte/utána le (az első '{'-tól a hozzá
// tartozó záró '}'-ig, a sztringeken belüli zárójeleket figyelembe véve).
QByteArray extractJsonObject(const QString& raw);
// A kis modellek gyakori JSON-hibáinak javítása, ahol ez biztonságos: idézőjel nélküli
// (escape-eletlen) belső idézőjel, nyers sortörés a sztringben, záró vessző, csonka vég
// (nyitott sztring / tömb / objektum lezárása), „okos” idézőjelek kulcs körül.
QByteArray repairJson(const QByteArray& json);
// A merge / single JSON hibatűrő értelmezése: hiányzó mező → üres; kulcs-álnevek
// (open_questions, action_items…); tömb helyett sztring és fordítva; a teendő lehet sima
// sztring is. A nyitott kérdések kMaxOpenQuestions-re vágva. ok == false, ha nincs
// értelmezhető objektum, vagy egyik ismert mező sincs benne.
MergeResult parseMergeJson(const QString& raw);

// Az egylépéses ("single") válasz: a JSON előtti rész a jegyzet (TOPICS), a JSON a rövid forma.
struct SingleResult {
    PartNotes   notes;            // üres, ha a modell nem írt jegyzetet (pl. régi saját prompt)
    MergeResult merge;
};
SingleResult parseSingle(const QString& raw, const TranscriptPart& part);

// A beszélők (résztvevők) a beszéd-blokkokból, az első megszólalás sorrendjében.
QStringList speakersOf(const QVector<Utterance>& segments);

// A teljes Summary összeállítása: a rövid forma + memó + résztvevők.
Summary buildSummary(const MergeResult& merge, const QVector<PartNotes>& parts,
                     const QStringList& participants);

} // namespace summarypipe
} // namespace tanara
