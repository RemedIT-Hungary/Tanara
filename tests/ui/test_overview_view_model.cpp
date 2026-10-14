// The Overview tab's view model (OverviewViewModel) with a real AppController in an ISOLATED
// TANARA_HOME (QTemporaryDir; no audio, no model, no network): adding and removing participants
// by hand, "only I spoke", the manual description, the processing steps before and after the
// transcript, the DATA tiles, the "nobody" state (only anonymous speakers), the tracks' speech
// share and including them in the mixdown (TrackListModel). Demo data: the four screen states.
#include "AppContext.h"
#include "UiServices.h"
#include "FacadeTestSupport.h"
#include "OverviewViewModel.h"
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
        QVERIFY(!vm.nameSpeakerInDemo(QStringLiteral("x"), QStringLiteral("Y")));   // not a demo on real data

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
        QCOMPARE(vm.anonymousSpeakers().size(), 2);
        // The rows by speaker key, as anonymous (nameable).
        QCOMPARE(vm.participants().size(), 2);
        for (const QVariant& v : vm.participants()) {
            QVERIFY(!v.toMap().value("speakerKey").toString().isEmpty());
            QVERIFY(v.toMap().value("anonymous").toBool());
        }
        const QVariantMap first = vm.anonymousSpeakers().first().toMap();
        QCOMPARE(first.value("name").toString(), QStringLiteral("Beszélő 1"));
        QVERIFY(first.value("share").toDouble() > 0.6);
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

    // Demo (screenshot): the made-up data of the four states.
    void demoStates()
    {
        m_app.reset();
        AppContext::instance()->setDemo(true);
        OverviewViewModel vm;
        vm.setDemoState(QStringLiteral("overviewProcessing"));
        QVERIFY(vm.approvalPending());
        QVERIFY(vm.transcribing());
        QCOMPARE(vm.candidateCount(), 4);
        QCOMPARE(vm.participants().size(), 6);
        QVERIFY(vm.participants().last().toMap().value("suggested").toBool());
        QCOMPARE(stepOf(vm, QStringLiteral("voice")).value("state").toString(), QStringLiteral("attention"));
        QCOMPARE(stepOf(vm, QStringLiteral("transcript")).value("progress").toInt(), 62);
        // "Add" / x on the suggestion.
        vm.removeParticipant(QStringLiteral("suggest:Szabó Bence"));
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
            QVERIFY(vm.nameSpeakerInDemo(row.value("speakerKey").toString(), QStringLiteral("Szabó Bence")));
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
        QCOMPARE(vm.anonymousSpeakers().size(), 3);
        AppContext::instance()->setDemo(false);
    }
};

QTEST_MAIN(TestOverviewViewModel)
#include "test_overview_view_model.moc"
