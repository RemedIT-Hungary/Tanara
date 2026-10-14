// ShellActions + ShellMeetingModel + ShellUiState: the shell's action and state layer.
// Real AppController, ISOLATED TANARA_HOME (QTemporaryDir), with a fake bridge (in place of the
// Widgets windows): the gating path of runnability, then the cloud blocker, then Settings on
// the right page, then the start. NO real provider call (the task is canceled right after
// starting; the provider address is a closed local port).
#include "AppContext.h"
#include "FacadeTestSupport.h"
#include "PlayerController.h"
#include "ShellActions.h"
#include "ShellBridge.h"
#include "ShellMeetingModel.h"
#include "ShellUiState.h"

#include "tanara/app/AppController.h"
#include "tanara/library/store/PeopleStore.h"
#include "tanara/app/SettingsManager.h"
#include "tanara/cloud/CloudTypes.h"
#include "tanara/editing/session/SpeakerEditor.h"
#include "tanara/editing/session/SpeakerOverlay.h"
#include "tanara/processing/analysis/UtteranceEmbeddings.h"
#include "tanara/library/jobs/MeetingJobTracker.h"
#include "tanara/library/store/MeetingArchive.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/tags/TagService.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

#include <memory>

using namespace tanara_qml;

namespace {

class FakeBridge : public ShellBridge {
public:
    using ShellBridge::ShellBridge;

    QStringList calls;
    QString previewSummary = QStringLiteral("Kovács Lilla és 1 ismeretlen partner");
    bool previewCancelled = false;

    void openSettings(const QString& page) override { calls << QStringLiteral("settings:") + page; }
    // The deep link's field; the page is logged the same way.
    void openSettingsAt(const QString& page, const QString& focusField) override
    {
        lastFocusField = focusField;
        openSettings(page);
    }
    QString lastFocusField;
    void openPeople() override { calls << QStringLiteral("people"); }
    void openPeopleAt(const QString& person) override { calls << QStringLiteral("people:") + person; }
    void openTags() override { calls << QStringLiteral("tags"); }
    void openTagsAt(const QString& tagId) override { calls << QStringLiteral("tags:") + tagId; }
    void openOnboarding() override { calls << QStringLiteral("onboarding"); }
    void openRecorder() override { calls << QStringLiteral("recorder"); }
    QString pickAudioFile() override { calls << QStringLiteral("pick"); return QStringLiteral("/tmp/x.ogg"); }
    QStringList pickAudioFiles() override { calls << QStringLiteral("pickMany"); return pickedFiles; }
    QStringList pickedFiles;
    QString pickSaveFile(const QString&, const QString& proposedPath, const QString& filter) override
    {
        calls << QStringLiteral("save");
        lastProposed = proposedPath;
        lastFilter = filter;
        return savePath;
    }
    QString pickArchiveFile() override { calls << QStringLiteral("pickArchive"); return archivePath; }
    QString pickMeetingFolder() override { calls << QStringLiteral("pickFolder"); return folderPath; }
    QString savePath, archivePath, folderPath, lastProposed, lastFilter;
    QString identifyParticipantsPreview(const QString&, bool* cancelled) override
    {
        calls << QStringLiteral("preview");
        if (cancelled) *cancelled = previewCancelled;
        return previewCancelled ? QString() : previewSummary;
    }
    void continueRecordingInBackground() override {}
    void stopRecordingAndQuit() override {}
    void shutdown() override {}
    void windowShown() override {}
};

} // namespace

class TestShellActions : public QObject {
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_home;
    std::unique_ptr<tanara::app::AppController> m_app;
    std::unique_ptr<FakeBridge> m_bridge;
    std::unique_ptr<ShellActions> m_shell;
    QStringList m_toasts;

    tanara::Meeting recording(const QString& title)
    {
        tanara::Meeting m = m_app->store()->createMeeting(title);
        tanara::Track t;
        t.id = QStringLiteral("mic");
        t.kind = tanara::TrackKind::Mic;
        t.file = QStringLiteral("track_mic.wav");
        t.active = true;
        m.tracks = {t};
        m.durationMs = 4564000;   // 1:16:04
        QFile f(QDir(m.folder).filePath(t.file));
        if (f.open(QIODevice::WriteOnly)) f.write("nem valódi hang");
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
        // Processing works from the tokens (the summary does not start on an empty token list).
        QJsonArray toks;
        const QStringList words{QStringLiteral("Sziasztok."), QStringLiteral(" Szia."), QStringLiteral(" Kezdjük.")};
        for (int i = 0; i < words.size(); ++i)
            toks.append(QJsonObject{{"text", words[i]}, {"speaker", i == 1 ? "Beszélő 2" : "Beszélő 1"},
                                    {"startMs", i * 1000}, {"endMs", i * 1000 + 900},
                                    {"confidence", 0.9}, {"trackId", "mixdown"}});
        QFile tok(QDir(m.folder).filePath(QStringLiteral("transcript.tokens.json")));
        if (tok.open(QIODevice::WriteOnly))
            tok.write(QJsonDocument(QJsonObject{{"language", "hu"}, {"tokens", toks}}).toJson());
        m.hasTranscript = true;
        m_app->store()->saveMeeting(m);
        return m;
    }
    // A transcribed meeting with EVERY speaker named, with made-up voice embeddings (the cache is
    // written directly: no model, no audio): under Anna's label there are 3 lines in Béla's voice.
    tanara::Meeting namedWithVoices(const QString& title)
    {
        // The made-up cache is under the "campplus" key, so we switch that model on.
        m_app->settings()->setEnabledVoiceModels({QStringLiteral("campplus")});
        tanara::Meeting m = recording(title);
        struct L { int start; const char* raw; char voice; };
        const QVector<L> rows{{0, "Beszélő 1", 'A'}, {5, "Beszélő 2", 'B'}, {10, "Beszélő 1", 'A'},
                              {15, "Beszélő 1", 'P'}, {20, "Beszélő 2", 'B'}, {25, "Beszélő 1", 'A'},
                              {30, "Beszélő 1", 'P'}, {35, "Beszélő 2", 'B'}, {40, "Beszélő 1", 'A'},
                              {45, "Beszélő 1", 'P'}, {50, "Beszélő 2", 'B'}};
        QJsonArray segs;
        for (const L& r : rows)
            segs.append(QJsonObject{{"startMs", r.start * 1000}, {"endMs", r.start * 1000 + 4000},
                                    {"speaker", QString::fromUtf8(r.raw)}, {"text", QStringLiteral("Kitalált sor.")}});
        QFile seg(tanara::editing::speakeredit::segmentsPath(m.folder));
        if (seg.open(QIODevice::WriteOnly)) seg.write(QJsonDocument(segs).toJson());
        seg.close();
        const QVector<tanara::TranscriptLine> lines = tanara::editing::speakeredit::loadTranscriptLines(m.folder);
        tanara::processing::UtteranceEmbeddingCache cache;
        cache.fingerprint = tanara::editing::speakeredit::transcriptFingerprint(lines);
        for (int i = 0; i < rows.size(); ++i)
            cache.set(QStringLiteral("campplus"), lines[i].id, rows[i].voice == 'A' ? QVector<float>{1, 0, 0}
                                            : rows[i].voice == 'B' ? QVector<float>{0, 1, 0}
                                                                   : QVector<float>{0.6f, 0.8f, 0});
        cache.save(m.folder);
        m.hasTranscript = true;
        m.speakerMap = {{QStringLiteral("Beszélő 1"), m_app->people()->ensure(QStringLiteral("Anna"))},
                        {QStringLiteral("Beszélő 2"), m_app->people()->ensure(QStringLiteral("Béla"))}};
        m_app->store()->saveMeeting(m);
        return m;
    }
    void configureProviders(bool withSttKey)
    {
        tanara::app::AppSettings s = m_app->settings()->settings();
        s.providers.stt.selectedId = QStringLiteral("soniox");
        s.providers.stt.configs[s.providers.stt.selectedId].baseUrl = QStringLiteral("http://127.0.0.1:9/v1");   // closed port
        s.providers.llm.selectedId = QStringLiteral("openai-compat");
        s.providers.llm.configs[s.providers.llm.selectedId].baseUrl = QStringLiteral("http://127.0.0.1:9/v1");
        s.providers.llm.configs[s.providers.llm.selectedId].model = QStringLiteral("teszt-modell");
        m_app->settings()->setSettings(s);
        if (withSttKey)
            m_app->setSecret(tanara::app::keys::SonioxApiKey, QStringLiteral("teszt-kulcs"));
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
        m_bridge = std::make_unique<FakeBridge>();
        m_shell = std::make_unique<ShellActions>(facadetest::shellOf(m_app.get()));
        m_shell->setBridge(m_bridge.get());
        m_toasts.clear();
        connect(m_shell.get(), &ShellActions::toastRequested, this,
                [this](const QString& text) { m_toasts << text; });
    }

    void cleanup()
    {
        m_shell.reset();
        m_bridge.reset();
        m_app.reset();
        m_home.reset();
        qunsetenv("TANARA_HOME");
    }

    void navigation()
    {
        const tanara::Meeting a = transcribed(QStringLiteral("A"));
        QSignalSpy current(m_shell.get(), &ShellActions::currentMeetingIdChanged);
        QSignalSpy positions(m_shell.get(), &ShellActions::transcriptPositionRequested);
        PlayerController player;
        player.setController(m_app.get());
        m_shell->setPlayer(&player);

        m_shell->showTab(3);
        QCOMPARE(m_shell->currentTab(), 3);
        m_shell->showTab(9);
        QCOMPARE(m_shell->currentTab(), 3);      // stays within bounds (0 Overview ... 3 Memo)

        // seekTo: selects, switches to the Transcript tab (1), moves the player, signals the editor.
        m_shell->seekTo(a.id, 1500);
        QCOMPARE(m_shell->currentMeetingId(), a.id);
        QCOMPARE(current.count(), 1);
        QCOMPARE(m_shell->currentTab(), 1);
        QCOMPARE(player.meetingId(), a.id);
        QCOMPARE(player.positionMs(), 1500);
        QCOMPARE(positions.count(), 1);
        QCOMPARE(positions.at(0).at(0).toInt(), 1500);

        QVERIFY(m_shell->meetingExists(a.id));
        QVERIFY(!m_shell->meetingExists(QStringLiteral("nincs-ilyen")));
        m_shell->setPlayer(nullptr);
    }

    // The selected meeting's shared undo stack: a new step posts meetingChangePosted (once per
    // event loop pass), undo (Ctrl+Z / bar / toast "meeting" key) posts meetingChangeUndone, and
    // an undo is not a "new step".
    void sharedMeetingUndo()
    {
        const tanara::Meeting a = recording(QStringLiteral("Közös verem"));
        const tanara::Meeting b = recording(QStringLiteral("Másik"));
        QSignalSpy posted(m_shell.get(), &ShellActions::meetingChangePosted);
        QSignalSpy undone(m_shell.get(), &ShellActions::meetingChangeUndone);
        QVERIFY(!m_shell->hasMeetingUndo());
        QVERIFY(!m_shell->undoMeeting());
        m_shell->setCurrentMeetingId(a.id);
        QVERIFY(m_shell->hasMeetingUndo());
        QVERIFY(!m_shell->canUndoMeeting());

        QVERIFY(!m_app->addParticipant(a.id, m_app->people()->choiceFor(QStringLiteral("Fehér Gábor"))).isEmpty());
        QTRY_COMPARE(posted.count(), 1);
        QCOMPARE(posted.at(0).at(0).toString(), QStringLiteral("Résztvevő felvéve: Fehér Gábor"));
        QCOMPARE(posted.at(0).at(1).toInt(), 2);   // Source::Participants
        QVERIFY(m_shell->canUndoMeeting());
        QCOMPARE(m_shell->meetingUndoText(), QStringLiteral("Résztvevő felvéve: Fehér Gábor"));
        m_app->setMeetingContextNote(a.id, QStringLiteral("Q4"));
        QTRY_COMPARE(posted.count(), 2);
        // Another meeting's step does not come here.
        m_app->setMeetingContextNote(b.id, QStringLiteral("más"));
        QTest::qWait(20);
        QCOMPARE(posted.count(), 2);

        m_shell->undoFromToast(QStringLiteral("meeting"));
        QCOMPARE(undone.count(), 1);
        QCOMPARE(undone.at(0).at(0).toString(), QStringLiteral("Leírás módosítva"));
        QVERIFY(m_app->store()->load(a.id).contextNote.isEmpty());
        QVERIFY(m_shell->undoMeeting());
        QCOMPARE(undone.count(), 2);
        QVERIFY(m_app->participants(a.id).isEmpty());
        QTest::qWait(20);
        QCOMPARE(posted.count(), 2);
        QVERIFY(!m_shell->canUndoMeeting());
        QVERIFY(!m_shell->undoMeeting());

        // Switch: the other meeting's stack; a step on it is not "new".
        m_shell->setCurrentMeetingId(b.id);
        QVERIFY(m_shell->canUndoMeeting());
        QTest::qWait(20);
        QCOMPARE(posted.count(), 2);
        m_shell->setCurrentMeetingId(QString());
        QVERIFY(!m_shell->hasMeetingUndo());
    }

    void importOpensPickerThenDialog()
    {
        QSignalSpy requested(m_shell.get(), &ShellActions::importDialogRequested);
        // Backing out of the file picker: no window opens.
        m_shell->openImport();
        QCOMPARE(m_bridge->calls, QStringList{QStringLiteral("pickMany")});
        QCOMPARE(requested.size(), 0);
        // With files selected the window opens with them.
        m_bridge->pickedFiles = {QStringLiteral("/tmp/a.wav"), QStringLiteral("/tmp/b.mp3")};
        m_shell->openImport();
        QCOMPARE(requested.size(), 1);
        QCOMPARE(requested.last().at(0).toList().size(), 2);
        // With given (dropped) files there is no picker.
        m_bridge->calls.clear();
        m_shell->openImport({QStringLiteral("/tmp/c.flac")});
        QVERIFY(m_bridge->calls.isEmpty());
        QCOMPARE(requested.size(), 2);
        QCOMPARE(requested.last().at(0).toList(), QVariantList{QStringLiteral("/tmp/c.flac")});
        // Without a bridge (demo) the empty window opens.
        m_shell->setBridge(nullptr);
        m_shell->openImport();
        QCOMPARE(requested.size(), 3);
        QVERIFY(requested.last().at(0).toList().isEmpty());
        QCOMPARE(m_shell->pickAudioFiles(), QStringList());
    }

    void widgetsActionsGoThroughTheBridge()
    {
        m_shell->openSettings(QStringLiteral("watcher"));
        m_shell->openPeople();
        m_shell->openRecorder();
        QCOMPARE(m_shell->pickAudioFile(), QStringLiteral("/tmp/x.ogg"));
        QCOMPARE(m_bridge->calls, (QStringList{QStringLiteral("settings:watcher"), QStringLiteral("people"),
                                               QStringLiteral("recorder"), QStringLiteral("pick")}));
        // People with one person selected (deep link); with an empty name a plain open.
        m_shell->openPeople(QStringLiteral(" Bárány Gergely "));
        QCOMPARE(m_bridge->calls.last(), QStringLiteral("people:Bárány Gergely"));
        m_shell->openPeople(QStringLiteral("  "));
        QCOMPARE(m_bridge->calls.last(), QStringLiteral("people"));
        // Without a bridge (demo) it does not crash, it notifies.
        m_shell->setBridge(nullptr);
        m_shell->openSettings(QString());
        m_shell->openRecorder();
        QCOMPARE(m_shell->pickAudioFile(), QString());
        QCOMPARE(m_toasts.size(), 2);
    }

    void archiveExportAndImport()
    {
        const tanara::Meeting a = recording(QStringLiteral("Archív próba"));
        QSignalSpy toasts(m_shell.get(), &ShellActions::toastRequested);
        QSignalSpy finished(m_app.get(), &tanara::app::AppController::archiveFinished);

        // Backing out of the save window: nothing starts. The suggested name is "<folder name>.tanara.zip".
        m_shell->exportArchive(a.id);
        QCOMPARE(m_bridge->calls, QStringList{QStringLiteral("save")});
        QVERIFY(m_bridge->lastProposed.endsWith(tanara::library::MeetingArchive::suggestedFileName(a)));
        QVERIFY(m_bridge->lastFilter.contains(QStringLiteral("*.tanara.zip")));
        QVERIFY(!m_app->archiveBusy());

        // A name without an extension gets .tanara.zip; runs in the background, shows in the job bar.
        QVERIFY(QDir().mkpath(m_home->filePath(QStringLiteral("ki"))));
        m_bridge->savePath = m_home->filePath(QStringLiteral("ki/archiv"));
        m_shell->exportArchive(a.id);
        QVERIFY(m_app->jobs()->isRunning(a.id, tanara::JobKind::Export));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 20000);
        const QString zip = m_home->filePath(QStringLiteral("ki/archiv.tanara.zip"));
        QVERIFY(QFile::exists(zip));
        QCOMPARE(toasts.last().at(1).toString(), QString());
        QCOMPARE(toasts.last().at(5).toString(), zip);   // "Show in folder"
        // The next export suggests the latest folder.
        m_bridge->savePath.clear();
        m_shell->exportArchive(a.id);
        QCOMPARE(QFileInfo(m_bridge->lastProposed).absolutePath(), m_home->filePath(QStringLiteral("ki")));

        // Import: first the picker (backing out: nothing), then the selected archive.
        QVERIFY(m_app->store()->deleteMeeting(a.id));
        m_bridge->calls.clear();
        m_shell->importArchive(QString());
        QCOMPARE(m_bridge->calls, QStringList{QStringLiteral("pickArchive")});
        QVERIFY(!m_app->archiveBusy());
        m_bridge->archivePath = zip;
        m_shell->importArchive(QString());
        QVERIFY(m_app->archiveBusy());
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 2, 20000);
        QVERIFY(finished.last().at(1).toBool());
        QCOMPARE(m_shell->currentMeetingId(), a.id);
        QCOMPARE(m_shell->currentTab(), 0);
        QVERIFY(m_toasts.last().contains(QStringLiteral("Archív próba")));

        // Folder import (no zip): the existing meeting's folder is copied to an outside place,
        // deleted from the library, then adopted through the picker; the same end as with the archive.
        const QString outside = m_home->filePath(QStringLiteral("kulso"));
        QVERIFY(QDir().mkpath(outside));
        const QString srcFolder = m_app->store()->load(a.id).folder;
        const QString copied = QDir(outside).filePath(QFileInfo(srcFolder).fileName());
        QVERIFY(QDir().mkpath(copied));
        for (const QFileInfo& e : QDir(srcFolder).entryInfoList(QDir::Files))
            QVERIFY(QFile::copy(e.absoluteFilePath(), QDir(copied).filePath(e.fileName())));
        QVERIFY(m_app->store()->deleteMeeting(a.id));
        m_bridge->calls.clear();
        m_shell->importFolder(QString());
        QCOMPARE(m_bridge->calls, QStringList{QStringLiteral("pickFolder")});
        QVERIFY(!m_app->archiveBusy());
        m_bridge->folderPath = copied;
        m_shell->importFolder(QString());
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 3, 20000);
        QVERIFY(finished.last().at(1).toBool());
        QCOMPARE(m_shell->currentMeetingId(), a.id);
        QVERIFY(QFileInfo(copied).isDir());                                   // the source stays
        QVERIFY(QDir(m_app->store()->audioDir()).entryList({QStringLiteral(".import-*")}, QDir::Dirs | QDir::Hidden).isEmpty());
        QVERIFY(m_shell->isDirectory(copied));
        QVERIFY(!m_shell->isDirectory(QDir(copied).filePath(QStringLiteral("meeting.json"))));

        // A dropped file URL that is not an archive: an error toast, the selection stays.
        const QString bad = m_home->filePath(QStringLiteral("hibas.zip"));
        QFile f(bad);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("nem zip");
        f.close();
        QVERIFY(m_shell->isArchiveFile(QUrl::fromLocalFile(bad)));
        QVERIFY(m_shell->isArchiveFile(QStringLiteral("/x/Valami.TANARA.ZIP")));
        QVERIFY(!m_shell->isArchiveFile(QUrl::fromLocalFile(QStringLiteral("/x/hang.wav"))));
        m_shell->importArchive(QUrl::fromLocalFile(bad));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 4, 20000);
        QCOMPARE(toasts.last().at(1).toString(), QStringLiteral("danger"));
        QCOMPARE(m_shell->currentMeetingId(), a.id);
    }

    void tagsActions()
    {
        // The Tags window through the bridge, with an optional selection.
        m_shell->openTags();
        QCOMPARE(m_bridge->calls.last(), QStringLiteral("tags"));
        m_shell->openTags(QStringLiteral(" t-1 "));
        QCOMPARE(m_bridge->calls.last(), QStringLiteral("tags:t-1"));

        // Filter by tag: Main.qml puts it on the library model.
        QSignalSpy filter(m_shell.get(), &ShellActions::tagFilterRequested);
        m_shell->filterByTag(QStringLiteral("t-1"));
        m_shell->filterByTag(QString());           // empty: nothing
        QCOMPARE(filter.count(), 1);
        QCOMPARE(filter.first().at(0).toString(), QStringLiteral("t-1"));
        // From the Tags window (bridge): filter + raise the main window.
        QSignalSpy activate(m_shell.get(), &ShellActions::windowActivationRequested);
        emit m_bridge->tagFilterRequested(QStringLiteral("t-2"));
        QCOMPARE(filter.count(), 2);
        QCOMPARE(filter.last().at(0).toString(), QStringLiteral("t-2"));
        QCOMPARE(activate.count(), 1);

        // Toast with "Undo": the key is in the signal; the button emits undoRequested(key).
        QSignalSpy toasts(m_shell.get(), &ShellActions::toastRequested);
        QSignalSpy undo(m_shell.get(), &ShellActions::undoRequested);
        m_shell->toast(QStringLiteral("Javaslat elutasítva: #Nordvik"), QStringLiteral("tags"));
        m_shell->toast(QStringLiteral("Sima értesítés"));
        QCOMPARE(toasts.count(), 2);
        QCOMPARE(toasts.at(0).at(4).toString(), QStringLiteral("tags"));
        QCOMPARE(toasts.at(1).at(4).toString(), QString());
        m_shell->undoFromToast(QStringLiteral("tags"));
        m_shell->undoFromToast(QString());
        QCOMPARE(undo.count(), 1);
        QCOMPARE(undo.first().at(0).toString(), QStringLiteral("tags"));

        // Without a bridge (demo) Tags does not open, it notifies.
        m_shell->setBridge(nullptr);
        m_toasts.clear();
        m_shell->openTags();
        QCOMPARE(m_toasts.size(), 1);
    }

    void onboardingAction()
    {
        // File > "Getting started...": through the bridge (regardless of onboardingDone).
        m_shell->openOnboarding();
        QCOMPARE(m_bridge->calls.last(), QStringLiteral("onboarding"));
        // Without a bridge (demo) it does not open, it notifies.
        m_shell->setBridge(nullptr);
        m_toasts.clear();
        m_shell->openOnboarding();
        QCOMPARE(m_toasts.size(), 1);
    }

    void meetingModelTagsFollowSelection()
    {
        const tanara::Meeting a = transcribed(QStringLiteral("Nordvik egyeztetés"));
        const tanara::Meeting b = transcribed(QStringLiteral("Nordvik egyeztetés 2"));
        const tanara::Meeting plain = recording(QStringLiteral("Átirat nélkül"));
        tanara::library::TagService* tags = m_app->tags();
        tags->addTag(a.id, QStringLiteral("Nordvik"));

        ShellMeetingModel model(facadetest::shellOf(m_app.get()));
        QVERIFY(model.tags());
        QSignalSpy computing(m_app.get(), &tanara::app::AppController::tagSuggestionsComputing);

        // Showing a transcribed meeting: the tag row is bound to the meeting + a suggestion request.
        model.setMeetingId(a.id);
        QCOMPARE(model.tags()->meetingId(), a.id);
        QCOMPARE(model.tags()->tags().size(), 1);
        QCOMPARE(computing.count(), 1);
        QCOMPARE(computing.last().at(0).toString(), a.id);
        // The same selection refreshing (e.g. job progress) does not ask again.
        m_app->renameMeeting(a.id, QStringLiteral("Nordvik egyeztetés (átnevezve)"));
        QTRY_COMPARE(model.title(), QStringLiteral("Nordvik egyeztetés (átnevezve)"));
        QCOMPARE(computing.count(), 1);

        // Without a transcript it does not ask (step 1 suggests by the title).
        model.setMeetingId(plain.id);
        QCOMPARE(model.tags()->meetingId(), plain.id);
        QCOMPARE(computing.count(), 1);

        // A new transcript on the selected meeting: asks again.
        model.setMeetingId(b.id);
        QCOMPARE(computing.count(), 2);
        emit m_app->transcriptReady(b.id, QString());
        QCOMPARE(computing.count(), 3);
        QCOMPARE(computing.last().at(0).toString(), b.id);
        // Another meeting's transcript: no.
        emit m_app->transcriptReady(a.id, QString());
        QCOMPARE(computing.count(), 3);

        // The suggestion list arrives in the model (the "Nordvik" tag from the similar meeting).
        QTRY_VERIFY_WITH_TIMEOUT(!model.tags()->computing(), 10000);
    }

    void transcriptionIsGatedByReadiness()
    {
        configureProviders(/*withSttKey*/ false);
        const tanara::Meeting m = recording(QStringLiteral("Kulcs nélkül"));
        QSignalSpy started(m_app->jobs(), &tanara::library::MeetingJobTracker::jobStarted);
        QSignalSpy revisions(m_shell.get(), &ShellActions::readinessRevisionChanged);

        // A missing API key: Settings on the "External services" page.
        m_shell->startTranscription(m.id);
        QCOMPARE(started.count(), 0);
        QCOMPARE(m_bridge->calls, QStringList{QStringLiteral("settings:providers")});
        QCOMPARE(m_bridge->lastFocusField, QStringLiteral("stt"));   // the transcriber's card opens highlighted
        QVERIFY(revisions.count() >= 1);

        // A meeting with no audio track: nothing to configure, only a notice.
        m_bridge->calls.clear();
        const tanara::Meeting empty = m_app->store()->createMeeting(QStringLiteral("Üres"));
        m_shell->startTranscription(empty.id);
        QCOMPARE(started.count(), 0);
        QVERIFY(m_bridge->calls.isEmpty());
        QCOMPARE(m_toasts.size(), 1);
        QVERIFY(m_toasts.last().contains(QStringLiteral("Nem indítható")));

        // Starts with a configured provider (own key: no estimate window).
        m_bridge->calls.clear();
        m_app->setSecret(tanara::app::keys::SonioxApiKey, QStringLiteral("teszt-kulcs"));
        m_shell->startTranscription(m.id);
        QCOMPARE(started.count(), 1);
        QCOMPARE(started.at(0).at(0).toString(), m.id);
        QVERIFY(m_bridge->calls.isEmpty());
        m_shell->cancelJob(m.id, int(tanara::JobKind::Transcribe));
        m_app->cancelAllJobs(m.id);
        QTRY_VERIFY(!m_app->jobs()->isBusy(m.id));
    }

    void summaryActionsAreGated()
    {
        configureProviders(true);
        const tanara::Meeting noTranscript = recording(QStringLiteral("Átirat nélkül"));
        QSignalSpy started(m_app->jobs(), &tanara::library::MeetingJobTracker::jobStarted);

        m_shell->startQuickSummary(noTranscript.id);
        m_shell->startTopicExtraction(noTranscript.id);
        m_shell->startTopicAnalysis(noTranscript.id);
        m_shell->analyzeTopic(noTranscript.id, QStringLiteral("t1"));
        QCOMPARE(started.count(), 0);
        QCOMPARE(m_toasts.size(), 4);                 // "Cannot start: no transcript..."

        // With a transcript: the per-topic analysis does not start without a topic list.
        const tanara::Meeting m = transcribed(QStringLiteral("Átirattal"));
        m_toasts.clear();
        m_shell->startTopicAnalysis(m.id);
        QCOMPARE(started.count(), 0);
        QCOMPARE(m_toasts.size(), 1);
        m_shell->analyzeTopic(m.id, QStringLiteral("nincs-ilyen-téma"));
        QCOMPARE(m_toasts.size(), 2);

        // Quick summary starts (and we cancel at once; the address is a closed port).
        m_toasts.clear();
        m_shell->startQuickSummary(m.id);
        QVERIFY2(started.count() == 1, qPrintable(m_toasts.join(QLatin1Char('|'))));
        QCOMPARE(started.at(0).at(1).value<tanara::JobKind>(), tanara::JobKind::Summarize);
        m_app->cancelAllJobs(m.id);
        QTRY_VERIFY(!m_app->jobs()->isBusy(m.id));
    }

    void cloudProviderNeverRunsFromTheUi()
    {
        // Live cloud mode (developer setting) with the Tanara Cloud provider selected: the UI does
        // not offer Cloud ("coming soon"), so nothing starts; the user is sent to the own-key
        // provider settings.
        m_shell.reset();
        m_app.reset();
        qputenv("TANARA_CLOUD", "live");
        qputenv("TANARA_CLOUD_URL", "http://127.0.0.1:9");
        m_app = std::make_unique<tanara::app::AppController>();
        qputenv("TANARA_CLOUD", "off");
        qunsetenv("TANARA_CLOUD_URL");
        if (!m_app->cloudLive())
            QSKIP("A cloud-mód nincs befordítva ebbe a buildbe.");
        m_shell = std::make_unique<ShellActions>(facadetest::shellOf(m_app.get()));
        m_shell->setBridge(m_bridge.get());
        tanara::app::AppSettings s = m_app->settings()->settings();
        s.providers.stt.selectedId = tanara::cloud::ProviderId;
        s.providers.stt.configs[tanara::cloud::ProviderId].type = tanara::cloud::ProviderId;
        m_app->settings()->setSettings(s);
        const tanara::Meeting m = recording(QStringLiteral("Cloud"));
        QVERIFY(m_app->usesCloud(tanara::providers::WorkflowStep::Transcribe));
        QSignalSpy started(m_app->jobs(), &tanara::library::MeetingJobTracker::jobStarted);

        m_shell->startTranscription(m.id);
        QCOMPARE(started.count(), 0);
        QCOMPARE(m_bridge->calls, QStringList{QStringLiteral("settings:providers")});
        QCOMPARE(m_bridge->lastFocusField, QStringLiteral("stt"));

        // The re-transcription confirmation goes through the same gate.
        m_bridge->calls.clear();
        m_shell->confirmRetranscribe(m.id, true);
        QCOMPARE(started.count(), 0);
        QCOMPARE(m_bridge->calls, QStringList{QStringLiteral("settings:providers")});
    }

    void retranscribeAsksFirstAndReportsImpact()
    {
        configureProviders(true);
        const tanara::Meeting m = transcribed(QStringLiteral("Újra-átírandó"));
        QSignalSpy asked(m_shell.get(), &ShellActions::retranscribeDialogRequested);
        QSignalSpy started(m_app->jobs(), &tanara::library::MeetingJobTracker::jobStarted);

        m_shell->retranscribe(m.id);
        QCOMPARE(asked.count(), 1);
        QCOMPARE(started.count(), 0);                 // nothing starts without confirmation

        const QVariantMap impact = m_shell->retranscribeImpact(m.id);
        QCOMPARE(impact.value("corrections").toInt(), 0);
        QVERIFY(impact.value("text").toString().contains(QStringLiteral("hanglenyomat")));

        m_shell->confirmRetranscribe(m.id, /*keepBackup*/ true);
        QCOMPARE(started.count(), 1);
        m_app->cancelAllJobs(m.id);
        QTRY_VERIFY(!m_app->jobs()->isBusy(m.id));
    }

    // "Retry" after a cloud error: starts only for the meeting belonging to the error that is
    // STILL selected, and goes through the gate + cost estimate the same way as the first start.
    void retryTargetsTheFailedMeetingAndGoesThroughTheGate()
    {
        configureProviders(true);
        const tanara::Meeting a = recording(QStringLiteral("Hibázott"));
        const tanara::Meeting b = transcribed(QStringLiteral("Másik"));
        QSignalSpy started(m_app->jobs(), &tanara::library::MeetingJobTracker::jobStarted);
        QSignalSpy asked(m_shell.get(), &ShellActions::retranscribeDialogRequested);

        // Meanwhile another meeting was selected: nothing starts, a notice appears.
        m_shell->showMeeting(b.id);
        emit m_bridge->retryRequested(a.id, QStringLiteral("transcribe"));
        QCOMPARE(started.count(), 0);
        QVERIFY(!m_toasts.isEmpty() && m_toasts.last().contains(QStringLiteral("Hibázott")));

        // For a meeting with a transcript, repeating "transcribe" is a re-transcription: a
        // confirmation window appears, a plain transcription does not start silently.
        emit m_bridge->retryRequested(b.id, QStringLiteral("transcribe"));
        QCOMPARE(asked.count(), 1);
        QCOMPARE(asked.last().at(0).toString(), b.id);
        QCOMPARE(started.count(), 0);

        // For the selected meeting with no transcript, transcription starts (through the gate).
        m_shell->showMeeting(a.id);
        emit m_bridge->retryRequested(a.id, QStringLiteral("transcribe"));
        QCOMPARE(started.count(), 1);
        QCOMPARE(started.last().at(0).toString(), a.id);
        m_app->cancelAllJobs(a.id);
        QTRY_VERIFY(!m_app->jobs()->isBusy(a.id));

        // Nothing for an unknown / empty id.
        emit m_bridge->retryRequested(QString(), QStringLiteral("transcribe"));
        emit m_bridge->retryRequested(QStringLiteral("nincs-ilyen"), QStringLiteral("summary"));
        QCOMPARE(started.count(), 1);
    }

    // After the transcript the identify button always gives a visible answer: if it cannot start, it says why.
    void identifyAfterTranscriptExplainsWhyItDidNotStart()
    {
        tanara::Meeting m = transcribed(QStringLiteral("Azonosítandó"));
        m_shell->identifyParticipants(m.id);
        QCOMPARE(m_toasts.size(), 1);
        if (!m_app->voiceIdentificationAvailable())
            QVERIFY(m_toasts.last().contains(QStringLiteral("hangmodell")));
        m_toasts.clear();
        m_shell->identifyParticipants(m.id);
        QCOMPARE(m_toasts.size(), 1);                  // every click gets an answer
    }

    // Everyone named: the identify button offers the recheck (with confirmation).
    void identifyWithEveryoneNamedOffersRecheck()
    {
        const tanara::Meeting m = namedWithVoices(QStringLiteral("Mindenki megvan"));
        tanara::editing::SpeakerEditor* ed = m_app->speakerEditor(m.id);
        QVERIFY(ed);
        QStringList shown;
        bool answer = true;
        connect(m_shell.get(), &ShellActions::confirmRequested, this,
                [&](const QString& title, const QString& text, const QString& label, bool danger) {
            shown << title + QLatin1Char('|') + label + (danger ? QStringLiteral("|danger") : QString());
            QVERIFY(text.contains(QStringLiteral("az Átnézendő szűrőbe teszem")));
            QTimer::singleShot(10, m_shell.get(), [&] { m_shell->resolveConfirm(answer); });
        });

        // No confirmed line yet: no dialog, the toast says what is needed.
        m_shell->identifyParticipants(m.id);
        QVERIFY(shown.isEmpty());
        QCOMPARE(m_toasts.size(), 1);
        QVERIFY(m_toasts.last().contains(QStringLiteral("Előbb erősíts meg vagy javíts legalább 3 sort egy beszélőnél")));

        // Three real lines of Anna confirmed: dialog, 3 doubtful lines.
        QVERIFY(ed->confirmUtterances({QStringLiteral("u0"), QStringLiteral("u10000"), QStringLiteral("u25000")}));
        m_shell->setCurrentMeetingId(m.id);
        m_shell->showTab(2);
        m_shell->identifyParticipants(m.id);
        QCOMPARE(shown, QStringList{QStringLiteral("Mindenki azonosítva|Újraellenőrzés")});
        // The toast says what it relied on (confirmed lines, stored samples, model).
        QVERIFY2(m_toasts.last().startsWith(QStringLiteral("3 kétséges sort jelöltem meg — az Átnézendő szűrőben találod. "
                                                           "Alapja: 3 megerősített sor · tárolt minta nélkül")),
                 qPrintable(m_toasts.last()));
        QCOMPARE(ed->uncertainUtteranceIds(),
                 (QStringList{QStringLiteral("u15000"), QStringLiteral("u30000"), QStringLiteral("u45000")}));
        QCOMPARE(m_shell->currentTab(), 1);              // switches to the Transcript tab

        // Rejected: nothing happens (no new toast).
        answer = false;
        const int toasts = m_toasts.size();
        m_shell->recheckSpeakers(m.id);
        QCOMPARE(shown.last(), QStringLiteral("Beszélők újraellenőrzése|Újraellenőrzés"));
        QCOMPARE(m_toasts.size(), toasts);

        // The doubtful lines are decided: another recheck finds nothing.
        answer = true;
        QVERIFY(ed->confirmUtterances(ed->uncertainUtteranceIds()));
        m_shell->recheckSpeakers(m.id);
        QVERIFY2(m_toasts.last().startsWith(QStringLiteral("A megerősített sorok alapján nem találtam kétséges sort. Alapja: ")),
                 qPrintable(m_toasts.last()));
    }

    void identifyBeforeTranscriptUsesThePreview()
    {
        const tanara::Meeting m = recording(QStringLiteral("Előnézet"));
        QSignalSpy guessed(m_shell.get(), &ShellActions::participantsGuessed);
        m_shell->identifyParticipants(m.id);
        QCOMPARE(m_bridge->calls, QStringList{QStringLiteral("preview")});
        QCOMPARE(guessed.count(), 1);
        QCOMPARE(m_shell->participantsGuess(m.id), m_bridge->previewSummary);
        QVERIFY(m_toasts.last().contains(QStringLiteral("Kovács Lilla")));

        // Canceled: no result remains, a notice appears.
        const tanara::Meeting other = recording(QStringLiteral("Megszakított"));
        m_bridge->previewCancelled = true;
        m_shell->identifyParticipants(other.id);
        QCOMPARE(guessed.count(), 1);
        QCOMPARE(m_shell->participantsGuess(other.id), QString());
    }

    void confirmWaitsForTheDialog()
    {
        // Nobody to show it: false at once (it does not hang).
        QVERIFY(!m_shell->confirm(QStringLiteral("Cím"), QStringLiteral("Szöveg"), QStringLiteral("Igen"), true));

        QStringList shown;
        bool answer = true;
        connect(m_shell.get(), &ShellActions::confirmRequested, this,
                [&](const QString& title, const QString&, const QString& label, bool danger) {
            shown << title + QLatin1Char('|') + label + (danger ? QStringLiteral("|danger") : QString());
            // The QML window answers later (the user clicks).
            QTimer::singleShot(20, m_shell.get(), [&] { m_shell->resolveConfirm(answer); });
        });
        QVERIFY(m_shell->confirm(QStringLiteral("Törlöd?"), QStringLiteral("…"), QStringLiteral("Törlés"), true));
        answer = false;
        QVERIFY(!m_shell->confirm(QStringLiteral("Biztos?"), QStringLiteral("…"), QStringLiteral("Igen"), false));
        QCOMPARE(shown, (QStringList{QStringLiteral("Törlöd?|Törlés|danger"), QStringLiteral("Biztos?|Igen")}));
    }

    void renameDeleteAndRecordingFinished()
    {
        const tanara::Meeting a = recording(QStringLiteral("Régi cím"));
        const tanara::Meeting b = recording(QStringLiteral("Másik"));
        m_shell->showMeeting(a.id);

        m_shell->renameMeeting(a.id, QStringLiteral("  Új cím  "));
        QCOMPARE(m_app->store()->load(a.id).title, QStringLiteral("Új cím"));
        m_shell->renameMeeting(a.id, QStringLiteral("   "));        // empty: stays
        QCOMPARE(m_app->store()->load(a.id).title, QStringLiteral("Új cím"));

        // Delete: first asks for confirmation (with the title), after confirmation it deletes and the selection ends.
        QSignalSpy asked(m_shell.get(), &ShellActions::deleteDialogRequested);
        m_shell->requestDelete(a.id);
        QCOMPARE(asked.count(), 1);
        QCOMPARE(asked.at(0).at(1).toString(), QStringLiteral("Új cím"));
        QVERIFY(m_shell->meetingExists(a.id));
        const QString folder = m_app->store()->load(a.id).folder;
        m_shell->deleteMeeting(a.id);
        QVERIFY(!m_shell->meetingExists(a.id));
        QVERIFY(!QDir(folder).exists());
        QCOMPARE(m_shell->currentMeetingId(), QString());
        QVERIFY(m_shell->meetingExists(b.id));

        // A new recording finished: it is selected and a notice appears.
        m_toasts.clear();
        emit m_app->recordingFinished(m_app->store()->load(b.id));
        QCOMPARE(m_shell->currentMeetingId(), b.id);
        QCOMPARE(m_toasts.size(), 1);

        // The recorder's "Open in the analyzer" button / `tanara --meeting <id>` (the bridge's signal):
        // selects + raises the main window; for an unknown id the selection stays.
        m_shell->setCurrentMeetingId(QString());
        QSignalSpy activated(m_shell.get(), &ShellActions::windowActivationRequested);
        emit m_bridge->showMeetingRequested(b.id);
        QCOMPARE(m_shell->currentMeetingId(), b.id);
        QCOMPARE(activated.count(), 1);
        emit m_bridge->showMeetingRequested(QStringLiteral("nincs-ilyen"));
        QCOMPARE(m_shell->currentMeetingId(), b.id);
        QCOMPARE(activated.count(), 2);

        // Errors appear as notices, not in a modal window.
        QSignalSpy toasts(m_shell.get(), &ShellActions::toastRequested);
        emit m_app->errorOccurred(QStringLiteral("Nincs rögzíthető hangeszköz."));
        QCOMPARE(toasts.count(), 1);
        QCOMPARE(toasts.at(0).at(1).toString(), QStringLiteral("danger"));
        QVERIFY(!m_shell->recording());

        // The open meeting's job error shows in the error bar: the same text does not come again in a
        // toast (another meeting's does).
        m_shell->setCurrentMeetingId(b.id);
        tanara::JobError je;
        je.message = QStringLiteral("A modell 4096 tokenes kontextussal van betöltve, a kérés 6042 token volt — nem fér bele.");
        m_app->jobs()->begin(b.id, tanara::JobKind::Summarize, QStringLiteral("Összefoglaló"));
        m_app->jobs()->fail(b.id, tanara::JobKind::Summarize, je);
        emit m_app->errorOccurred(QStringLiteral("Összefoglaló hiba: ") + je.message);
        QCOMPARE(toasts.count(), 1);
        QTest::qWait(10);
        const tanara::Meeting c = recording(QStringLiteral("Harmadik"));
        m_app->jobs()->begin(c.id, tanara::JobKind::Summarize, QStringLiteral("Összefoglaló"));
        m_app->jobs()->fail(c.id, tanara::JobKind::Summarize, je);
        emit m_app->errorOccurred(QStringLiteral("Összefoglaló hiba: ") + je.message);
        QCOMPARE(toasts.count(), 2);
    }

    void meetingModelDescribesHeaderAndTasks()
    {
        configureProviders(true);
        const tanara::Meeting plain = recording(QStringLiteral("Csak felvétel"));
        const tanara::Meeting done = transcribed(QStringLiteral("Átírt megbeszélés"));

        ShellMeetingModel model(facadetest::shellOf(m_app.get()));
        QVERIFY(!model.exists());
        model.setMeetingId(plain.id);
        QVERIFY(model.exists());
        QVERIFY(!model.hasTranscript());
        QVERIFY(model.canIdentify());
        QCOMPARE(model.title(), QStringLiteral("Csak felvétel"));
        QVERIFY2(model.metaText().endsWith(QStringLiteral("· 1:16:04 · 1 sáv")), qPrintable(model.metaText()));

        model.setMeetingId(done.id);
        QVERIFY(model.hasTranscript());
        QVERIFY2(model.metaText().endsWith(QStringLiteral("· 1:16:04 · 2 beszélő")), qPrintable(model.metaText()));
        QVERIFY(!model.summaryStale());
        QVERIFY(model.tasks().isEmpty());

        // Rename: the header follows.
        QSignalSpy changed(&model, &ShellMeetingModel::changed);
        m_app->renameMeeting(done.id, QStringLiteral("Új név"));
        QTRY_COMPARE(model.title(), QStringLiteral("Új név"));
        QVERIFY(changed.count() >= 1);

        // A running job appears in the bar; the FIRST transcription does not (the pre-transcript view shows it).
        m_app->jobs()->begin(done.id, tanara::JobKind::Mixdown, QStringLiteral("Lekeverés"));
        m_app->jobs()->setPercent(done.id, tanara::JobKind::Mixdown, 64);
        QCOMPARE(model.tasks().size(), 1);
        const QVariantMap task = model.tasks().at(0).toMap();
        QCOMPARE(task.value("kind").toInt(), int(tanara::JobKind::Mixdown));
        QCOMPARE(task.value("percent").toInt(), 64);
        QCOMPARE(task.value("title").toString(), QStringLiteral("Lekeverés…"));
        m_app->jobs()->finish(done.id, tanara::JobKind::Mixdown);
        QVERIFY(model.tasks().isEmpty());

        model.setMeetingId(plain.id);
        m_app->jobs()->begin(plain.id, tanara::JobKind::Transcribe, QStringLiteral("Átírás folyamatban"));
        QVERIFY(model.tasks().isEmpty());
        m_app->jobs()->cancelled(plain.id, tanara::JobKind::Transcribe);

        // A deleted meeting does not exist.
        m_app->deleteMeeting(plain.id);
        QTRY_VERIFY(!model.exists());
    }

    void describeJobFormatsProgress()
    {
        tanara::JobProgress identify;
        identify.meetingId = QStringLiteral("m");
        identify.kind = tanara::JobKind::Identify;
        identify.title = QStringLiteral("Résztvevők azonosítása");
        identify.done = 3;
        identify.total = 5;
        QVariantMap t = ShellMeetingModel::describeJob(identify);
        QCOMPARE(t.value("detail").toString(), QStringLiteral("3 / 5 beszélő"));
        QCOMPARE(t.value("percent").toInt(), 60);
        QCOMPARE(t.value("iconName").toString(), QStringLiteral("fingerprint"));
        QVERIFY(t.value("cancellable").toBool());

        // A stage without a percentage: an indeterminate bar (percent -1), with the running stage's name.
        tanara::JobProgress transcribe;
        transcribe.meetingId = QStringLiteral("m");
        transcribe.kind = tanara::JobKind::Transcribe;
        tanara::JobStage upload{QStringLiteral("upload"), QStringLiteral("Feltöltés"), tanara::StageState::Done, 100, {}};
        tanara::JobStage run{QStringLiteral("transcribe"), QStringLiteral("Átírás"), tanara::StageState::Running, -1, {}};
        transcribe.stages = {upload, run};
        transcribe.cancelling = true;
        t = ShellMeetingModel::describeJob(transcribe);
        QCOMPARE(t.value("percent").toInt(), -1);
        QCOMPARE(t.value("detail").toString(), QStringLiteral("Átírás"));
        QCOMPARE(t.value("title").toString(), QStringLiteral("Átírás…"));
        QVERIFY(t.value("cancelling").toBool());
        QCOMPARE(t.value("eta").toString(), QString());   // without an estimate there is no made-up time
    }

    void uiStatePersistsOnlyToItsFile()
    {
        const QString path = m_home->filePath(QStringLiteral("ui-state.json"));
        {
            ShellUiState state;
            state.setFilePath(path);
            QCOMPARE(state.value(QStringLiteral("selectedMeetingId"), QStringLiteral("x")).toString(),
                     QStringLiteral("x"));
            state.setValue(QStringLiteral("selectedMeetingId"), QStringLiteral("abc"));
            state.setValue(QStringLiteral("window"),
                           QVariantMap{{QStringLiteral("w"), 1400}, {QStringLiteral("maximized"), true}});
            QVERIFY(!QFile::exists(path));      // writes delayed
            state.flush();
            QVERIFY(QFile::exists(path));
        }
        ShellUiState again;
        again.setFilePath(path);
        QCOMPARE(again.value(QStringLiteral("selectedMeetingId")).toString(), QStringLiteral("abc"));
        QCOMPARE(again.value(QStringLiteral("window")).toMap().value(QStringLiteral("w")).toInt(), 1400);

        // Without a path (demo) it lives only in memory.
        ShellUiState memory;
        memory.setValue(QStringLiteral("k"), 1);
        memory.flush();
        QCOMPARE(memory.value(QStringLiteral("k")).toInt(), 1);
        QVERIFY(memory.filePath().isEmpty());
    }
};

QTEST_MAIN(TestShellActions)
#include "test_shell_actions.moc"
