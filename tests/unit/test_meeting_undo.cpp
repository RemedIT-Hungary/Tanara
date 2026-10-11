//
// G4 — a megbeszélés közös visszavonási verme (edit/MeetingUndo.h) az AppController-ben: a
// szerkesztő, a meetinghez tartozó címke-lépések és a résztvevő-műveletek időrendben; a más
// megbeszélés címke-lépése nem kerül ide (és amíg felül van, átugorja a címke-bejegyzést); a
// „Csak én beszéltem” elnyeli a szerkesztő kötés-lépését; a résztvevő tartós törlése (A6) és a
// javasolt személy tartós elutasítása egy megbeszélésre (tags.json). Kitalált nevek, IZOLÁLT
// TANARA_HOME; Soniox / LLM / hálózat nélkül.
//
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "tanara/AppController.h"
#include "tanara/SettingsManager.h"
#include "tanara/edit/MeetingUndo.h"
#include "tanara/edit/SpeakerEditor.h"
#include "tanara/store/MeetingStore.h"
#include "tanara/tags/TagService.h"

#include <memory>

using namespace tanara;
using Source = MeetingUndoService::Source;

namespace {

const QString kSelf = QStringLiteral("Kovács Lilla");

QJsonObject readJson(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

bool hasParticipant(const Meeting& m, const QString& name)
{
    return std::any_of(m.participants.cbegin(), m.participants.cend(),
                       [&](const Participant& p) { return p.personName == name; });
}

} // namespace

class MeetingUndoTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void cleanup();

    void orderAcrossSources();
    void editorUndoRedoAndTagGroups();
    void soloAbsorbsEditorStep();
    void removeParticipantDurableWithUndo();
    void rejectSuggestedPersonForMeeting();
    void trackExcludeIncludeUndo();

private:
    Meeting makeMeeting(const QString& title, bool transcript = true);
    QString tagged(const QString& title, const QString& tagId, const QStringList& people);

    std::unique_ptr<QTemporaryDir> m_home;
    std::unique_ptr<AppController> m_app;
};

void MeetingUndoTest::initTestCase()
{
    qputenv("TANARA_CLOUD", "off");
}

void MeetingUndoTest::init()
{
    m_home = std::make_unique<QTemporaryDir>();
    QVERIFY(m_home->isValid());
    qputenv("TANARA_HOME", m_home->path().toUtf8());
    m_app = std::make_unique<AppController>();
    QVERIFY(m_app->store()->metadataDir().startsWith(m_home->path()));
    m_app->setUserSpeakerName(kSelf);
}

void MeetingUndoTest::cleanup()
{
    m_app.reset();
    m_home.reset();
}

// Két nyers beszélős átirat (Beszélő 1: 0–3 és 5–8 mp, Beszélő 2: 3–5 mp), hang nélkül.
Meeting MeetingUndoTest::makeMeeting(const QString& title, bool transcript)
{
    Meeting m = m_app->store()->createMeeting(title);
    m.durationMs = 8000;
    if (transcript) {
        QJsonArray toks, segs;
        const struct { qint64 s, e; const char* spk; } rows[] = {
            {0, 3000, "Beszélő 1"}, {3000, 5000, "Beszélő 2"}, {5000, 8000, "Beszélő 1"}};
        for (const auto& r : rows) {
            toks.append(QJsonObject{{"text", "x"}, {"speaker", QString::fromUtf8(r.spk)}, {"startMs", double(r.s)},
                                    {"endMs", double(r.e)}, {"confidence", 1.0}, {"trackId", "mixdown"}});
            segs.append(QJsonObject{{"startMs", double(r.s)}, {"endMs", double(r.e)},
                                    {"speaker", QString::fromUtf8(r.spk)}, {"text", "x"}});
        }
        QFile tf(QDir(m.folder).filePath("transcript.tokens.json"));
        if (tf.open(QIODevice::WriteOnly))
            tf.write(QJsonDocument(QJsonObject{{"language", "hu"}, {"tokens", toks}}).toJson());
        QFile sf(QDir(m.folder).filePath("transcript.segments.json"));
        if (sf.open(QIODevice::WriteOnly)) sf.write(QJsonDocument(segs).toJson());
        m.hasTranscript = true;
    }
    m_app->store()->saveMeeting(m);
    return m;
}

QString MeetingUndoTest::tagged(const QString& title, const QString& tagId, const QStringList& people)
{
    Meeting m = m_app->store()->createMeeting(title);
    m.startedAt = QDateTime(QDate(2026, 9, 1), QTime(10, 0));
    for (int i = 0; i < people.size(); ++i)
        m.speakerMap.insert(QStringLiteral("Beszélő %1").arg(i + 1), people.at(i));
    m.tagIds = {tagId};
    m_app->store()->saveMeeting(m);
    return m.id;
}

void MeetingUndoTest::orderAcrossSources()
{
    const Meeting m = makeMeeting("Közös verem");
    const Meeting other = makeMeeting("Másik megbeszélés", false);
    MeetingUndoService* u = m_app->meetingUndo(m.id);
    QVERIFY(u);
    QCOMPARE(m_app->meetingUndo(m.id), u);
    QVERIFY(!m_app->meetingUndo(QStringLiteral("nincs-ilyen")));
    SpeakerEditor* ed = m_app->speakerEditor(m.id);
    QVERIFY(ed);
    QCOMPARE(u->speakerEditor(), ed);
    TagService* tags = m_app->tags();
    QVERIFY(!u->canUndo());
    QSignalSpy changed(u, &MeetingUndoService::changed);

    // 1. címke a meetingen, 2. beszélő-javítás, 3. kézi résztvevő, 4. leírás.
    const Tag t = tags->addTag(m.id, QStringLiteral("Projekt"));
    QVERIFY(!t.id.isEmpty());
    QCOMPARE(u->count(), 1);
    QCOMPARE(u->undoSource(), Source::Tags);
    const QString key = ed->speakers().first().key;
    QVERIFY(ed->reassignSpeaker(key, QStringLiteral("Fehér Gábor")));
    QCOMPARE(u->undoSource(), Source::Speakers);
    QVERIFY(!m_app->addParticipant(m.id, QStringLiteral("Molnár Eszter")).isEmpty());
    QCOMPARE(u->undoSource(), Source::Participants);
    m_app->setMeetingContextNote(m.id, QStringLiteral("Heti egyeztetés"));
    m_app->setMeetingContextNote(m.id, QStringLiteral("Heti egyeztetés"));   // változatlan: nincs lépés
    QCOMPARE(u->count(), 4);
    QCOMPARE(u->undoText(), QStringLiteral("Leírás módosítva"));
    QVERIFY(changed.count() >= 4);

    // Más megbeszélés címke-lépése nem kerül ide.
    tags->addTag(other.id, QStringLiteral("Más"));
    QCOMPARE(u->count(), 4);

    QSignalSpy undone(u, &MeetingUndoService::undone);
    QVERIFY(u->undo());
    QVERIFY(m_app->store()->load(m.id).contextNote.isEmpty());
    QCOMPARE(undone.last().at(0).toString(), QStringLiteral("Leírás módosítva"));
    QVERIFY(u->undo());
    QVERIFY(!hasParticipant(m_app->store()->load(m.id), QStringLiteral("Molnár Eszter")));
    QCOMPARE(u->undoSource(), Source::Speakers);
    QVERIFY(u->undo());
    QVERIFY(!m_app->store()->load(m.id).speakerMap.values().contains(QStringLiteral("Fehér Gábor")));
    QVERIFY(!ed->canUndo());

    // A címke-bejegyzés fölött a TagService-ben a másik megbeszélés lépése áll: most nem vonható vissza.
    QCOMPARE(u->count(), 1);
    QVERIFY(!u->canUndo());
    QVERIFY(!u->undo());
    tags->undo();   // a másik megbeszélésé (pl. a Címkék ablakból)
    QVERIFY(u->canUndo());
    QCOMPARE(u->undoSource(), Source::Tags);
    QVERIFY(u->undo());
    QVERIFY(tags->tagsOf(m.id).isEmpty());
    QVERIFY(!u->canUndo());
    QCOMPARE(u->count(), 0);
}

void MeetingUndoTest::editorUndoRedoAndTagGroups()
{
    const Meeting m = makeMeeting("Szerkesztő");
    SpeakerEditor* ed = m_app->speakerEditor(m.id);
    MeetingUndoService* u = m_app->meetingUndo(m.id);
    QVERIFY(ed && u);
    TagService* tags = m_app->tags();

    QVERIFY(ed->reassignSpeaker(ed->speakers().first().key, QStringLiteral("Varga Árpád")));
    QCOMPARE(u->count(), 1);
    ed->undo();   // közvetlenül a szerkesztőben (pl. a szerkesztő saját Ctrl+Z-je)
    QCOMPARE(u->count(), 0);
    ed->redo();   // a visszahozott lépés a legfrissebb
    QCOMPARE(u->count(), 1);
    QCOMPARE(u->undoSource(), Source::Speakers);

    // Személy-címke az Áttekintésből: a meetinghez jelölt csoport ide tartozik.
    tags->beginGroup(QStringLiteral("Címke a résztvevőn"), m.id);
    tags->addPersonTag(QStringLiteral("Varga Árpád"), QStringLiteral("Csapat"));
    tags->endGroup();
    QCOMPARE(u->count(), 2);
    QCOMPARE(u->undoSource(), Source::Tags);
    QCOMPARE(u->undoText(), QStringLiteral("Címke a résztvevőn"));

    // Jelölés nélküli személy-címke (pl. a Személyek ablakból): nem ide tartozik, és amíg felül
    // van, a címke-bejegyzés átugródik — a szerkesztő-lépés vonható vissza.
    tags->addPersonTag(QStringLiteral("Varga Árpád"), QStringLiteral("Másik"));
    QCOMPARE(u->count(), 2);
    QCOMPARE(u->undoSource(), Source::Speakers);
    tags->undo();
    QCOMPARE(u->undoSource(), Source::Tags);
    QVERIFY(u->undo());
    QVERIFY(tags->tagsOfPerson(QStringLiteral("Varga Árpád")).isEmpty());
    QVERIFY(u->undo());
    QVERIFY(m_app->store()->load(m.id).speakerMap.isEmpty());

    // A szerkesztő bezárása: a szerkesztő-bejegyzései kiesnek.
    QVERIFY(ed->reassignSpeaker(ed->speakers().first().key, QStringLiteral("Varga Árpád")));
    QCOMPARE(u->count(), 1);
    QSignalSpy changed(u, &MeetingUndoService::changed);
    m_app->closeSpeakerEditor(m.id);
    QTRY_COMPARE(u->count(), 0);
}

void MeetingUndoTest::soloAbsorbsEditorStep()
{
    const Meeting m = makeMeeting("Csak én");
    MeetingUndoService* u = m_app->meetingUndo(m.id);
    SpeakerEditor* ed = m_app->speakerEditor(m.id);
    QVERIFY(u && ed);

    QVERIFY(m_app->setSoloMeeting(m.id));
    Meeting mm = m_app->store()->load(m.id);
    QVERIFY(mm.approval && mm.approval->solo);
    QVERIFY(hasParticipant(mm, kSelf));
    QVERIFY(ed->canUndo());   // a kötések a szerkesztőn át mentek…
    QCOMPARE(u->count(), 1);  // …de a közös veremben egy lépés
    QCOMPARE(u->undoText(), QStringLiteral("Csak én beszéltem"));
    for (const EditorSpeaker& s : ed->speakers())
        if (s.utteranceCount > 0) QCOMPARE(s.personName, kSelf);

    QVERIFY(u->undo());
    mm = m_app->store()->load(m.id);
    QVERIFY(!mm.approval.has_value());
    QVERIFY(mm.participants.isEmpty());
    QVERIFY(mm.speakerMap.isEmpty());
    QVERIFY(!ed->canUndo());
    QCOMPARE(u->count(), 0);
}

void MeetingUndoTest::removeParticipantDurableWithUndo()
{
    const Meeting m = makeMeeting("Törlés");
    MeetingUndoService* u = m_app->meetingUndo(m.id);
    QVERIFY(u);
    const QString cili = m_app->addParticipant(m.id, QStringLiteral("Molnár Eszter"));
    const QString dora = m_app->addParticipant(m.id, QStringLiteral("Szabó Bence"));
    QVERIFY(m_app->approveParticipants(m.id, m_app->participants(m.id)));
    // Bence egy beszélő nevén is (az átirat-szerkesztőből).
    m_app->renameSpeaker(m.id, QStringLiteral("Beszélő 2"), QStringLiteral("Szabó Bence"), /*enroll*/ false);
    QCOMPARE(m_app->store()->load(m.id).speakerMap.value("Beszélő 2"), QStringLiteral("Szabó Bence"));
    const int before = u->count();

    QVERIFY(m_app->removeParticipant(m.id, dora));
    QCOMPARE(u->count(), before + 1);   // a szerkesztő névtelenítő lépése elnyelve
    QCOMPARE(u->undoSource(), Source::Participants);
    QCOMPARE(u->undoText(), QStringLiteral("Résztvevő eltávolítva: Szabó Bence"));
    Meeting mm = m_app->store()->load(m.id);
    QVERIFY(!hasParticipant(mm, QStringLiteral("Szabó Bence")));
    QVERIFY(hasParticipant(mm, QStringLiteral("Molnár Eszter")));
    QVERIFY(mm.approval);
    for (const ParticipantApprovalEntry& e : mm.approval->candidates) QVERIFY(e.participantId != dora);
    QVERIFY(!mm.speakerMap.values().contains(QStringLiteral("Szabó Bence")));
    QVERIFY(!m_app->removeParticipant(m.id, dora));
    QVERIFY(!m_app->removeParticipant(QStringLiteral("nincs"), cili));

    // Visszavonás: a résztvevő, a jóváhagyás-bejegyzése és a beszélő neve is visszajön.
    QVERIFY(u->undo());
    mm = m_app->store()->load(m.id);
    QVERIFY(hasParticipant(mm, QStringLiteral("Szabó Bence")));
    bool entry = false;
    for (const ParticipantApprovalEntry& e : mm.approval->candidates) entry = entry || e.participantId == dora;
    QVERIFY(entry);
    QCOMPARE(mm.speakerMap.value("Beszélő 2"), QStringLiteral("Szabó Bence"));

    // Tartós: újraindítás után sincs ott.
    QVERIFY(m_app->removeParticipant(m.id, dora));
    m_app.reset();
    m_app = std::make_unique<AppController>();
    mm = m_app->store()->load(m.id);
    QVERIFY(!hasParticipant(mm, QStringLiteral("Szabó Bence")));
    QVERIFY(hasParticipant(mm, QStringLiteral("Molnár Eszter")));
    QCOMPARE(m_app->participants(m.id).size(), 1);
}

void MeetingUndoTest::rejectSuggestedPersonForMeeting()
{
    TagService* tags = m_app->tags();
    const Tag nord = tags->create(QStringLiteral("Nordvik"));
    const QStringList crew{QStringLiteral("Fehér Gábor"), QStringLiteral("Varga Árpád"),
                           QStringLiteral("Molnár Eszter"), QStringLiteral("Szabó Bence"), kSelf};
    for (int i = 1; i <= 3; ++i) tagged(QStringLiteral("Nordvik %1").arg(i), nord.id, crew);
    const QString target = tagged(QStringLiteral("Új Nordvik"), nord.id, {});
    const QString second = tagged(QStringLiteral("Másik Nordvik"), nord.id, {});
    tags->refreshPersonStatsNow();

    // Legfeljebb 3 javaslat; a saját személy és a már résztvevők kimaradnak.
    QVector<PersonTagStat> sugg = tags->suggestPeopleForMeeting(target);
    QCOMPARE(sugg.size(), 3);
    for (const PersonTagStat& s : sugg) QVERIFY(s.name != kSelf);
    QCOMPARE(tags->suggestPeopleForMeeting(target, {}, 0).size(), 4);
    QCOMPARE(tags->suggestPeopleForMeeting(target, {QStringLiteral("fehér gábor")}, 0).size(), 3);

    // × a javasolt személyen: csak erre a megbeszélésre, tartósan, a közös veremben.
    MeetingUndoService* u = m_app->meetingUndo(target);
    QVERIFY(u);
    const QString gabor = QStringLiteral("Fehér Gábor");
    tags->rejectPersonForMeeting(gabor, target);
    QVERIFY(tags->isPersonRejectedForMeeting(gabor.toUpper(), target));
    QVERIFY(!tags->isPersonRejectedForMeeting(gabor, second));
    QCOMPARE(tags->suggestPeopleForMeeting(target, {}, 0).size(), 3);
    for (const PersonTagStat& s : tags->suggestPeopleForMeeting(target, {}, 0)) QVERIFY(s.name != gabor);
    QCOMPARE(tags->suggestPeopleForMeeting(second, {}, 0).size(), 4);
    QCOMPARE(u->count(), 1);
    QCOMPARE(u->undoSource(), Source::Tags);

    // A tags.json-ban {"person", "meetingId"} címke nélkül; egy új példány is látja.
    const QString path = QDir(m_app->store()->metadataDir()).filePath("tags.json");
    bool stored = false;
    for (const QJsonValue& v : readJson(path).value("rejected").toArray()) {
        const QJsonObject o = v.toObject();
        if (o.value("person").toString() == gabor && o.value("meetingId").toString() == target
            && !o.contains("tagId"))
            stored = true;
    }
    QVERIFY(stored);
    {
        TagService fresh(m_app->store(), path);
        QVERIFY(fresh.isPersonRejectedForMeeting(gabor, target));
    }

    // Visszavonás a közös veremből.
    QVERIFY(u->undo());
    QVERIFY(!tags->isPersonRejectedForMeeting(gabor, target));
    QCOMPARE(tags->suggestPeopleForMeeting(target, {}, 0).size(), 4);

    // A megbeszélés törlésével az elutasítás is megy.
    tags->rejectPersonForMeeting(gabor, target);
    const int n = tags->rejectedCount();
    m_app->deleteMeeting(target);
    QCOMPARE(tags->rejectedCount(), n - 1);
}

// „Kiveszem” / „Beemelem” a közös veremben (a hangfájlok hiányoznak: a lekeverés nem készül el,
// de a sáv-állapot visszaállítása ettől független).
void MeetingUndoTest::trackExcludeIncludeUndo()
{
    Meeting m = makeMeeting("Sávok", false);
    for (const char* id : {"a", "b"}) {
        Track t;
        t.id = QString::fromLatin1(id);
        t.deviceName = t.id;
        t.file = QStringLiteral("track_%1.wav").arg(t.id);
        t.active = true;
        m.tracks.append(t);
    }
    m_app->store()->saveMeeting(m);
    MeetingUndoService* u = m_app->meetingUndo(m.id);
    QVERIFY(u);
    QSignalSpy tracks(m_app.get(), &AppController::tracksChanged);

    m_app->excludeTrack(m.id, "b");
    QVERIFY(!m_app->store()->load(m.id).tracks.at(1).included());
    QCOMPARE(u->undoText(), QStringLiteral("Sáv kivéve"));
    m_app->excludeTrack(m.id, "b");   // már kivéve: nincs új lépés
    QCOMPARE(u->count(), 1);

    tracks.clear();
    QVERIFY(u->undo());
    const Meeting back = m_app->store()->load(m.id);
    QVERIFY(back.tracks.at(1).included());
    QVERIFY(back.tracks.at(1).excludedReason.isEmpty());
    QVERIFY(back.mixdownDirty);
    QVERIFY(tracks.count() >= 1);
    QCOMPARE(u->count(), 0);
}

QTEST_GUILESS_MAIN(MeetingUndoTest)
#include "test_meeting_undo.moc"
