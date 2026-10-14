#include "demo/OverviewDemoFacade.h"

#include "demo/DemoText.h"

#include <QPair>
#include <QSignalBlocker>

#include <algorithm>

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
            {QStringLiteral("unheard"), false},
            {QStringLiteral("sampleTrackId"), QString()}, {QStringLiteral("sampleStartMs"), qint64(-1)},
            {QStringLiteral("sampleEndMs"), qint64(-1)}, {QStringLiteral("approved"), false},
            {QStringLiteral("doubtful"), false}, {QStringLiteral("counted"), !suggested && !dimmed},
            {QStringLiteral("voiceless"), false}, {QStringLiteral("lineCount"), 0}};
}

// The card's decision keys of a row (see OverviewViewModel): taken by "Approve · N", doubtful (left out
// unless "Was there"), no voice of theirs.
QVariantMap withCard(QVariantMap row, bool counted, bool doubtful = false, bool voiceless = false)
{
    row.insert(QStringLiteral("counted"), counted);
    row.insert(QStringLiteral("doubtful"), doubtful);
    row.insert(QStringLiteral("voiceless"), voiceless);
    return row;
}

// An unknown voice of the analysis ("Unknown voice N"): nameable, not counted.
QVariantMap voiceRow(const QString& id, int n, int colorIndex, const QVariantList& sources, double talkShare)
{
    QVariantMap row = personRow(id, OverviewDemoFacade::tr("Ismeretlen hang %1").arg(n), colorIndex,
                                OverviewDemoFacade::tr("névtelen · elnevezhető"), sources, talkShare);
    row.insert(QStringLiteral("monogram"), QStringLiteral("?"));
    row.insert(QStringLiteral("anonymous"), true);
    return withCard(row, false);
}

// A fictional 6 s sample on the demo's mic / call track (the silent demo engine "plays" it).
QVariantMap withSample(QVariantMap row, const QString& trackId, qint64 startMs)
{
    row.insert(QStringLiteral("sampleTrackId"), trackId);
    row.insert(QStringLiteral("sampleStartMs"), startMs);
    row.insert(QStringLiteral("sampleEndMs"), startMs + 6000);
    return row;
}

// A participant not tied to a speaker (added manually, never spoke): a dim row with a "not heard"
// badge, removable.
QVariantMap unheardRow(const QString& id, const QString& name, int colorIndex)
{
    const QVariantList src{source(QStringLiteral("fingerprint"), OverviewDemoFacade::tr("nem hallottuk"), true, false,
                                  OverviewDemoFacade::tr("Az átiratban nem találtuk a hangját: nincs beszélő a nevén"))};
    QVariantMap row = personRow(id, name, colorIndex, QString(), src, -1.0, false, false, true, true);
    row.insert(QStringLiteral("unheard"), true);
    return withCard(row, false, false, true);
}

// The row is one of the transcript's speakers.
QVariantMap withSpeaker(QVariantMap row, const QString& speakerKey, bool anonymous)
{
    row.insert(QStringLiteral("speakerKey"), speakerKey);
    row.insert(QStringLiteral("anonymous"), anonymous);
    return row;
}

// The scenarios that show the "Who was there?" card with candidates, before the transcript (the
// decision pending, the card's panels, the decided states).
bool candidateScenario(const QString& st)
{
    static const QStringList all = {
        QStringLiteral("overviewProcessing"), QStringLiteral("overviewNaming"), QStringLiteral("overviewSameVoice"),
        QStringLiteral("overviewWhichVoice"), QStringLiteral("overviewVoiceless"), QStringLiteral("overviewThreeTracks"),
        QStringLiteral("overviewApproved"), QStringLiteral("overviewSkipped"), QStringLiteral("overviewSolo")};
    return all.contains(st);
}

} // namespace

void OverviewDemoFacade::touch(const QString& meetingId)
{
    m_snapshot.approveCount = int(std::count_if(m_snapshot.participants.cbegin(), m_snapshot.participants.cend(),
                                                [](const QVariant& v) {
        const QVariantMap row = v.toMap();
        return row.value(QStringLiteral("counted")).toBool() && !row.value(QStringLiteral("anonymous")).toBool();
    }));
    emit changed(meetingId);
}

OverviewDemoFacade::OverviewDemoFacade(const QString& scenario, QObject* parent)
    : tanara::library::IOverviewFacade(parent), m_scenario(scenario), m_snapshot(build())
{
    const QSignalBlocker quiet(this);
    applyScenarioEdits();
    touch(QString());
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
    rows << withCard(personRow(id, n, int(rows.size()), QString(),
                               {source(QStringLiteral("user"), tr("kézi"), false, false, tr("Kézzel adtad hozzá"))},
                               -1.0, false, false, false, true),
                     true, false, true);
    // A suggestion with this name is now a participant.
    for (int i = int(rows.size()) - 2; i >= 0; --i)
        if (rows.at(i).toMap().value(QStringLiteral("suggested")).toBool()
            && rows.at(i).toMap().value(QStringLiteral("name")).toString() == n)
            rows.removeAt(i);
    m_snapshot.participants = rows;
    m_snapshot.peopleState = QStringLiteral("list");
    touch(meetingId);
    return id;
}

void OverviewDemoFacade::removeParticipant(const QString& meetingId, const QString& id)
{
    // In place: the row disappears.
    QVariantList rows;
    for (const QVariant& v : std::as_const(m_snapshot.participants))
        if (v.toMap().value(QStringLiteral("id")).toString() != id) rows << v;
    m_snapshot.participants = rows;
    touch(meetingId);
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
    touch(meetingId);
    return true;
}

int OverviewDemoFacade::rowIndex(const QString& id) const
{
    for (int i = 0; i < m_snapshot.participants.size(); ++i) {
        const QVariantMap row = m_snapshot.participants.at(i).toMap();
        if (!id.isEmpty()
            && (row.value(QStringLiteral("id")).toString() == id || row.value(QStringLiteral("speakerKey")).toString() == id))
            return i;
    }
    return -1;
}

bool OverviewDemoFacade::nameParticipant(const QString& meetingId, const QString& id, const QString& personIdOrName)
{
    const QString n = personIdOrName.trimmed();
    const int at = rowIndex(id);
    if (n.isEmpty() || at < 0)
        return false;
    QVariantList rows = m_snapshot.participants;
    QVariantMap row = rows.at(at).toMap();
    row.insert(QStringLiteral("name"), n);
    row.insert(QStringLiteral("monogram"), monogramOf(n));
    row.insert(QStringLiteral("sub"), QString());
    row.insert(QStringLiteral("anonymous"), false);
    row.insert(QStringLiteral("counted"), true);
    row.insert(QStringLiteral("doubtful"), false);
    // The "unknown voice" badge gives way to the manual naming.
    QVariantList sources;
    for (const QVariant& src : row.value(QStringLiteral("sources")).toList())
        if (src.toMap().value(QStringLiteral("text")).toString() != tr("ismeretlen hang")) sources << src;
    sources.prepend(source(QStringLiteral("user"), tr("kézi"), false, false, tr("Kézzel nevezted el")));
    row.insert(QStringLiteral("sources"), sources);
    rows[at] = row;
    // The person's manual row without a voice (no sample) and their suggestion dissolve into the named one.
    for (int i = int(rows.size()) - 1; i >= 0; --i) {
        const QVariantMap other = rows.at(i).toMap();
        if (i != at && other.value(QStringLiteral("name")).toString() == n
            && other.value(QStringLiteral("sampleTrackId")).toString().isEmpty())
            rows.removeAt(i);
    }
    m_snapshot.participants = rows;
    touch(meetingId);
    return true;
}

bool OverviewDemoFacade::mergeParticipantInto(const QString& meetingId, const QString& fromId, const QString& intoId)
{
    const int from = rowIndex(fromId), into = rowIndex(intoId);
    if (from < 0 || into < 0 || from == into)
        return false;
    QVariantList rows = m_snapshot.participants;
    const QVariantMap f = rows.at(from).toMap();
    QVariantMap t = rows.at(into).toMap();
    if (t.value(QStringLiteral("anonymous")).toBool() && !f.value(QStringLiteral("anonymous")).toBool()) {
        for (const char* key : {"name", "monogram", "sub", "anonymous", "self", "counted", "doubtful"})
            t.insert(QString::fromLatin1(key), f.value(QString::fromLatin1(key)));
    }
    const double a = t.value(QStringLiteral("talkShare")).toDouble(), b = f.value(QStringLiteral("talkShare")).toDouble();
    if (a >= 0 && b > 0) t.insert(QStringLiteral("talkShare"), a + b);
    rows[into] = t;
    rows.removeAt(from);
    m_snapshot.participants = rows;
    touch(meetingId);
    return true;
}

bool OverviewDemoFacade::confirmParticipant(const QString& meetingId, const QString& id, bool confirmed)
{
    const int at = rowIndex(id);
    if (at < 0)
        return false;
    QVariantMap row = m_snapshot.participants.at(at).toMap();
    if (row.value(QStringLiteral("anonymous")).toBool())
        return false;   // like the core: confirming needs a name
    row.insert(QStringLiteral("approved"), confirmed);
    if (row.value(QStringLiteral("doubtful")).toBool()) row.insert(QStringLiteral("counted"), confirmed);
    m_snapshot.participants[at] = row;
    touch(meetingId);
    return true;
}

bool OverviewDemoFacade::setSelfParticipant(const QString& meetingId, const QString& id, QString* whyNot)
{
    if (whyNot) whyNot->clear();
    const int at = rowIndex(id);
    if (at < 0)
        return false;
    // The demo's own person: the earlier own row (if any) gives way.
    QVariantList rows = m_snapshot.participants;
    QVariantMap row = rows.at(at).toMap();
    const QString name = demoText("Kovács Lilla (te)", "Emma Carter (you)");
    row.insert(QStringLiteral("name"), name);
    row.insert(QStringLiteral("monogram"), monogramOf(name));
    row.insert(QStringLiteral("sub"), QString());
    row.insert(QStringLiteral("anonymous"), false);
    row.insert(QStringLiteral("self"), true);
    row.insert(QStringLiteral("counted"), true);
    row.insert(QStringLiteral("doubtful"), false);
    row.insert(QStringLiteral("voiceless"), false);
    rows[at] = row;
    for (int i = int(rows.size()) - 1; i >= 0; --i)
        if (i != at && rows.at(i).toMap().value(QStringLiteral("self")).toBool())
            rows.removeAt(i);
    m_snapshot.participants = rows;
    touch(meetingId);
    return true;
}

bool OverviewDemoFacade::setSolo(const QString& meetingId)
{
    return decideInPlace(meetingId, QStringLiteral("solo"));
}

bool OverviewDemoFacade::decideInPlace(const QString& meetingId, const QString& decision)
{
    if (decision != QLatin1String("approved") && decision != QLatin1String("skipped") && decision != QLatin1String("solo"))
        return false;
    m_snapshot.decision = decision;
    m_snapshot.approvalPending = false;
    m_snapshot.approved = decision == QLatin1String("approved");
    QVariantList rows;
    for (const QVariant& v : std::as_const(m_snapshot.participants)) {
        QVariantMap row = v.toMap();
        const bool counted = row.value(QStringLiteral("counted")).toBool() && !row.value(QStringLiteral("anonymous")).toBool();
        // "Only I spoke": only the own row stays approved (the others are hidden on the card).
        const bool approved = decision == QLatin1String("approved") ? counted
                            : decision == QLatin1String("solo") ? row.value(QStringLiteral("self")).toBool()
                            : row.value(QStringLiteral("approved")).toBool();
        row.insert(QStringLiteral("approved"), approved);
        if (decision != QLatin1String("skipped")) row.insert(QStringLiteral("counted"), approved);
        rows << row;
    }
    m_snapshot.participants = rows;
    m_snapshot.peopleHint = decision == QLatin1String("approved") ? tr("jóváhagyva %1-kor").arg(demoText("okt. 1. 19:12", "Oct 1, 7:12 PM"))
                          : decision == QLatin1String("solo") ? tr("csak én beszéltem")
                                                              : tr("kihagyva");
    for (QVariant& v : m_snapshot.steps) {
        QVariantMap step = v.toMap();
        if (step.value(QStringLiteral("key")).toString() != QLatin1String("voice")) continue;
        step.insert(QStringLiteral("state"), QStringLiteral("done"));
        step.insert(QStringLiteral("sub"), decision == QLatin1String("solo") ? tr("csak én beszéltem")
                                         : decision == QLatin1String("skipped") ? tr("kihagyva")
                                         : tr("%n résztvevő", nullptr, int(std::count_if(rows.cbegin(), rows.cend(), [](const QVariant& r) {
                                               return r.toMap().value(QStringLiteral("approved")).toBool(); }))));
        v = step;
    }
    touch(meetingId);
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
    // No voice model: recorded, not transcribed, the voice step blocked; one manual participant.
    const bool noModel = st == QLatin1String("overviewNoVoiceModel");

    // The first transcription runs (two tracks, nobody yet): the phases and "Cancel" in the step.
    const bool transcribingOnly = st == QLatin1String("overviewTranscribing");
    // The card's candidate states (the transcription runs meanwhile).
    const bool candidates = candidateScenario(st);
    const bool twoTrack = empty || nobody || recorded || transcribingOnly || noModel;

    s.hasTranscript = done || nobody;
    s.title = demoText("Negyedéves partnertalálkozó", "Quarterly partner review");
    s.transcribing = !s.hasTranscript && !recorded && !noModel;
    s.canReanalyze = done || nobody;
    s.allDone = done;
    s.analysisRunning = empty;
    s.approvalPending = candidates;
    s.approved = done;
    s.decision = done ? QStringLiteral("approved") : candidates ? QStringLiteral("pending") : QString();
    s.candidateCount = 5;
    s.selfName = demoName("Kovács Lilla");
    s.contextNote = twoTrack ? QString()
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
             << withSpeaker(withCard(personRow(QStringLiteral("S5"), demoRaw(QStringLiteral("Távoli 1")), 4, tr("névtelen · elnevezhető"),
                                               {callSide}, 0.07), false),
                            QStringLiteral("S5"), true);
        const QList<int> lines = {286, 341, 247, 196, 91};
        for (int i = 0; i < rows.size(); ++i) {
            QVariantMap row = rows.at(i).toMap();
            row.insert(QStringLiteral("lineCount"), lines.value(i));
            row.insert(QStringLiteral("approved"), !row.value(QStringLiteral("anonymous")).toBool());
            rows[i] = row;
        }
        // Invited but did not speak (added manually): dim, after the speakers.
        rows << unheardRow(QStringLiteral("p5"), demoName("Lantos Réka"), 5);
        s.peopleState = QStringLiteral("list");
        s.peopleHint = tr("jóváhagyva %1-kor").arg(demoText("okt. 1. 16:40", "Oct 1, 4:40 PM"));
        // An anonymous speaker whose lines all went to others: hidden.
        s.hiddenSpeakers = 1;
    } else if (empty || recorded || transcribingOnly) {
        s.peopleState = QStringLiteral("empty");
        s.peopleHint.clear();
    } else if (noModel) {
        rows << withCard(personRow(QStringLiteral("m1"), demoName("Fehér Gábor"), 1, {},
                                   {source(QStringLiteral("user"), tr("kézi"), false, false, tr("Kézzel adtad hozzá"))},
                                   -1.0, false, false, false, true),
                         true, false, true);
        s.peopleState = QStringLiteral("list");
        s.peopleHint = tr("kézi");
    } else if (nobody) {
        // Everyone is anonymous: the speakers are the rows (named on the card).
        const QList<QPair<QString, double>> anon = {{tr("Beszélő 1"), 0.46}, {tr("Beszélő 2"), 0.38}, {tr("Beszélő 3"), 0.16}};
        const QList<int> lines = {188, 151, 73};
        int i = 0;
        for (const auto& [name, share] : anon) {
            QVariantMap row = withCard(personRow(QStringLiteral("S%1").arg(i + 1), name, i, tr("névtelen · elnevezhető"),
                                                 {}, share, false, false, false, false),
                                       false);
            row.insert(QStringLiteral("monogram"), QStringLiteral("B%1").arg(i + 1));
            row.insert(QStringLiteral("lineCount"), lines.value(i));
            rows << withSpeaker(row, QStringLiteral("S%1").arg(i + 1), true);
            ++i;
        }
        s.peopleState = QStringLiteral("nobody");
        s.peopleHint.clear();
    } else {
        // The decision pending (A variant, design 1): two known voices, two unknown, a doubtful one, a
        // tag suggestion.
        const QString voiceDetail = tr("A hangja %1%-ban hasonlít a mentett mintáihoz");
        rows << personRow(QStringLiteral("p1"), demoText("Kovács Lilla (te)", "Emma Carter (you)"), 0, {},
                          {source(QStringLiteral("fingerprint"), tr("hang %1%").arg(88), false, false, voiceDetail.arg(88))},
                          0.41, true)
             << personRow(QStringLiteral("p2"), demoName("Fehér Gábor"), 1, {},
                          {source(QStringLiteral("fingerprint"), tr("hang %1%").arg(74), false, false, voiceDetail.arg(74)),
                           source(QStringLiteral("tag"), QStringLiteral("#Nordvik"), false, false,
                                  tr("Közös címke: %1").arg(QStringLiteral("Nordvik")))},
                          0.27, false, false, false, true)
             << voiceRow(QStringLiteral("v1"), 1, 2,
                         {source(QStringLiteral("fingerprint"), tr("ismeretlen hang"), false, false,
                                 tr("A hangja egyik mentett mintához sem hasonlít eléggé"))}, 0.19)
             << withCard(personRow(QStringLiteral("p4"), demoName("Varga Árpád"), 3, {},
                                   {source(QStringLiteral("fingerprint"), tr("hang %1%").arg(52), false, false, voiceDetail.arg(52)),
                                    source(QStringLiteral("tag"), tr("#%1-on még nem volt").arg(QStringLiteral("Nordvik")), true, false,
                                           tr("A #%1 megbeszéléseken még nem vett részt").arg(QStringLiteral("Nordvik")))},
                                   0.11, false, false, false, true),
                         false, true)
             << voiceRow(QStringLiteral("v2"), 2, 4,
                         {source(QStringLiteral("fingerprint"), tr("ismeretlen hang"), false, false,
                                 tr("A hangja egyik mentett mintához sem hasonlít eléggé")),
                          source(QStringLiteral("x"), tr("%n ablak · rövid", nullptr, 4), true, false,
                                 tr("Kevés hangablakból áll: lehet zaj vagy egy másik hang része"))}, 0.02)
             << personRow(QStringLiteral("suggest:") + demoName("Molnár Eszter"), demoName("Molnár Eszter"), -1, tr("javasolt · nem hallottuk"),
                          {source(QStringLiteral("tag"), tr("a #%1 alapján · %2 / %3").arg(QStringLiteral("Nordvik")).arg(9).arg(12), false, true)},
                          -1.0, false, true, false, true);
        for (QVariant& v : rows) {
            QVariantMap row = v.toMap();
            row.insert(QStringLiteral("removable"), true);
            v = row;
        }
        if (st == QLatin1String("overviewVoiceless"))
            rows.insert(rows.size() - 1, withCard(personRow(QStringLiteral("t1"), demoName("Tóth Bence"), 5, {},
                                                            {source(QStringLiteral("user"), tr("kézi"), false, false, tr("Kézzel adtad hozzá"))},
                                                            -1.0, false, false, false, true),
                                                  true, false, true));
        s.peopleState = QStringLiteral("list");
        s.peopleHint = tr("hang · sáv · címke · kézi");
    }
    // Fictional samples: every heard row (not the dim / suggested ones) can be listened to.
    {
        const QStringList heard = done ? QStringList{QStringLiteral("p1"), QStringLiteral("p2"), QStringLiteral("p3"),
                                                     QStringLiteral("p4"), QStringLiteral("S5")}
                                : nobody ? QStringList{QStringLiteral("S1"), QStringLiteral("S2"), QStringLiteral("S3")}
                                         : QStringList{QStringLiteral("p1"), QStringLiteral("p2"), QStringLiteral("v1"),
                                                       QStringLiteral("p4"), QStringLiteral("v2")};
        const QStringList onMic = {QStringLiteral("p1"), QStringLiteral("v2"), QStringLiteral("S1")};
        for (QVariant& v : rows) {
            const QVariantMap row = v.toMap();
            const QString id = row.value(QStringLiteral("id")).toString();
            const int n = int(heard.indexOf(id));
            if (n >= 0 && !row.value(QStringLiteral("dimmed")).toBool())
                v = withSample(row, onMic.contains(id) ? QStringLiteral("mic") : QStringLiteral("call"), 42000 + 31000 * n);
        }
    }
    s.participants = rows;
    s.anonymousSpeakers.clear();
    if (nobody) {
        for (const QVariant& v : std::as_const(rows)) {
            const QVariantMap row = v.toMap();
            s.anonymousSpeakers << QVariantMap{{QStringLiteral("name"), row.value(QStringLiteral("name"))},
                                               {QStringLiteral("share"), row.value(QStringLiteral("talkShare"))},
                                               {QStringLiteral("colorIndex"), row.value(QStringLiteral("colorIndex"))}};
        }
    }
    s.sideWho = twoTrack ? QVariantMap()
              : QVariantMap{{QStringLiteral("mic"), demoName("Kovács Lilla")},
                            {QStringLiteral("loopback"), tr("%n résztvevő", nullptr, 4)}};

    QVariantList steps;
    if (twoTrack) {
        steps << step(QStringLiteral("recording"), tr("Felvétel"), QStringLiteral("0:42:10 · ") + tr("%n sáv", nullptr, 2), QStringLiteral("done"))
              << step(QStringLiteral("tracks"), tr("Sávok"), tr("%n aktív", nullptr, 2), QStringLiteral("done"))
              << step(QStringLiteral("mixdown"), tr("Lekeverés"), tr("%n sávból", nullptr, 2), QStringLiteral("done"));
        if (transcribingOnly)
            steps << step(QStringLiteral("voice"), tr("Hanglenyomat-elemzés"), tr("nem futott"), QStringLiteral("waiting"), true)
                  << step(QStringLiteral("transcript"), tr("Átirat"), QStringLiteral("Soniox · ") + tr("feltöltés"), QStringLiteral("running"), true, -2)
                  << step(QStringLiteral("summary"), tr("Összefoglaló"), tr("az átirat után"), QStringLiteral("waiting"));
        else if (noModel) {
            QVariantMap voice = step(QStringLiteral("voice"), tr("Hanglenyomat-elemzés"),
                                     tr("Nincs hangmodell. Töltsd le a Beállításokban (Beszélők)."), QStringLiteral("attention"), true);
            voice.insert(QStringLiteral("fixTarget"), QStringLiteral("speakers"));
            steps << voice
                  << step(QStringLiteral("transcript"), tr("Átirat"), tr("még nem indult"), QStringLiteral("waiting"), true)
                  << step(QStringLiteral("summary"), tr("Összefoglaló"), tr("az átirat után"), QStringLiteral("waiting"));
        } else if (recorded)
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
            steps << step(QStringLiteral("voice"), tr("Hanglenyomat-elemzés"), tr("%n jelölt · jóváhagyásra vár", nullptr, 5), QStringLiteral("attention"), true)
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

// The card's decided / edited design states, applied over the pending candidates with the same
// in-memory operations as a click.
void OverviewDemoFacade::applyScenarioEdits()
{
    const QString st = m_scenario;
    if (st == QLatin1String("overviewSameVoice") || st == QLatin1String("overviewApproved"))
        nameParticipant(QString(), QStringLiteral("v1"), demoName("Molnár Eszter"));
    if (st == QLatin1String("overviewApproved")) {
        confirmParticipant(QString(), QStringLiteral("p4"), true);
        mergeParticipantInto(QString(), QStringLiteral("v2"), QStringLiteral("p1"));
        decideInPlace(QString(), QStringLiteral("approved"));
    } else if (st == QLatin1String("overviewSkipped")) {
        decideInPlace(QString(), QStringLiteral("skipped"));
    } else if (st == QLatin1String("overviewSolo")) {
        decideInPlace(QString(), QStringLiteral("solo"));
    }
}

} // namespace tanara_demo
