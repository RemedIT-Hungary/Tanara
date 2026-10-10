#pragma once
//
// SummarySources — a gyors összefoglaló forrás-hivatkozásai (TISZTA függvények, I/O és LLM nélkül).
//
// A modell a vezetői összefoglaló minden mondata, minden döntés és teendő végére forrás-jelölőt
// tesz: `[t=12:30]` (több is lehet: `[t=12:30, t=14:05]`, tartomány: `[t=12:30-13:10]`), vagy
// megszólalás-azonosítót: `[u754000]`. A json_schema helyi modellel megbízhatatlan, ezért sima
// szöveg + jelölő. Itt:
//   - a jelölők kinyerése és levétele (a tárolt summary.md / mezők jelölő nélküliek),
//   - a vezetői összefoglaló mondatokra bontása,
//   - a jelölt idő → a legközelebbi megszólalás (±kToleranceMs),
//   - a Summary kiegészítése: statements, sourceSpeakers, memó-szakaszok beszélői,
//   - célzott elavulás: mely állítások forrás-sorainak beszélője változott azóta.
//
#include "tanara/Types.h"
#include "tanara/edit/SpeakerEditTypes.h"

#include <QHash>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara {
namespace summarysrc {

// A jelölt idő és a megszólalás közti legnagyobb eltérés.
constexpr qint64 kToleranceMs = 5000;

// Egy megszólalás az átiratból (a forrás-feloldáshoz).
struct SourceLine {
    QString id;             // "u<startMs>" (transcript.segments.json)
    qint64  startMs = 0;
    qint64  endMs = 0;
    QString speakerKey;     // feloldott beszélő-kulcs
    QString speakerName;    // megjelenített név (ahogy az LLM látta)
};

// Egy jelölő egy eleme, nyersen: idő(tartomány) vagy megszólalás-id.
struct SourceRef {
    qint64  startMs = -1;   // a jelölt idő (másodperc-pontosság); -1 = nem idő
    qint64  endMs = -1;     // tartománynál a vége, különben = startMs
    QString utteranceId;    // `[u123]` alak
};

// Egy szöveg(darab) jelölők nélkül + a benne talált hivatkozások.
struct MarkedText {
    QString text;
    QVector<SourceRef> refs;
};

// A beszéd-blokkokból képzett sorok (ha nincs transcript.segments.json): id = "u<startMs>",
// kulcs = név = a blokk beszélője.
QVector<SourceLine> linesFromSegments(const QVector<Utterance>& segments);

// Jelölők kinyerése és levétele. A szöveg szerkezete (sortörések) marad; a jelölő előtti
// szóköz és a jelölő utáni írásjel előtti szóköz eltűnik („szó [t=1:00].” → „szó.”). Az
// értelmezhetetlen idejű jelölő (pl. `[t=12:7]`) is lekerül, hivatkozás nélkül.
MarkedText extractMarkers(const QString& text);
QString stripMarkers(const QString& text);

// Mondatokra bontás (. ! ? … után, ha nagybetű / sortörés / vég jön); a mondat UTÁNI
// jelölő is a mondaté („Mondat. [t=1:00] Következő”). A mondatok jelölő nélküliek.
QVector<MarkedText> splitSentences(const QString& text);

// Hivatkozás → forrás-tartomány. Idő: az a megszólalás, amelyik tartalmazza (a jelölt
// másodpercet), különben a legközelebbi kToleranceMs-on belül; tartomány: az átfedő
// megszólalások. Feloldhatatlan hivatkozás kimarad. Az azonos megszólalások összevonódnak.
QVector<SourceSpan> resolveRefs(const QVector<SourceRef>& refs, const QVector<SourceLine>& lines,
                                qint64 toleranceMs = kToleranceMs);

// A memó-szakaszok beszélői: az időkeretükbe eső megszólalások beszélői (sorrendben).
void fillMemoSpeakers(QVector<MemoSection>& memo, const QVector<SourceLine>& lines);

// A modell kimenetéből épült Summary kiegészítése: a jelölők lekerülnek minden mezőről
// (execSummary, decisions, openQuestions, actionItems, memo), a statements és a
// sourceSpeakers kitöltődik, a memó-szakaszok beszélőket kapnak.
void attachSources(Summary& s, const QVector<SourceLine>& lines);

QString kindToString(StatementKind k);          // "statement" | "decision" | "todo"
StatementKind kindFromString(const QString& s);

// Célzott elavulás. then: az összefoglaló készítésekor rögzített beszélők (Summary::
// sourceSpeakers); now: a mostani feloldás (utteranceId → beszélő); changedKeys: az
// összefoglaló óta javított beszélő-kulcsok (a „Rendben így” után üres → semmi sem érintett).
// Egy forrás-sor érintett, ha a neve azóta más lett, és a régi vagy az új kulcsa javított.
// Kitölti a statement-ek staleBecause / ownerStaleBecause mezőit és az info célzott mezőit
// (az info.stale / correctedSpeakers értékét nem bántja). info.targeted: van-e forrásos állítás.
void applyTargetedStaleness(QVector<SummaryStatement>& statements,
                            const QMap<QString, SourceSpeaker>& then,
                            const QHash<QString, SourceSpeaker>& now,
                            const QStringList& changedKeys, SummaryStaleInfo& info);

} // namespace summarysrc
} // namespace tanara
