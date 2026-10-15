// The Overview tab's view model (OverviewViewModel) with a real AppController in an ISOLATED
// TANARA_HOME (QTemporaryDir; no audio, no model, no network): adding and removing participants
// by hand, "only I spoke", the manual description, the processing steps before and after the
// transcript, the DATA tiles, the "nobody" state (only anonymous speakers), the tracks' speech
// share and including them in the mixdown (TrackListModel). Demo data: the four screen states.
#include "AppContext.h"
#include "UiServices.h"
#include "FacadeTestSupport.h"
#include "OverviewViewModel.h"
#include "ParticipantsViewModel.h"
#include "ShellMeetingModel.h"
#include "TrackListModel.h"

#include "tanara/app/AppController.h"
#include "tanara/app/SettingsManager.h"
#include "tanara/editing/undo/MeetingUndo.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/store/PeopleStore.h"
#include "tanara/library/tags/TagService.h"
#include "tanara/processing/analysis/ParticipantAnalysis.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace tanara_qml;

class TestOverviewViewModel : public QObject {
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_home;
    std::unique_ptr<tanara::app::AppController> m_app;

    tanara::Meeting recording(const QString& title)
    {
        tanara::Meeting m = m_app->store()->createMeeting(title);
        tanara::Track mic;
        mic.id = QStringLiteral("mic");
        mic.kind = tanara::TrackKind::Mic;
        mic.file = QStringLiteral("track_mic.wav");
        mic.speechRatio = 0.38;
        tanara::Track sys;
        sys.id = QStringLiteral("system");
        sys.kind = tanara::TrackKind::Loopback;
        sys.file = QStringLiteral("track_system.wav");
        sys.active = false;
        sys.speechRatio = 0.0;
        sys.excludedReason = QStringLiteral("noSpeech");
        m.tracks = {mic, sys};
        m.durationMs = 4564000;   // 1:16:04
        for (const tanara::Track& t : m.tracks) {
            QFile f(QDir(m.folder).filePath(t.file));
            if (f.open(QIODevice::WriteOnly)) f.write("nem valódi hang");
        }
        m_app->store()->saveMeeting(m);
        return m;
    }
    tanara::Meeting transcribed(const QString& title)
    {
        tanara::Meeting m = recording(title);
        QFile seg(QDir(m.folder).filePath(QStringLiteral("transcript.segments.json")));
        if (seg.open(QIODevice::WriteOnly))
            seg.write(QJsonDocument(QJsonArray{
                QJsonObject{{"startMs", 0}, {"endMs", 900}, {"speaker", "Beszélő 1"}, {"text", "Sziasztok."}},
                QJsonObject{{"startMs", 1000}, {"endMs", 1900}, {"speaker", "Beszélő 2"}, {"text", "Szia."}},
                QJsonObject{{"startMs", 2000}, {"endMs", 2900}, {"speaker", "Beszélő 1"}, {"text", "Kezdjük."}}}).toJson());
        m.hasTranscript = true;
        m_app->store()->saveMeeting(m);
        return m;
    }
    static QVariantMap stepOf(const OverviewViewModel& vm, const QString& key)
    {
        for (const QVariant& v : vm.steps())
            if (v.toMap().value(QStringLiteral("key")).toString() == key) return v.toMap();
        return {};
    }
    static QStringList names(const OverviewViewModel& vm)
    {
        QStringList out;
        for (const QVariant& v : vm.participants()) out << v.toMap().value(QStringLiteral("name")).toString();
        return out;
    }

private slots:
    void initTestCase() { qputenv("TANARA_CLOUD", "off"); }

    void init()
    {
        m_home = std::make_unique<QTemporaryDir>();
        QVERIFY(m_home->isValid());
        qputenv("TANARA_HOME", m_home->path().toUtf8());
        AppContext::instance()->setDemo(false);
        m_app = std::make_unique<tanara::app::AppController>();
        QVERIFY(m_app->store()->audioDir().startsWith(m_home->path()));
    }
    void cleanup()
    {
        m_app.reset();
        m_home.reset();
        qunsetenv("TANARA_HOME");
    }

    void monogram()
    {
        QCOMPARE(OverviewViewModel::monogramOf(QStringLiteral("Varga Árpád")), QStringLiteral("VÁ"));
        QCOMPARE(OverviewViewModel::monogramOf(QStringLiteral("Távoli 1")), QStringLiteral("T1"));
        QCOMPARE(OverviewViewModel::monogramOf(QStringLiteral("Kovács Lilla (te)")), QStringLiteral("KL"));
        QCOMPARE(OverviewViewModel::monogramOf(QString()), QStringLiteral("?"));
    }

    // Before the transcript: steps (recording done, transcript not started), an empty participant
    // list, a manual participant gives the list + source "manual", x removes it; the manual description is saved.
    void beforeTranscript_participantsAndSteps()
    {
        const tanara::Meeting m = recording(QStringLiteral("Előtte"));
        OverviewViewModel vm(facadetest::overviewOf(m_app.get()));
        vm.setMeetingId(m.id);
        QVERIFY(!vm.hasTranscript());
        QVERIFY(!vm.transcribing());
        QCOMPARE(vm.peopleState(), QStringLiteral("empty"));
        QCOMPARE(stepOf(vm, QStringLiteral("recording")).value("state").toString(), QStringLiteral("done"));
        QCOMPARE(stepOf(vm, QStringLiteral("recording")).value("sub").toString(), QStringLiteral("1:16:04 · 2 sáv"));
        QCOMPARE(stepOf(vm, QStringLiteral("tracks")).value("sub").toString(),
                 QStringLiteral("1 aktív · 1 kimaradt, nincs rajta beszéd"));
        QCOMPARE(stepOf(vm, QStringLiteral("transcript")).value("state").toString(), QStringLiteral("waiting"));
        QVERIFY(stepOf(vm, QStringLiteral("transcript")).value("parallel").toBool());
        // Without a voice model the voice step is blocked like the transcript step: warning + the Settings page to fix it.
        QCOMPARE(stepOf(vm, QStringLiteral("voice")).value("state").toString(), QStringLiteral("attention"));
        QCOMPARE(stepOf(vm, QStringLiteral("voice")).value("fixTarget").toString(), QStringLiteral("speakers"));
        QCOMPARE(stepOf(vm, QStringLiteral("voice")).value("sub").toString(),
                 QStringLiteral("Nincs hangmodell. Töltsd le a Beállításokban (Beszélők)."));
        QCOMPARE(stepOf(vm, QStringLiteral("summary")).value("sub").toString(), QStringLiteral("az átirat után"));
        QVERIFY(!vm.allDone());
        QVERIFY(vm.stats().isEmpty());

        const QString id = vm.addParticipant(QStringLiteral("Fehér Gábor"));
        QVERIFY(!id.isEmpty());
        QTRY_COMPARE(vm.peopleState(), QStringLiteral("list"));
        QCOMPARE(names(vm), QStringList{QStringLiteral("Fehér Gábor")});
        const QVariantMap row = vm.participants().first().toMap();
        QCOMPARE(row.value("monogram").toString(), QStringLiteral("FG"));
        QCOMPARE(row.value("talkShare").toDouble(), -1.0);
        const QVariantList sources = row.value("sources").toList();
        QVERIFY(!sources.isEmpty());
        QCOMPARE(sources.first().toMap().value("text").toString(), QStringLiteral("kézi"));
        // The badge's tooltip (evidence explanation); before the transcript there is no speaker key.
        QVERIFY(!sources.first().toMap().value("detail").toString().isEmpty());
        QVERIFY(row.value("speakerKey").toString().isEmpty());
        QVERIFY(!row.value("anonymous").toBool());
        QCOMPARE(vm.title(), QStringLiteral("Előtte"));
        // Without a voice model there is no "Re-analyze", and core does not start one either.
        QVERIFY(!vm.canReanalyze());
        QVERIFY(!vm.reanalyze());

        // The x is persistent (meeting.json) and can be undone from the shared stack.
        vm.removeParticipant(id);
        QTRY_COMPARE(vm.peopleState(), QStringLiteral("empty"));
        QVERIFY(m_app->participants(m.id).isEmpty());
        {
            OverviewViewModel fresh(facadetest::overviewOf(m_app.get()));
            fresh.setMeetingId(m.id);
            QCOMPARE(fresh.peopleState(), QStringLiteral("empty"));
        }
        tanara::editing::MeetingUndoService* undo = m_app->meetingUndo(m.id);
        QVERIFY(undo);
        QCOMPARE(undo->undoText(), QStringLiteral("Résztvevő eltávolítva: Fehér Gábor"));
        QVERIFY(undo->undo());
        QTRY_COMPARE(names(vm), QStringList{QStringLiteral("Fehér Gábor")});
        vm.removeParticipant(id);
        QTRY_COMPARE(vm.peopleState(), QStringLiteral("empty"));

        vm.setContextNote(QStringLiteral("  Q4 partner review  "));
        QTRY_COMPARE(vm.contextNote(), QStringLiteral("Q4 partner review"));
        QCOMPARE(m_app->store()->load(m.id).contextNote, QStringLiteral("Q4 partner review"));
    }

    // "Only I spoke": does not work without an own name; with a configured name the decision is saved.
    void soloMeeting()
    {
        const tanara::Meeting m = recording(QStringLiteral("Egyedül"));
        OverviewViewModel vm(facadetest::overviewOf(m_app.get()));
        vm.setMeetingId(m.id);
        tanara::app::AppSettings s = m_app->settings()->settings();
        s.recording.userSpeakerName = QString();
        m_app->settings()->setSettings(s);
        QVERIFY(!vm.setSolo());
        s.recording.userSpeakerName = QStringLiteral("Kovács Lilla");
        m_app->settings()->setSettings(s);
        QVERIFY(vm.setSolo());
        const tanara::Meeting after = m_app->store()->load(m.id);
        QVERIFY(after.approval.has_value());
        QVERIFY(after.approval->solo);
        QTRY_COMPARE(vm.peopleHint(), QStringLiteral("csak én beszéltem"));
        QCOMPARE(stepOf(vm, QStringLiteral("voice")).value("sub").toString(), QStringLiteral("csak én beszéltem"));
    }

    // The row carries the voice sample to listen to (sampleTrackId / sampleStartMs / sampleEndMs, meeting
    // time): before the transcript the participant's cluster sample (parsed from sampleRef), a manual
    // participant has none; after the transcript the speaker's longest line on the cluster's track.
    void rowsCarryTheVoiceSample()
    {
        const tanara::Meeting m = recording(QStringLiteral("Minta"));
        tanara::Participant voice;
        voice.id = QStringLiteral("p-voice");
        voice.source = tanara::ParticipantSource::Voice;
        voice.sides = {QStringLiteral("mic")};
        tanara::Participant manual;
        manual.id = QStringLiteral("p-manual");
        manual.source = tanara::ParticipantSource::Manual;
        tanara::Meeting saved = m_app->store()->load(m.id);
        saved.participants = {voice, manual};
        m_app->store()->saveMeeting(saved);
        tanara::processing::ParticipantAnalysisFile file;
        file.at = QStringLiteral("2026-10-14T10:00:00Z");
        tanara::VoiceCluster small, big, other;
        small.trackId = QStringLiteral("system");
        small.participantId = QStringLiteral("p-voice");
        small.windows = {{1000, 4000}};
        small.sampleRef = QStringLiteral("track_system.wav#1000-4000");
        big.trackId = QStringLiteral("mic");
        big.participantId = QStringLiteral("p-voice");
        big.windows = {{10000, 16000}, {20000, 26000}};
        big.sampleRef = QStringLiteral("track_mic.wav#12500-18500");
        other.trackId = QStringLiteral("mic");
        other.participantId = QStringLiteral("p-else");
        other.windows = {{0, 9000}};
        other.sampleRef = QStringLiteral("track_mic.wav#0-6000");
        file.clusters = {small, big, other};
        QVERIFY(file.save(m.folder));

        OverviewViewModel vm(facadetest::overviewOf(m_app.get()));
        vm.setMeetingId(m.id);
        QCOMPARE(vm.participants().size(), 2);
        const QVariantMap v = vm.participants().at(0).toMap();
        QCOMPARE(v.value("id").toString(), QStringLiteral("p-voice"));
        QCOMPARE(v.value("sampleTrackId").toString(), QStringLiteral("mic"));   // the larger cluster
        QCOMPARE(v.value("sampleStartMs").toLongLong(), 12500);
        QCOMPARE(v.value("sampleEndMs").toLongLong(), 18500);
        const QVariantMap mn = vm.participants().at(1).toMap();
        QCOMPARE(mn.value("sampleTrackId").toString(), QString());
        QCOMPARE(mn.value("sampleStartMs").toLongLong(), -1);
        QCOMPARE(mn.value("sampleEndMs").toLongLong(), -1);
    }

    void rowsCarryTheLongestLineAfterTheTranscript()
    {
        tanara::Meeting m = transcribed(QStringLiteral("Hosszú sor"));
        QFile seg(QDir(m.folder).filePath(QStringLiteral("transcript.segments.json")));
        QVERIFY(seg.open(QIODevice::WriteOnly | QIODevice::Truncate));
        seg.write(QJsonDocument(QJsonArray{
            QJsonObject{{"startMs", 0}, {"endMs", 900}, {"speaker", "Beszélő 1"}, {"text", "Rövid."}},
            QJsonObject{{"startMs", 5000}, {"endMs", 8000}, {"speaker", "Beszélő 1"}, {"text", "Ez a hosszabb."}},
            QJsonObject{{"startMs", 30000}, {"endMs", 60000}, {"speaker", "Beszélő 2"}, {"text", "Nagyon hosszú."}}}).toJson());
        seg.close();
        tanara::Participant p;
        p.id = QStringLiteral("p-one");
        p.source = tanara::ParticipantSource::Voice;
        p.rawSpeakerIds = {QStringLiteral("Beszélő 1")};
        m = m_app->store()->load(m.id);
        m.participants = {p};
        m_app->store()->saveMeeting(m);
        tanara::processing::ParticipantAnalysisFile file;
        file.at = QStringLiteral("2026-10-14T10:00:00Z");
        tanara::VoiceCluster mic, sys;
        mic.trackId = QStringLiteral("mic");
        mic.participantId = QStringLiteral("p-one");
        mic.windows = {{0, 900}};
        mic.sampleRef = QStringLiteral("track_mic.wav#0-900");
        sys.trackId = QStringLiteral("system");
        sys.participantId = QStringLiteral("p-one");
        sys.windows = {{4500, 8500}};
        sys.sampleRef = QStringLiteral("track_system.wav#4500-8500");
        file.clusters = {mic, sys};
        QVERIFY(file.save(m.folder));

        OverviewViewModel vm(facadetest::overviewOf(m_app.get()));
        vm.setMeetingId(m.id);
        QVERIFY(vm.hasTranscript());
        bool found = false;
        for (const QVariant& v : vm.participants()) {
            const QVariantMap row = v.toMap();
            if (row.value("id").toString() != QLatin1String("p-one")) continue;
            found = true;
            QCOMPARE(row.value("sampleTrackId").toString(), QStringLiteral("system"));   // overlaps the line most
            QCOMPARE(row.value("sampleStartMs").toLongLong(), 5000);
            QCOMPARE(row.value("sampleEndMs").toLongLong(), 8000);
        }
        QVERIFY(found);
        // A speaker with no participant record (no cluster): nothing to play.
        for (const QVariant& v : vm.participants())
            if (v.toMap().value("id").toString() != QLatin1String("p-one"))
                QCOMPARE(v.toMap().value("sampleTrackId").toString(), QString());
    }

    // After the transcript, with only anonymous speakers: "nobody" + the raw speakers with their share; DATA tiles.
    void afterTranscript_nobodyAndStats()
    {
        const tanara::Meeting m = transcribed(QStringLiteral("Utána"));
        OverviewViewModel vm(facadetest::overviewOf(m_app.get()));
        vm.setMeetingId(m.id);
        QVERIFY(vm.hasTranscript());
        QCOMPARE(vm.peopleState(), QStringLiteral("nobody"));
        // The rows by speaker key, as anonymous (nameable).
        QCOMPARE(vm.participants().size(), 2);
        for (const QVariant& v : vm.participants()) {
            QVERIFY(!v.toMap().value("speakerKey").toString().isEmpty());
            QVERIFY(v.toMap().value("anonymous").toBool());
        }
        const QVariantMap first = vm.participants().first().toMap();
        QCOMPARE(first.value("name").toString(), QStringLiteral("Beszélő 1"));
        QVERIFY(first.value("talkShare").toDouble() > 0.6);
        QCOMPARE(stepOf(vm, QStringLiteral("transcript")).value("state").toString(), QStringLiteral("done"));
        QCOMPARE(stepOf(vm, QStringLiteral("transcript")).value("sub").toString(),
                 QStringLiteral("3 megszólalás · 2 beszélő"));
        QCOMPARE(vm.stats().size(), 6);
        QCOMPARE(vm.stats().at(0).toMap().value("value").toString(), QStringLiteral("1:16:04"));
        QCOMPARE(vm.stats().at(2).toMap().value("value").toString(), QStringLiteral("2"));
        QCOMPARE(vm.stats().at(2).toMap().value("sub").toString(), QStringLiteral("2 névtelen"));
        QCOMPARE(vm.stats().at(3).toMap().value("value").toString(), QStringLiteral("3"));
        QCOMPARE(vm.stats().at(5).toMap().value("value").toString(), QStringLiteral("nincs"));
    }

    // After the transcript, a participant not bound to a speaker (added by hand, never spoke)
    // shows dimmed after the speakers, with a "not heard" badge; it can be deleted permanently
    // and undone.
    void afterTranscript_unheardParticipant()
    {
        const tanara::Meeting m = transcribed(QStringLiteral("Nem hallott"));
        OverviewViewModel vm(facadetest::overviewOf(m_app.get()));
        vm.setMeetingId(m.id);
        QCOMPARE(vm.peopleState(), QStringLiteral("nobody"));
        const QString id = vm.addParticipant(QStringLiteral("Lantos Réka"));
        QVERIFY(!id.isEmpty());
        QTRY_COMPARE(vm.participants().size(), 3);
        QCOMPARE(vm.peopleState(), QStringLiteral("list"));
        const QVariantMap row = vm.participants().last().toMap();
        QCOMPARE(row.value("id").toString(), id);
        QCOMPARE(row.value("name").toString(), QStringLiteral("Lantos Réka"));
        QVERIFY(row.value("unheard").toBool());
        QVERIFY(row.value("dimmed").toBool());
        QVERIFY(row.value("removable").toBool());
        QVERIFY(row.value("speakerKey").toString().isEmpty());
        const QVariantMap badge = row.value("sources").toList().first().toMap();
        QCOMPARE(badge.value("text").toString(), QStringLiteral("nem hallottuk"));
        QVERIFY(badge.value("negative").toBool());
        // The speakers' rows come first, not dimmed.
        QVERIFY(!vm.participants().first().toMap().value("unheard").toBool());

        vm.removeParticipant(id);
        QTRY_COMPARE(vm.participants().size(), 2);
        QCOMPARE(vm.peopleState(), QStringLiteral("nobody"));
        QVERIFY(m_app->participants(m.id).isEmpty());
        QVERIFY(m_app->meetingUndo(m.id)->undo());
        QTRY_COMPARE(vm.participants().size(), 3);
    }

    // Tag-based person suggestion: at most 3; the x is a persistent rejection for this meeting
    // (TagService::rejectPersonForMeeting), undoable from the shared stack.
    void tagSuggestions_limitAndDurableReject()
    {
        tanara::library::TagService* tags = m_app->tags();
        const tanara::Tag nord = tags->create(QStringLiteral("Nordvik"));
        const QStringList crew{QStringLiteral("Fehér Gábor"), QStringLiteral("Varga Árpád"),
                               QStringLiteral("Molnár Eszter"), QStringLiteral("Szabó Bence")};
        for (int i = 1; i <= 3; ++i) {
            tanara::Meeting t = m_app->store()->createMeeting(QStringLiteral("Nordvik %1").arg(i));
            t.startedAt = QDateTime(QDate(2026, 9, i), QTime(10, 0));
            for (int k = 0; k < crew.size(); ++k)
                t.speakerMap.insert(QStringLiteral("Beszélő %1").arg(k + 1), m_app->people()->ensure(crew.at(k)));
            t.tagIds = {nord.id};
            m_app->store()->saveMeeting(t);
        }
        tanara::Meeting m = recording(QStringLiteral("Új Nordvik"));
        m.tagIds = {nord.id};
        m_app->store()->saveMeeting(m);
        tags->refreshPersonStatsNow();

        OverviewViewModel vm(facadetest::overviewOf(m_app.get()));
        vm.setMeetingId(m.id);
        auto suggested = [&vm] {
            QStringList out;
            for (const QVariant& v : vm.participants())
                if (v.toMap().value("suggested").toBool()) out << v.toMap().value("name").toString();
            return out;
        };
        QCOMPARE(suggested().size(), 3);
        QCOMPARE(vm.peopleState(), QStringLiteral("list"));
        const QString rejected = suggested().first();
        const tanara::PersonId rejectedId = m_app->people()->idForName(rejected);
        QVERIFY(!rejectedId.isNull());
        vm.removeParticipant(QStringLiteral("suggest:") + rejectedId.toString());
        QTRY_VERIFY(!suggested().contains(rejected));
        QCOMPARE(suggested().size(), 3);   // the fourth takes its place
        QVERIFY(tags->isPersonRejectedForMeeting(rejectedId, m.id));
        {
            OverviewViewModel fresh(facadetest::overviewOf(m_app.get()));
            fresh.setMeetingId(m.id);
            for (const QVariant& v : fresh.participants())
                QVERIFY(v.toMap().value("name").toString() != rejected);
        }
        QVERIFY(m_app->meetingUndo(m.id)->undo());
        QVERIFY(!tags->isPersonRejectedForMeeting(rejectedId, m.id));
        vm.refresh();
        QVERIFY(suggested().contains(rejected));

        // "Add": becomes a participant and drops out of the suggestions.
        QVERIFY(!vm.addParticipant(rejected).isEmpty());
        QTRY_VERIFY(!suggested().contains(rejected));
        QVERIFY(names(vm).contains(rejected));
    }

    // The tracks' speech share, the reason for exclusion and "Include" (TrackListModel -> AppController).
    void tracks_speechAndInclude()
    {
        const tanara::Meeting m = recording(QStringLiteral("Sávok"));
        TrackListModel tracks;
        tracks.setFacade(AppContext::instance()->servicesFor(m_app.get()).tracks(QString(), nullptr));
        tracks.setMeetingId(m.id);
        QCOMPARE(tracks.count(), 2);
        QCOMPARE(tracks.includedCount(), 1);
        QCOMPARE(tracks.excludedCount(), 1);
        int sys = -1;
        for (int i = 0; i < tracks.count(); ++i)
            if (tracks.trackIdAt(i) == QLatin1String("system")) sys = i;
        QVERIFY(sys >= 0);
        const QModelIndex idx = tracks.index(sys);
        QCOMPARE(tracks.data(idx, TrackListModel::ExcludedReasonRole).toString(), QStringLiteral("noSpeech"));
        QCOMPARE(tracks.data(idx, TrackListModel::SpeechRatioRole).toDouble(), 0.0);
        QCOMPARE(tracks.data(idx, TrackListModel::KindRole).toString(), QStringLiteral("loopback"));
        QVERIFY(!tracks.data(idx, TrackListModel::IncludedRole).toBool());

        tracks.setIncluded(sys, true);
        QTRY_COMPARE(tracks.includedCount(), 2);
        const tanara::Meeting after = m_app->store()->load(m.id);
        for (const tanara::Track& t : after.tracks)
            QVERIFY2(t.included(), qPrintable(t.id));
        tracks.setIncluded(0, false);
        QTRY_COMPARE(tracks.includedCount(), 1);
        QCOMPARE(m_app->store()->load(m.id).tracks.at(0).excludedReason, QStringLiteral("manual"));
    }

    // The tab state for the shell: whether there is a summary, whether a transcription is running.
    void shellMeetingTabState()
    {
        const tanara::Meeting m = transcribed(QStringLiteral("Fülek"));
        ShellMeetingModel model(facadetest::shellOf(m_app.get()));
        model.setMeetingId(m.id);
        QVERIFY(model.hasTranscript());
        QVERIFY(!model.hasSummary());
        QCOMPARE(model.transcribePercent(), -1);
        QCOMPARE(model.summarizePercent(), -1);
    }

    static QVariantMap rowOf(const OverviewViewModel& vm, const QString& id)
    {
        for (const QVariant& v : vm.participants())
            if (v.toMap().value(QStringLiteral("id")).toString() == id) return v.toMap();
        return {};
    }
    static tanara::Evidence evidence(tanara::EvidenceKind kind, tanara::Polarity polarity)
    {
        tanara::Evidence e;
        e.kind = kind;
        e.polarity = polarity;
        e.value = 0.8;
        return e;
    }

    // The "Who was there?" card's keys on real data: the decision ("pending" while there are voice
    // candidates and no decision), "Approve · N" (= what ParticipantsViewModel approves), doubtful /
    // counted / voiceless rows, and the row operations through the view model (confirm, "This is me"
    // with its reason, naming merging the manual voiceless row, "same voice" merge).
    void card_decisionKeysAndRowOperations()
    {
        using namespace tanara;
        Meeting m = recording(QStringLiteral("Kártya"));
        Participant sure;
        sure.id = QStringLiteral("p-sure");
        sure.personId = m_app->people()->ensure(QStringLiteral("Fehér Gábor"));
        sure.sides = {QStringLiteral("loopback")};
        sure.evidence = {evidence(EvidenceKind::Voice, Polarity::Support), evidence(EvidenceKind::Tag, Polarity::Support)};
        Participant doubt;
        doubt.id = QStringLiteral("p-doubt");
        doubt.personId = m_app->people()->ensure(QStringLiteral("Varga Árpád"));
        doubt.sides = {QStringLiteral("loopback")};
        doubt.evidence = {evidence(EvidenceKind::Voice, Polarity::Support), evidence(EvidenceKind::Tag, Polarity::Contradict)};
        Participant anon;
        anon.id = QStringLiteral("p-anon");
        anon.sides = {QStringLiteral("mic")};
        Participant manual;
        manual.id = QStringLiteral("p-man");
        manual.personId = m_app->people()->ensure(QStringLiteral("Molnár Eszter"));
        manual.source = ParticipantSource::Manual;
        Meeting saved = m_app->store()->load(m.id);
        saved.participants = {sure, doubt, anon, manual};
        m_app->store()->saveMeeting(saved);
        app::AppSettings settings = m_app->settings()->settings();
        settings.recording.userSpeakerName = QString();
        m_app->settings()->setSettings(settings);

        OverviewViewModel vm(facadetest::overviewOf(m_app.get()));
        vm.setMeetingId(m.id);
        QCOMPARE(vm.decision(), QStringLiteral("pending"));
        QVERIFY(vm.approvalPending());
        QVERIFY(rowOf(vm, "p-sure").value("counted").toBool());
        QVERIFY(!rowOf(vm, "p-sure").value("doubtful").toBool());
        QVERIFY(!rowOf(vm, "p-sure").value("voiceless").toBool());
        QVERIFY(!rowOf(vm, "p-doubt").value("counted").toBool());
        QVERIFY(rowOf(vm, "p-doubt").value("doubtful").toBool());
        QVERIFY(!rowOf(vm, "p-anon").value("counted").toBool());
        QVERIFY(!rowOf(vm, "p-anon").value("doubtful").toBool());
        QVERIFY(rowOf(vm, "p-anon").value("anonymous").toBool());
        QVERIFY(rowOf(vm, "p-man").value("voiceless").toBool());
        QVERIFY(rowOf(vm, "p-man").value("counted").toBool());
        QCOMPARE(vm.approveCount(), 2);

        // "Was there" on the doubtful row: counted, one undo step; an anonymous row cannot be confirmed.
        QVERIFY(vm.confirmParticipant(QStringLiteral("p-doubt"), true));
        QTRY_COMPARE(vm.approveCount(), 3);
        QVERIFY(rowOf(vm, "p-doubt").value("approved").toBool());
        QVERIFY(!vm.confirmParticipant(QStringLiteral("p-anon"), true));
        QVERIFY(m_app->meetingUndo(m.id)->undo());
        QTRY_COMPARE(vm.approveCount(), 2);
        QVERIFY(vm.confirmParticipant(QStringLiteral("p-doubt"), true));
        QTRY_COMPARE(vm.approveCount(), 3);

        // "This is me" without an own name: fails with a reason for the panel.
        const QVariantMap self = vm.setSelfParticipant(QStringLiteral("p-anon"));
        QVERIFY(!self.value("ok").toBool());
        QVERIFY(!self.value("reason").toString().isEmpty());
        QVERIFY(vm.selfName().isEmpty());

        // Naming the unknown voice after the person's manual voiceless row: the two rows become one.
        QVERIFY(vm.nameParticipant(QStringLiteral("p-anon"), QStringLiteral("Molnár Eszter")));
        QTRY_VERIFY(rowOf(vm, "p-man").isEmpty());
        QCOMPARE(rowOf(vm, "p-anon").value("name").toString(), QStringLiteral("Molnár Eszter"));
        QVERIFY(!rowOf(vm, "p-anon").value("voiceless").toBool());
        QVERIFY(!rowOf(vm, "p-anon").value("anonymous").toBool());

        // "Same voice as ...": the doubtful row joins the sure one (meeting-local).
        QVERIFY(vm.mergeParticipantInto(QStringLiteral("p-doubt"), QStringLiteral("p-sure")));
        QTRY_VERIFY(rowOf(vm, "p-doubt").isEmpty());
        QVERIFY(!rowOf(vm, "p-sure").isEmpty());

        // "Approve · N" is what the participants view model approves (the card's footer).
        ParticipantsViewModel decision(AppContext::instance()->servicesFor(m_app.get()));
        decision.setMeetingId(m.id);
        decision.reload();
        QCOMPARE(decision.checkedCount(), vm.approveCount());
        const int n = vm.approveCount();
        QVERIFY(decision.approve());
        QTRY_COMPARE(vm.decision(), QStringLiteral("approved"));
        QCOMPARE(vm.approveCount(), n);
        QVERIFY(!vm.approvalPending());
        // A demo-only decision does nothing on real data.
        QVERIFY(!vm.decideInDemo(QStringLiteral("skipped")));

        // With an own name: "This is me" works and the panel knows the name; "Only I spoke" -> "solo".
        m_app->setUserSpeakerName(QStringLiteral("Kovács Lilla"));
        const tanara::Meeting other = recording(QStringLiteral("Egyedül a kártyán"));
        Meeting otherSaved = m_app->store()->load(other.id);
        Participant voice;
        voice.id = QStringLiteral("p-voice");
        voice.sides = {QStringLiteral("mic")};
        otherSaved.participants = {voice};
        m_app->store()->saveMeeting(otherSaved);
        OverviewViewModel second(facadetest::overviewOf(m_app.get()));
        second.setMeetingId(other.id);
        QCOMPARE(second.selfName(), QStringLiteral("Kovács Lilla"));
        QVERIFY(second.setSelfParticipant(QStringLiteral("p-voice")).value("ok").toBool());
        QTRY_VERIFY(rowOf(second, "p-voice").value("self").toBool());
        QVERIFY(second.setSolo());
        QTRY_COMPARE(second.decision(), QStringLiteral("solo"));
    }

    // The card's demo states (OverviewDemoFacade): decision, "Approve · N", and the row operations in
    // memory (naming, "Was there", "same voice", "This is me", the decisions).
    void card_demoStatesAndOperations()
    {
        m_app.reset();
        AppContext::instance()->setDemo(true);
        OverviewViewModel vm;
        vm.setDemoState(QStringLiteral("overviewProcessing"));
        QCOMPARE(vm.decision(), QStringLiteral("pending"));
        QCOMPARE(vm.approveCount(), 2);
        QVERIFY(rowOf(vm, "p4").value("doubtful").toBool());
        QVERIFY(rowOf(vm, "v1").value("anonymous").toBool());
        QVERIFY(!vm.selfName().isEmpty());
        QVERIFY(vm.confirmParticipant(QStringLiteral("p4"), true));
        QCOMPARE(vm.approveCount(), 3);
        QVERIFY(!vm.confirmParticipant(QStringLiteral("v1"), true));     // a name is needed
        QVERIFY(vm.nameParticipant(QStringLiteral("v1"), QStringLiteral("Molnár Eszter")));
        QCOMPARE(vm.approveCount(), 4);
        // The suggestion with that name dissolved into the named voice.
        QVERIFY(rowOf(vm, QStringLiteral("suggest:Molnár Eszter")).isEmpty());
        QVERIFY(vm.mergeParticipantInto(QStringLiteral("v2"), QStringLiteral("p1")));
        QVERIFY(rowOf(vm, "v2").isEmpty());
        QVERIFY(vm.decideInDemo(QStringLiteral("approved")));
        QCOMPARE(vm.decision(), QStringLiteral("approved"));
        QVERIFY(!vm.approvalPending());
        QCOMPARE(vm.approveCount(), 4);

        vm.setDemoState(QStringLiteral("overviewVoiceless"));
        QVERIFY(rowOf(vm, "t1").value("voiceless").toBool());
        QVERIFY(vm.setSelfParticipant(QStringLiteral("v1")).value("ok").toBool());
        QVERIFY(rowOf(vm, "p1").isEmpty());                                // the earlier own row gave way
        QVERIFY(rowOf(vm, "v1").value("self").toBool());

        vm.setDemoState(QStringLiteral("overviewApproved"));
        QCOMPARE(vm.decision(), QStringLiteral("approved"));
        QCOMPARE(vm.approveCount(), 4);
        vm.setDemoState(QStringLiteral("overviewSkipped"));
        QCOMPARE(vm.decision(), QStringLiteral("skipped"));
        vm.setDemoState(QStringLiteral("overviewSolo"));
        QCOMPARE(vm.decision(), QStringLiteral("solo"));
        vm.setDemoState(QStringLiteral("overviewNoVoiceModel"));
        QCOMPARE(stepOf(vm, QStringLiteral("voice")).value("fixTarget").toString(), QStringLiteral("speakers"));
        QVERIFY(rowOf(vm, "m1").value("voiceless").toBool());
        vm.setDemoState(QStringLiteral("overviewDone"));
        QCOMPARE(vm.hiddenSpeakers(), 1);
        QVERIFY(rowOf(vm, "p2").value("lineCount").toInt() > 0);
        vm.setDemoState(QStringLiteral("overviewNobody"));
        QCOMPARE(vm.participants().size(), 3);
        QVERIFY(vm.nameParticipant(QStringLiteral("S1"), QStringLiteral("Szabó Bence")));
        QCOMPARE(rowOf(vm, "S1").value("name").toString(), QStringLiteral("Szabó Bence"));
        AppContext::instance()->setDemo(false);
    }

    // Demo (screenshot): the made-up data of the four states.
    void demoStates()
    {
        m_app.reset();
        AppContext::instance()->setDemo(true);
        OverviewViewModel vm;
        vm.setDemoState(QStringLiteral("overviewProcessing"));
        QVERIFY(vm.approvalPending());
        QVERIFY(vm.transcribing());
        QCOMPARE(vm.candidateCount(), 5);
        QCOMPARE(vm.participants().size(), 6);
        QVERIFY(vm.participants().last().toMap().value("suggested").toBool());
        QCOMPARE(stepOf(vm, QStringLiteral("voice")).value("state").toString(), QStringLiteral("attention"));
        QCOMPARE(stepOf(vm, QStringLiteral("transcript")).value("progress").toInt(), 62);
        // "Add" / x on the suggestion.
        vm.removeParticipant(QStringLiteral("suggest:Molnár Eszter"));
        QCOMPARE(vm.participants().size(), 5);

        vm.setDemoState(QStringLiteral("overviewDone"));
        QVERIFY(vm.allDone());
        QCOMPARE(vm.stats().size(), 6);
        QVERIFY(vm.approved());

        vm.setDemoState(QStringLiteral("overviewEmpty"));
        QCOMPARE(vm.peopleState(), QStringLiteral("empty"));
        QVERIFY(vm.contextNote().isEmpty());

        // An anonymous line can be named in place in demo mode; the finished rows have a speaker key.
        vm.setDemoState(QStringLiteral("overviewDone"));
        bool found = false;
        int unheard = 0;
        for (const QVariant& v : vm.participants()) {
            const QVariantMap row = v.toMap();
            // The invited participant who was not heard is dimmed, with no speaker key.
            if (row.value("unheard").toBool()) {
                ++unheard;
                QVERIFY(row.value("dimmed").toBool());
                QVERIFY(row.value("speakerKey").toString().isEmpty());
                continue;
            }
            QVERIFY(!row.value("speakerKey").toString().isEmpty());
            if (!row.value("anonymous").toBool()) continue;
            found = true;
            QVERIFY(vm.nameParticipant(row.value("id").toString(), QStringLiteral("Szabó Bence")));
        }
        QVERIFY(found);
        QCOMPARE(unheard, 1);
        // The heard rows carry a fictional sample; the dim (not heard) row does not.
        int withSample = 0;
        for (const QVariant& v : vm.participants()) {
            const QVariantMap row = v.toMap();
            const bool has = !row.value("sampleTrackId").toString().isEmpty();
            QCOMPARE(has, !row.value("unheard").toBool());
            if (has) {
                ++withSample;
                QCOMPARE(row.value("sampleEndMs").toLongLong() - row.value("sampleStartMs").toLongLong(), 6000);
            }
        }
        QVERIFY(withSample >= 4);
        QVERIFY(names(vm).contains(QStringLiteral("Szabó Bence")));
        QVERIFY(vm.canReanalyze());

        // The first transcription is running: two tracks, nobody yet, the transcript step is running.
        vm.setDemoState(QStringLiteral("overviewTranscribing"));
        QVERIFY(vm.transcribing());
        QVERIFY(!vm.hasTranscript());
        QCOMPARE(vm.peopleState(), QStringLiteral("empty"));
        QCOMPARE(stepOf(vm, QStringLiteral("transcript")).value("state").toString(), QStringLiteral("running"));
        QVERIFY(!vm.canReanalyze());

        vm.setDemoState(QStringLiteral("overviewNobody"));
        QCOMPARE(vm.peopleState(), QStringLiteral("nobody"));
        QCOMPARE(vm.participants().size(), 3);
        AppContext::instance()->setDemo(false);
    }
};

QTEST_MAIN(TestOverviewViewModel)
#include "test_overview_view_model.moc"
