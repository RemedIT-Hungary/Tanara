#include "demo/OverviewDemoFacade.h"

#include "demo/DemoText.h"

#include <QPair>

using tanara::library::OverviewSnapshot;

namespace tanara_demo {

namespace {

// The QML contract's rows (see OverviewViewModel): a source badge, a processing step, a DATA tile,
// a participant row.
QVariantMap source(const QString& icon, const QString& text, bool negative = false, bool accent = false,
                   const QString& detail = QString())
{
    return {{QStringLiteral("icon"), icon}, {QStringLiteral("text"), text},
            {QStringLiteral("negative"), negative}, {QStringLiteral("accent"), accent},
            {QStringLiteral("detail"), detail}};
}

QVariantMap step(const QString& key, const QString& title, const QString& sub, const QString& state,
                 bool parallel = false, int progress = -1)
{
    return {{QStringLiteral("key"), key}, {QStringLiteral("title"), title},
            {QStringLiteral("sub"), sub}, {QStringLiteral("state"), state},
            {QStringLiteral("parallel"), parallel}, {QStringLiteral("progress"), progress}};
}

QVariantMap stat(const QString& label, const QString& value, const QString& sub)
{
    return {{QStringLiteral("label"), label}, {QStringLiteral("value"), value},
            {QStringLiteral("sub"), sub}};
}

QVariantMap personRow(const QString& id, const QString& name, int colorIndex, const QString& sub,
                      const QVariantList& sources, double talkShare, bool self = false,
                      bool suggested = false, bool dimmed = false, bool removable = false)
{
    return {{QStringLiteral("id"), id}, {QStringLiteral("name"), name},
            {QStringLiteral("monogram"), OverviewDemoFacade::monogramOf(name)},
            {QStringLiteral("colorIndex"), colorIndex}, {QStringLiteral("sub"), sub},
            {QStringLiteral("sources"), sources}, {QStringLiteral("talkShare"), talkShare},
            {QStringLiteral("self"), self}, {QStringLiteral("suggested"), suggested},
            {QStringLiteral("dimmed"), dimmed}, {QStringLiteral("removable"), removable},
            {QStringLiteral("speakerKey"), QString()}, {QStringLiteral("anonymous"), false},
            {QStringLiteral("unheard"), false}};
}

// A participant not tied to a speaker (added manually, never spoke): a dim row with a "not heard"
// badge, removable.
QVariantMap unheardRow(const QString& id, const QString& name, int colorIndex)
{
    const QVariantList src{source(QStringLiteral("fingerprint"), OverviewDemoFacade::tr("nem hallottuk"), true, false,
                                  OverviewDemoFacade::tr("Az átiratban nem találtuk a hangját: nincs beszélő a nevén"))};
    QVariantMap row = personRow(id, name, colorIndex, QString(), src, -1.0, false, false, true, true);
    row.insert(QStringLiteral("unheard"), true);
    return row;
}

// The row is one of the transcript's speakers.
QVariantMap withSpeaker(QVariantMap row, const QString& speakerKey, bool anonymous)
{
    row.insert(QStringLiteral("speakerKey"), speakerKey);
    row.insert(QStringLiteral("anonymous"), anonymous);
    return row;
}

} // namespace

OverviewDemoFacade::OverviewDemoFacade(const QString& scenario, QObject* parent)
    : tanara::library::IOverviewFacade(parent), m_scenario(scenario), m_snapshot(build())
{
}

QString OverviewDemoFacade::monogramOf(const QString& name)
{
    const QStringList words = name.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QString out;
    for (const QString& w : words) {
        if (w.startsWith(QLatin1Char('('))) continue;   // "Kovács Lilla (you)"
        out += w.left(1).toUpper();
        if (out.size() == 2) break;
    }
    return out.isEmpty() ? QStringLiteral("?") : out;
}

OverviewSnapshot OverviewDemoFacade::snapshot(const QString&)
{
    return m_snapshot;
}

QString OverviewDemoFacade::addParticipant(const QString& meetingId, const QString& name)
{
    const QString n = name.trimmed();
    if (n.isEmpty())
        return {};
    QVariantList rows = m_snapshot.participants;
    const QString id = QStringLiteral("demo-") + QString::number(rows.size() + 1);
    rows << personRow(id, n, int(rows.size()), QString(),
                      {source(QStringLiteral("user"), tr("kézi"))}, -1.0, false, false, false, true);
    m_snapshot.participants = rows;
    m_snapshot.peopleState = QStringLiteral("list");
    emit changed(meetingId);
    return id;
}

void OverviewDemoFacade::removeParticipant(const QString& meetingId, const QString& id)
{
    // In place: the row disappears.
    QVariantList rows;
    for (const QVariant& v : std::as_const(m_snapshot.participants))
        if (v.toMap().value(QStringLiteral("id")).toString() != id) rows << v;
    m_snapshot.participants = rows;
    emit changed(meetingId);
}

bool OverviewDemoFacade::nameSpeakerInPlace(const QString& meetingId, const QString& speakerKey,
                                            const QString& name)
{
    const QString n = name.trimmed();
    if (n.isEmpty())
        return true;
    QVariantList rows;
    for (const QVariant& v : std::as_const(m_snapshot.participants)) {
        QVariantMap row = v.toMap();
        if (row.value(QStringLiteral("speakerKey")).toString() == speakerKey) {
            row.insert(QStringLiteral("name"), n);
            row.insert(QStringLiteral("monogram"), monogramOf(n));
            row.insert(QStringLiteral("sub"), QString());
            row.insert(QStringLiteral("anonymous"), false);
        }
        rows << row;
    }
    m_snapshot.participants = rows;
    emit changed(meetingId);
    return true;
}

void OverviewDemoFacade::setContextNote(const QString& meetingId, const QString& note)
{
    if (note == m_snapshot.contextNote)
        return;
    m_snapshot.contextNote = note;
    emit changed(meetingId);
}

tanara::library::OverviewSnapshot OverviewDemoFacade::build() const
{
    tanara::library::OverviewSnapshot s;
    // Fictional data with the handoff's names.
    const QString st = m_scenario.isEmpty() ? QStringLiteral("overviewDone") : m_scenario;
    const bool done = st == QLatin1String("overviewDone");
    const bool empty = st == QLatin1String("overviewEmpty");
    const bool nobody = st == QLatin1String("overviewNobody");
    // After recording, before the transcript (like the empty state, "Start transcription" in the step); ...Error: with an error.
    const bool recordedError = st == QLatin1String("overviewRecordedError");
    const bool recorded = recordedError || st == QLatin1String("overviewRecorded");

    // The first transcription runs (two tracks, nobody yet): the phases and "Cancel" in the step.
    const bool transcribingOnly = st == QLatin1String("overviewTranscribing");

    s.hasTranscript = done || nobody;
    s.title = demoText("Negyedéves partnertalálkozó", "Quarterly partner review");
    s.transcribing = !s.hasTranscript && !recorded;
    s.canReanalyze = done || nobody;
    s.allDone = done;
    s.analysisRunning = empty;
    s.approvalPending = st == QLatin1String("overviewProcessing");
    s.approved = done;
    s.candidateCount = 4;
    s.contextNote = (empty || nobody || recorded || transcribingOnly) ? QString()
                      : demoText("Q4 partner review — napirend: Q3 számok, súgóoldalak, számlázási folyamat, Nordvik-megújítás.",
                                 "Q4 partner review — agenda: Q3 numbers, help pages, billing process, Nordvik renewal.");

    QVariantList rows;
    if (done) {
        const QVariantMap callSide = source(QStringLiteral("phone"), tr("hívás"), false, false, tr("A hívás hangján (távoli oldal) beszél"));
        const QVariantMap nordvik = source(QStringLiteral("tag"), QStringLiteral("#Nordvik"), false, false,
                                           tr("Közös címke: %1").arg(QStringLiteral("Nordvik")));
        rows << withSpeaker(personRow(QStringLiteral("p1"), demoText("Kovács Lilla (te)", "Emma Carter (you)"), 0, {},
                                      {source(QStringLiteral("mic"), tr("mikrofon"), false, false, tr("A saját mikrofon sávján beszél"))},
                                      0.22, true), QStringLiteral("S1"), false)
             << withSpeaker(personRow(QStringLiteral("p2"), demoName("Fehér Gábor"), 1, {}, {callSide, nordvik}, 0.26),
                            QStringLiteral("S2"), false)
             << withSpeaker(personRow(QStringLiteral("p3"), demoName("Varga Árpád"), 2, {}, {callSide, nordvik}, 0.19),
                            QStringLiteral("S3"), false)
             << withSpeaker(personRow(QStringLiteral("p4"), demoName("Molnár Eszter"), 3, {}, {callSide}, 0.15),
                            QStringLiteral("S4"), false)
             << withSpeaker(personRow(QStringLiteral("S5"), demoRaw(QStringLiteral("Távoli 1")), 4, tr("névtelen · elnevezhető"),
                                      {callSide}, 0.07),
                            QStringLiteral("S5"), true);
        // Invited but did not speak (added manually): dim, after the speakers.
        rows << unheardRow(QStringLiteral("p5"), demoName("Lantos Réka"), 5);
        s.peopleState = QStringLiteral("list");
        s.peopleHint = tr("jóváhagyva %1-kor").arg(demoText("okt. 1. 16:40", "Oct 1, 4:40 PM"));
    } else if (empty || recorded || transcribingOnly) {
        s.peopleState = QStringLiteral("empty");
        s.peopleHint.clear();
    } else if (nobody) {
        s.peopleState = QStringLiteral("nobody");
        s.peopleHint.clear();
    } else {
        rows << personRow(QStringLiteral("p1"), demoText("Kovács Lilla (te)", "Emma Carter (you)"), 0, {}, {source(QStringLiteral("user"), tr("te")), source(QStringLiteral("mic"), tr("mikrofon"))}, -1.0, true)
             << personRow(QStringLiteral("p2"), demoName("Fehér Gábor"), 1, {},
                          {source(QStringLiteral("fingerprint"), tr("hang 84%"), false, false,
                                  tr("A hangja 84%-ban hasonlít a mentett mintáihoz (2 modell)")),
                           source(QStringLiteral("tag"), QStringLiteral("#Nordvik"), false, false,
                                  tr("Közös címke: %1").arg(QStringLiteral("Nordvik")))},
                          -1.0, false, false, false, true)
             << personRow(QStringLiteral("p3"), demoName("Varga Árpád"), 2, {}, {source(QStringLiteral("fingerprint"), tr("hang 79%")), source(QStringLiteral("tag"), QStringLiteral("#Nordvik"))}, -1.0, false, false, false, true)
             << personRow(QStringLiteral("p4"), demoName("Molnár Eszter"), 3, {}, {source(QStringLiteral("fingerprint"), tr("hang 71%"))}, -1.0, false, false, false, true)
             << personRow(QStringLiteral("p5"), demoName("Lantos Réka"), 4, {}, {source(QStringLiteral("fingerprint"), tr("nem hallottuk"), true)}, -1.0, false, false, true, true)
             << personRow(QStringLiteral("suggest:") + demoName("Szabó Bence"), demoName("Szabó Bence"), -1, tr("javasolt"),
                          {source(QStringLiteral("tag"), tr("a #%1 alapján · %2 / %3").arg(QStringLiteral("Nordvik")).arg(11).arg(14), false, true)},
                          -1.0, false, true, false, true);
        s.peopleState = QStringLiteral("list");
        s.peopleHint = tr("naptár · hang · címke · kézi");
    }
    s.participants = rows;
    s.anonymousSpeakers.clear();
    if (nobody) {
        const QList<QPair<QString, double>> anon = {{tr("Beszélő 1"), 0.46}, {tr("Beszélő 2"), 0.38}, {tr("Beszélő 3"), 0.16}};
        int i = 0;
        for (const auto& [name, share] : anon)
            s.anonymousSpeakers << QVariantMap{{QStringLiteral("name"), name}, {QStringLiteral("share"), share},
                                       {QStringLiteral("colorIndex"), i++}};
    }
    s.sideWho = (empty || nobody || recorded || transcribingOnly) ? QVariantMap()
              : QVariantMap{{QStringLiteral("mic"), demoName("Kovács Lilla")},
                            {QStringLiteral("loopback"), tr("%n résztvevő", nullptr, 4)}};

    QVariantList steps;
    if (empty || nobody || recorded || transcribingOnly) {
        steps << step(QStringLiteral("recording"), tr("Felvétel"), QStringLiteral("0:42:10 · ") + tr("%n sáv", nullptr, 2), QStringLiteral("done"))
              << step(QStringLiteral("tracks"), tr("Sávok"), tr("%n aktív", nullptr, 2), QStringLiteral("done"))
              << step(QStringLiteral("mixdown"), tr("Lekeverés"), tr("%n sávból", nullptr, 2), QStringLiteral("done"));
        if (transcribingOnly)
            steps << step(QStringLiteral("voice"), tr("Hanglenyomat-elemzés"), tr("nem futott"), QStringLiteral("waiting"), true)
                  << step(QStringLiteral("transcript"), tr("Átirat"), QStringLiteral("Soniox · ") + tr("feltöltés"), QStringLiteral("running"), true, -2)
                  << step(QStringLiteral("summary"), tr("Összefoglaló"), tr("az átirat után"), QStringLiteral("waiting"));
        else if (recorded)
            steps << step(QStringLiteral("voice"), tr("Hanglenyomat-elemzés"), tr("nem futott"), QStringLiteral("waiting"), true)
                  << step(QStringLiteral("transcript"), tr("Átirat"), recordedError ? tr("nem sikerült") : tr("még nem indult"),
                          recordedError ? QStringLiteral("attention") : QStringLiteral("waiting"), true)
                  << step(QStringLiteral("summary"), tr("Összefoglaló"), tr("az átirat után"), QStringLiteral("waiting"));
        else if (empty)
            steps << step(QStringLiteral("voice"), tr("Hanglenyomat-elemzés"), QStringLiteral("1 / 2 ") + tr("sáv"), QStringLiteral("running"), true, 40)
                  << step(QStringLiteral("transcript"), tr("Átirat"), QStringLiteral("Soniox · 18% · ") + tr("kb. %n perc", nullptr, 6), QStringLiteral("running"), true, 18)
                  << step(QStringLiteral("summary"), tr("Összefoglaló"), tr("az átirat után"), QStringLiteral("waiting"));
        else
            steps << step(QStringLiteral("voice"), tr("Hanglenyomat-elemzés"), tr("nincs ismert hang"), QStringLiteral("done"), true)
                  << step(QStringLiteral("transcript"), tr("Átirat"), tr("%n megszólalás", nullptr, 412) + QStringLiteral(" · ") + tr("%n beszélő", nullptr, 3), QStringLiteral("done"), true)
                  << step(QStringLiteral("summary"), tr("Összefoglaló"), tr("még nem készült"), QStringLiteral("waiting"));
    } else {
        steps << step(QStringLiteral("recording"), tr("Felvétel"), QStringLiteral("2:11:04 · ") + tr("%n sáv", nullptr, 3), QStringLiteral("done"))
              << step(QStringLiteral("tracks"), tr("Sávok"), tr("%n aktív", nullptr, 2) + QStringLiteral(" · ") + tr("%n kimaradt, nincs rajta beszéd", nullptr, 1), QStringLiteral("done"))
              << step(QStringLiteral("mixdown"), tr("Lekeverés"), tr("%n sávból", nullptr, 2), QStringLiteral("done"));
        if (done)
            steps << step(QStringLiteral("voice"), tr("Hanglenyomat-elemzés"), tr("%n résztvevő", nullptr, 4), QStringLiteral("done"), true)
                  << step(QStringLiteral("transcript"), tr("Átirat"), tr("%n megszólalás", nullptr, 1298) + QStringLiteral(" · ") + tr("%n beszélő", nullptr, 5), QStringLiteral("done"), true)
                  << step(QStringLiteral("summary"), tr("Összefoglaló"), tr("kész"), QStringLiteral("done"));
        else
            steps << step(QStringLiteral("voice"), tr("Hanglenyomat-elemzés"), tr("%n jelölt · jóváhagyásra vár", nullptr, 4), QStringLiteral("attention"), true)
                  << step(QStringLiteral("transcript"), tr("Átirat"), QStringLiteral("Soniox · 62% · ") + tr("kb. %n perc", nullptr, 4), QStringLiteral("running"), true, 62)
                  << step(QStringLiteral("summary"), tr("Összefoglaló"), tr("az átirat után"), QStringLiteral("waiting"));
    }
    s.steps = steps;
    s.stepsSummary = tr("felvétel · lekeverés · átirat · összefoglaló");
    s.stats.clear();
    if (done)
        s.stats << stat(tr("HOSSZ"), QStringLiteral("2:11:04"), tr("felvétel"))
                << stat(tr("BESZÉD"), QStringLiteral("1:52"), tr("%n sávon", nullptr, 2))
                << stat(tr("BESZÉLŐK"), QStringLiteral("5"), tr("%n névtelen", nullptr, 1))
                << stat(tr("MEGSZÓLALÁS"), QStringLiteral("1298"), tr("%n javítva", nullptr, 23))
                << stat(tr("ÁTNÉZENDŐ"), QStringLiteral("84"), tr("az Átirat fülön"))
                << stat(tr("ÖSSZEFOGLALÓ"), tr("kész"), demoText("okt. 1. 17:05", "Oct 1, 5:05 PM"));
    return s;
}


} // namespace tanara_demo
