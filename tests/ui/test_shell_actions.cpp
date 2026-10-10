// ShellActions + ShellMeetingModel + ShellUiState — a héj művelet- és állapot-rétege.
// Valódi AppController, IZOLÁLT TANARA_HOME (QTemporaryDir), ál-híddal (a Widgets-ablakok
// helyén): a kapuzás útja — futtathatóság → cloud-akadály →
// Beállítások a megfelelő lapon → indítás. Valódi szolgáltató-hívás NINCS (a feladatot az
// indulás után rögtön megszakítjuk; a szolgáltató címe egy zárt helyi port).
#include "AppContext.h"
#include "PlayerController.h"
#include "ShellActions.h"
#include "ShellBridge.h"
#include "ShellMeetingModel.h"
#include "ShellUiState.h"

#include "tanara/AppController.h"
#include "tanara/SettingsManager.h"
#include "tanara/cloud/CloudTypes.h"
#include "tanara/edit/SpeakerEditor.h"
#include "tanara/edit/SpeakerOverlay.h"
#include "tanara/edit/UtteranceEmbeddings.h"
#include "tanara/jobs/MeetingJobTracker.h"
#include "tanara/store/MeetingArchive.h"
#include "tanara/store/MeetingStore.h"
#include "tanara/tags/TagService.h"

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
    bool handleBlockers = false;      // true: a cloud-akadályt „kezeli”
    bool estimateAnswer = true;
    QString previewSummary = QStringLiteral("Kovács Lilla és 1 ismeretlen partner");
    bool previewCancelled = false;

    bool cloudChipVisible() const override { return false; }
    QString cloudChipText() const override { return {}; }
    QString cloudChipTone() const override { return QStringLiteral("normal"); }
    QString cloudChipToolTip() const override { return {}; }
    QVariantList cloudBanners() const override { return {}; }

    void openSettings(const QString& page) override { calls << QStringLiteral("settings:") + page; }
    // A mély hivatkozás (B04) mezője; a lap ugyanúgy naplózódik.
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
    bool handleCloudBlocker(const tanara::ReadinessResult& r) override
    {
        calls << QStringLiteral("blocker:%1:%2").arg(int(r.blockerKind)).arg(r.providerId);
        return handleBlockers;
    }
    bool confirmCloudEstimate(const QString&, const QString& task, const QString& mode) override
    {
        calls << QStringLiteral("estimate:%1:%2").arg(task, mode);
        return estimateAnswer;
    }
    QString identifyParticipantsPreview(const QString&, bool* cancelled) override
    {
        calls << QStringLiteral("preview");
        if (cancelled) *cancelled = previewCancelled;
        return previewCancelled ? QString() : previewSummary;
    }
    void cloudBannerAction(const QString&) override {}
    void cloudBannerDismiss(const QString&) override {}
    void continueRecordingInBackground() override {}
    void stopRecordingAndQuit() override {}
    void shutdown() override {}
    void windowShown() override {}
    void windowActivated() override {}
    void openUsageLog() override {}
};

} // namespace

class TestShellActions : public QObject {
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_home;
    std::unique_ptr<tanara::AppController> m_app;
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
        // A feldolgozás a tokenekből dolgozik (az összefoglaló üres token-listára nem indul).
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
    // Átírt, MINDEN beszélőjében elnevezett megbeszélés kitalált hang-embeddingekkel (a cache
    // közvetlenül íródik — nincs modell, nincs hang): Anna címkéje alá 3 Béla-hangú sor került.
    tanara::Meeting namedWithVoices(const QString& title)
    {
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
        QFile seg(tanara::speakeredit::segmentsPath(m.folder));
        if (seg.open(QIODevice::WriteOnly)) seg.write(QJsonDocument(segs).toJson());
        seg.close();
        const QVector<tanara::TranscriptLine> lines = tanara::speakeredit::loadTranscriptLines(m.folder);
        tanara::UtteranceEmbeddingCache cache;
        cache.fingerprint = tanara::speakeredit::transcriptFingerprint(lines);
        for (int i = 0; i < rows.size(); ++i)
            cache.set(QStringLiteral("campplus"), lines[i].id, rows[i].voice == 'A' ? QVector<float>{1, 0, 0}
                                            : rows[i].voice == 'B' ? QVector<float>{0, 1, 0}
                                                                   : QVector<float>{0.6f, 0.8f, 0});
        cache.save(m.folder);
        m.hasTranscript = true;
        m.speakerMap = {{QStringLiteral("Beszélő 1"), QStringLiteral("Anna")},
                        {QStringLiteral("Beszélő 2"), QStringLiteral("Béla")}};
        m_app->store()->saveMeeting(m);
        return m;
    }
    void configureProviders(bool withSttKey)
    {
        tanara::AppSettings s = m_app->settings()->settings();
        s.sttProviderId = QStringLiteral("soniox");
        s.sttConfigs[s.sttProviderId].baseUrl = QStringLiteral("http://127.0.0.1:9/v1");   // zárt port
        s.llmProviderId = QStringLiteral("openai-compat");
        s.llmConfigs[s.llmProviderId].baseUrl = QStringLiteral("http://127.0.0.1:9/v1");
        s.llmConfigs[s.llmProviderId].model = QStringLiteral("teszt-modell");
        m_app->settings()->setSettings(s);
        if (withSttKey)
            m_app->setSecret(tanara::keys::SonioxApiKey, QStringLiteral("teszt-kulcs"));
    }

private slots:
    void initTestCase() { qputenv("TANARA_CLOUD", "off"); }

    void init()
    {
        m_home = std::make_unique<QTemporaryDir>();
        QVERIFY(m_home->isValid());
        qputenv("TANARA_HOME", m_home->path().toUtf8());
        AppContext::instance()->setDemo(false);
        m_app = std::make_unique<tanara::AppController>();
        QVERIFY(m_app->store()->audioDir().startsWith(m_home->path()));
        m_bridge = std::make_unique<FakeBridge>();
        m_shell = std::make_unique<ShellActions>();
        m_shell->setController(m_app.get());
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
        QCOMPARE(m_shell->currentTab(), 3);      // határon belül marad (0 Áttekintés … 3 Memó)

        // seekTo: kijelöl, az Átirat fülre (1) vált, a lejátszót tekeri, az Editornak jelez.
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

    void importOpensPickerThenDialog()
    {
        QSignalSpy requested(m_shell.get(), &ShellActions::importDialogRequested);
        // Visszalépés a fájlválasztóból: nem nyílik ablak.
        m_shell->openImport();
        QCOMPARE(m_bridge->calls, QStringList{QStringLiteral("pickMany")});
        QCOMPARE(requested.size(), 0);
        // Kiválasztott fájlokkal az ablak azokkal nyílik.
        m_bridge->pickedFiles = {QStringLiteral("/tmp/a.wav"), QStringLiteral("/tmp/b.mp3")};
        m_shell->openImport();
        QCOMPARE(requested.size(), 1);
        QCOMPARE(requested.last().at(0).toList().size(), 2);
        // Megadott (ejtett) fájlokkal nincs választó.
        m_bridge->calls.clear();
        m_shell->openImport({QStringLiteral("/tmp/c.flac")});
        QVERIFY(m_bridge->calls.isEmpty());
        QCOMPARE(requested.size(), 2);
        QCOMPARE(requested.last().at(0).toList(), QVariantList{QStringLiteral("/tmp/c.flac")});
        // Híd nélkül (demó) az üres ablak nyílik.
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
        // Személyek egy személy kijelölésével (mély hivatkozás); üres névvel a sima megnyitás.
        m_shell->openPeople(QStringLiteral(" Bárány Gergely "));
        QCOMPARE(m_bridge->calls.last(), QStringLiteral("people:Bárány Gergely"));
        m_shell->openPeople(QStringLiteral("  "));
        QCOMPARE(m_bridge->calls.last(), QStringLiteral("people"));
        // Híd nélkül (demó) nem omlik össze, értesít.
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
        QSignalSpy finished(m_app.get(), &tanara::AppController::archiveFinished);

        // Visszalépés a mentés-ablakból: nem indul semmi. A javasolt név „<mappanév>.tanara.zip”.
        m_shell->exportArchive(a.id);
        QCOMPARE(m_bridge->calls, QStringList{QStringLiteral("save")});
        QVERIFY(m_bridge->lastProposed.endsWith(tanara::MeetingArchive::suggestedFileName(a)));
        QVERIFY(m_bridge->lastFilter.contains(QStringLiteral("*.tanara.zip")));
        QVERIFY(!m_app->archiveBusy());

        // Kiterjesztés nélküli név → .tanara.zip; háttérben fut, a feladat-sávban látszik.
        QVERIFY(QDir().mkpath(m_home->filePath(QStringLiteral("ki"))));
        m_bridge->savePath = m_home->filePath(QStringLiteral("ki/archiv"));
        m_shell->exportArchive(a.id);
        QVERIFY(m_app->jobs()->isRunning(a.id, tanara::JobKind::Export));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 20000);
        const QString zip = m_home->filePath(QStringLiteral("ki/archiv.tanara.zip"));
        QVERIFY(QFile::exists(zip));
        QCOMPARE(toasts.last().at(1).toString(), QString());
        QCOMPARE(toasts.last().at(5).toString(), zip);   // „Megnyitás mappában”
        // A következő export a legutóbbi mappát javasolja.
        m_bridge->savePath.clear();
        m_shell->exportArchive(a.id);
        QCOMPARE(QFileInfo(m_bridge->lastProposed).absolutePath(), m_home->filePath(QStringLiteral("ki")));

        // Import: előbb a választó (visszalépés: semmi), majd a kiválasztott archívum.
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

        // Mappa-import (zip nélkül): a meglévő megbeszélés mappáját kimásoljuk egy külső helyre,
        // töröljük a könyvtárból, majd a választóból behúzzuk — ugyanaz a vég, mint az archívumnál.
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
        QVERIFY(QFileInfo(copied).isDir());                                   // a forrás megmarad
        QVERIFY(QDir(m_app->store()->audioDir()).entryList({QStringLiteral(".import-*")}, QDir::Dirs | QDir::Hidden).isEmpty());
        QVERIFY(m_shell->isDirectory(copied));
        QVERIFY(!m_shell->isDirectory(QDir(copied).filePath(QStringLiteral("meeting.json"))));

        // Ejtett fájl-URL, ami nem archívum: hiba-toast, a kijelölés marad.
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
        // A Címkék ablaka a hídon át, opcionális kijelöléssel.
        m_shell->openTags();
        QCOMPARE(m_bridge->calls.last(), QStringLiteral("tags"));
        m_shell->openTags(QStringLiteral(" t-1 "));
        QCOMPARE(m_bridge->calls.last(), QStringLiteral("tags:t-1"));

        // Szűrés címkére: a Main.qml a könyvtár-modellre teszi.
        QSignalSpy filter(m_shell.get(), &ShellActions::tagFilterRequested);
        m_shell->filterByTag(QStringLiteral("t-1"));
        m_shell->filterByTag(QString());           // üres: semmi
        QCOMPARE(filter.count(), 1);
        QCOMPARE(filter.first().at(0).toString(), QStringLiteral("t-1"));
        // A Címkék ablakából (híd): szűrés + a főablak előre.
        QSignalSpy activate(m_shell.get(), &ShellActions::windowActivationRequested);
        emit m_bridge->tagFilterRequested(QStringLiteral("t-2"));
        QCOMPARE(filter.count(), 2);
        QCOMPARE(filter.last().at(0).toString(), QStringLiteral("t-2"));
        QCOMPARE(activate.count(), 1);

        // Toast „Visszavonás”-sal: a kulcs a jelben; a gomb undoRequested(kulcs)-ot ad.
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

        // Híd nélkül (demó) a Címkék nem nyílik, értesít.
        m_shell->setBridge(nullptr);
        m_toasts.clear();
        m_shell->openTags();
        QCOMPARE(m_toasts.size(), 1);
    }

    void onboardingAction()
    {
        // Fájl › „Első lépések…”: a hídon át (az onboardingDone-tól függetlenül).
        m_shell->openOnboarding();
        QCOMPARE(m_bridge->calls.last(), QStringLiteral("onboarding"));
        // Híd nélkül (demó) nem nyílik, értesít.
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
        tanara::TagService* tags = m_app->tags();
        tags->addTag(a.id, QStringLiteral("Nordvik"));

        ShellMeetingModel model;
        model.setController(m_app.get());
        QVERIFY(model.tags());
        QSignalSpy computing(m_app.get(), &tanara::AppController::tagSuggestionsComputing);

        // Átírt megbeszélés megjelenítése → a címkesor a megbeszéléshez kötve + javaslat-kérés.
        model.setMeetingId(a.id);
        QCOMPARE(model.tags()->meetingId(), a.id);
        QCOMPARE(model.tags()->tags().size(), 1);
        QCOMPARE(computing.count(), 1);
        QCOMPARE(computing.last().at(0).toString(), a.id);
        // Ugyanaz a kijelölés frissülése (pl. feladat-haladás) nem kér újra.
        m_app->renameMeeting(a.id, QStringLiteral("Nordvik egyeztetés (átnevezve)"));
        QTRY_COMPARE(model.title(), QStringLiteral("Nordvik egyeztetés (átnevezve)"));
        QCOMPARE(computing.count(), 1);

        // Átirat nélkül nem kér (az 1. lépés a cím alapján javasol).
        model.setMeetingId(plain.id);
        QCOMPARE(model.tags()->meetingId(), plain.id);
        QCOMPARE(computing.count(), 1);

        // Új átirat a kijelölt megbeszélésen → újra kér.
        model.setMeetingId(b.id);
        QCOMPARE(computing.count(), 2);
        emit m_app->transcriptReady(b.id, QString());
        QCOMPARE(computing.count(), 3);
        QCOMPARE(computing.last().at(0).toString(), b.id);
        // Másik megbeszélés átirata: nem.
        emit m_app->transcriptReady(a.id, QString());
        QCOMPARE(computing.count(), 3);

        // A javaslat-lista megérkezik a modellbe (a „Nordvik” címke a hasonló megbeszélésről).
        QTRY_VERIFY_WITH_TIMEOUT(!model.tags()->computing(), 10000);
    }

    void transcriptionIsGatedByReadiness()
    {
        configureProviders(/*withSttKey*/ false);
        const tanara::Meeting m = recording(QStringLiteral("Kulcs nélkül"));
        QSignalSpy started(m_app->jobs(), &tanara::MeetingJobTracker::jobStarted);
        QSignalSpy revisions(m_shell.get(), &ShellActions::readinessRevisionChanged);

        // Hiányzó API-kulcs → nem cloud-akadály → Beállítások a „Külső szolgáltatások” lapon.
        m_shell->startTranscription(m.id);
        QCOMPARE(started.count(), 0);
        QCOMPARE(m_bridge->calls.size(), 2);
        QVERIFY(m_bridge->calls.at(0).startsWith(QStringLiteral("blocker:")));
        QCOMPARE(m_bridge->calls.at(1), QStringLiteral("settings:providers"));
        QCOMPARE(m_bridge->lastFocusField, QStringLiteral("stt"));   // az átíró kártyája kiemelve nyílik
        QVERIFY(revisions.count() >= 1);

        // Hangsáv nélküli megbeszélés → nincs mit beállítani, csak értesítés.
        m_bridge->calls.clear();
        const tanara::Meeting empty = m_app->store()->createMeeting(QStringLiteral("Üres"));
        m_shell->startTranscription(empty.id);
        QCOMPARE(started.count(), 0);
        QCOMPARE(m_bridge->calls.size(), 1);          // csak a cloud-akadály kérdés
        QCOMPARE(m_toasts.size(), 1);
        QVERIFY(m_toasts.last().contains(QStringLiteral("Nem indítható")));

        // Beállított szolgáltatóval indul (saját kulcs → nincs becslés-ablak).
        m_bridge->calls.clear();
        m_app->setSecret(tanara::keys::SonioxApiKey, QStringLiteral("teszt-kulcs"));
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
        QSignalSpy started(m_app->jobs(), &tanara::MeetingJobTracker::jobStarted);

        m_shell->startQuickSummary(noTranscript.id);
        m_shell->startTopicExtraction(noTranscript.id);
        m_shell->startTopicAnalysis(noTranscript.id);
        m_shell->analyzeTopic(noTranscript.id, QStringLiteral("t1"));
        QCOMPARE(started.count(), 0);
        QCOMPARE(m_toasts.size(), 4);                 // „Nem indítható: nincs átirat…”

        // Átirattal: a témánkénti elemzés téma-lista nélkül nem indul.
        const tanara::Meeting m = transcribed(QStringLiteral("Átirattal"));
        m_toasts.clear();
        m_shell->startTopicAnalysis(m.id);
        QCOMPARE(started.count(), 0);
        QCOMPARE(m_toasts.size(), 1);
        m_shell->analyzeTopic(m.id, QStringLiteral("nincs-ilyen-téma"));
        QCOMPARE(m_toasts.size(), 2);

        // Gyors összefoglaló indul (és rögtön megszakítjuk — a cím egy zárt port).
        m_toasts.clear();
        m_shell->startQuickSummary(m.id);
        QVERIFY2(started.count() == 1, qPrintable(m_toasts.join(QLatin1Char('|'))));
        QCOMPARE(started.at(0).at(1).value<tanara::JobKind>(), tanara::JobKind::Summarize);
        m_app->cancelAllJobs(m.id);
        QTRY_VERIFY(!m_app->jobs()->isBusy(m.id));
    }

    void cloudBlockerIsHandledByTheBridge()
    {
        // Élő cloud-mód, Tanara Cloud szolgáltató, bejelentkezés nélkül → Auth-akadály: a híd
        // kezeli (bejelentkezés), a héj NEM nyit Beállításokat és NEM indít semmit.
        m_shell.reset();
        m_app.reset();
        qputenv("TANARA_CLOUD", "live");
        qputenv("TANARA_CLOUD_URL", "http://127.0.0.1:9");
        m_app = std::make_unique<tanara::AppController>();
        qputenv("TANARA_CLOUD", "off");
        qunsetenv("TANARA_CLOUD_URL");
        if (!m_app->cloudLive())
            QSKIP("A cloud-mód nincs befordítva ebbe a buildbe.");
        m_shell = std::make_unique<ShellActions>();
        m_shell->setController(m_app.get());
        m_shell->setBridge(m_bridge.get());
        tanara::AppSettings s = m_app->settings()->settings();
        s.sttProviderId = tanara::cloud::ProviderId;
        s.sttConfigs[tanara::cloud::ProviderId].type = tanara::cloud::ProviderId;
        m_app->settings()->setSettings(s);
        const tanara::Meeting m = recording(QStringLiteral("Cloud"));
        QVERIFY(m_app->usesCloud(tanara::WorkflowStep::Transcribe));
        QSignalSpy started(m_app->jobs(), &tanara::MeetingJobTracker::jobStarted);

        m_bridge->handleBlockers = true;
        m_shell->startTranscription(m.id);
        QCOMPARE(started.count(), 0);
        QCOMPARE(m_bridge->calls.size(), 1);
        QVERIFY(m_bridge->calls.at(0).startsWith(QStringLiteral("blocker:")));
        QVERIFY(m_bridge->calls.at(0).endsWith(tanara::cloud::ProviderId));

        // Az újra-átírás megerősítése ugyanazon a kapun megy át.
        m_bridge->calls.clear();
        m_shell->confirmRetranscribe(m.id, true);
        QCOMPARE(started.count(), 0);
        QCOMPARE(m_bridge->calls.size(), 1);
    }

    void retranscribeAsksFirstAndReportsImpact()
    {
        configureProviders(true);
        const tanara::Meeting m = transcribed(QStringLiteral("Újra-átírandó"));
        QSignalSpy asked(m_shell.get(), &ShellActions::retranscribeDialogRequested);
        QSignalSpy started(m_app->jobs(), &tanara::MeetingJobTracker::jobStarted);

        m_shell->retranscribe(m.id);
        QCOMPARE(asked.count(), 1);
        QCOMPARE(started.count(), 0);                 // megerősítés nélkül semmi nem indul

        const QVariantMap impact = m_shell->retranscribeImpact(m.id);
        QCOMPARE(impact.value("corrections").toInt(), 0);
        QVERIFY(impact.value("text").toString().contains(QStringLiteral("hanglenyomat")));

        m_shell->confirmRetranscribe(m.id, /*keepBackup*/ true);
        QCOMPARE(started.count(), 1);
        m_app->cancelAllJobs(m.id);
        QTRY_VERIFY(!m_app->jobs()->isBusy(m.id));
    }

    // „Újra" egy cloud-hiba után: csak a hibához tartozó, MÉG KIJELÖLT megbeszélésre indul,
    // és ugyanúgy a kapun + költségbecslésen megy át, mint az első indítás.
    void retryTargetsTheFailedMeetingAndGoesThroughTheGate()
    {
        configureProviders(true);
        const tanara::Meeting a = recording(QStringLiteral("Hibázott"));
        const tanara::Meeting b = transcribed(QStringLiteral("Másik"));
        QSignalSpy started(m_app->jobs(), &tanara::MeetingJobTracker::jobStarted);
        QSignalSpy asked(m_shell.get(), &ShellActions::retranscribeDialogRequested);

        // Közben másik megbeszélés lett kijelölve → nem indul semmi, értesítés jön.
        m_shell->showMeeting(b.id);
        emit m_bridge->retryRequested(a.id, QStringLiteral("transcribe"));
        QCOMPARE(started.count(), 0);
        QVERIFY(!m_toasts.isEmpty() && m_toasts.last().contains(QStringLiteral("Hibázott")));

        // Átirattal rendelkező megbeszélésnél a „transcribe" ismétlés újra-átírás: megerősítő
        // ablak jön, nem indul csendben sima átírás.
        emit m_bridge->retryRequested(b.id, QStringLiteral("transcribe"));
        QCOMPARE(asked.count(), 1);
        QCOMPARE(asked.last().at(0).toString(), b.id);
        QCOMPARE(started.count(), 0);

        // A kijelölt, átirat nélküli megbeszélésre az átírás elindul (a kapun át).
        m_shell->showMeeting(a.id);
        emit m_bridge->retryRequested(a.id, QStringLiteral("transcribe"));
        QCOMPARE(started.count(), 1);
        QCOMPARE(started.last().at(0).toString(), a.id);
        m_app->cancelAllJobs(a.id);
        QTRY_VERIFY(!m_app->jobs()->isBusy(a.id));

        // Ismeretlen / üres azonosítóra semmi.
        emit m_bridge->retryRequested(QString(), QStringLiteral("transcribe"));
        emit m_bridge->retryRequested(QStringLiteral("nincs-ilyen"), QStringLiteral("summary"));
        QCOMPARE(started.count(), 1);
    }

    // Átirat után az azonosítás gombja mindig ad látható választ: ha nem indítható, megmondja, miért.
    void identifyAfterTranscriptExplainsWhyItDidNotStart()
    {
        tanara::Meeting m = transcribed(QStringLiteral("Azonosítandó"));
        m_shell->identifyParticipants(m.id);
        QCOMPARE(m_toasts.size(), 1);
        if (!m_app->voiceIdentificationAvailable())
            QVERIFY(m_toasts.last().contains(QStringLiteral("hangmodell")));
        m_toasts.clear();
        m_shell->identifyParticipants(m.id);
        QCOMPARE(m_toasts.size(), 1);                  // minden kattintásra van válasz
    }

    // Mindenki elnevezve: az azonosítás gombja az újraellenőrzést kínálja (megerősítéssel).
    void identifyWithEveryoneNamedOffersRecheck()
    {
        const tanara::Meeting m = namedWithVoices(QStringLiteral("Mindenki megvan"));
        tanara::SpeakerEditor* ed = m_app->speakerEditor(m.id);
        QVERIFY(ed);
        QStringList shown;
        bool answer = true;
        connect(m_shell.get(), &ShellActions::confirmRequested, this,
                [&](const QString& title, const QString& text, const QString& label, bool danger) {
            shown << title + QLatin1Char('|') + label + (danger ? QStringLiteral("|danger") : QString());
            QVERIFY(text.contains(QStringLiteral("bizonytalanként jelölöm meg")));
            QTimer::singleShot(10, m_shell.get(), [&] { m_shell->resolveConfirm(answer); });
        });

        // Még nincs megerősített sor: nincs párbeszéd, a toast megmondja, mi kell.
        m_shell->identifyParticipants(m.id);
        QVERIFY(shown.isEmpty());
        QCOMPARE(m_toasts.size(), 1);
        QVERIFY(m_toasts.last().contains(QStringLiteral("Előbb erősíts meg vagy javíts legalább 3 sort egy beszélőnél")));

        // Anna három valódi sora megerősítve → párbeszéd → 3 kétes sor.
        QVERIFY(ed->confirmUtterances({QStringLiteral("u0"), QStringLiteral("u10000"), QStringLiteral("u25000")}));
        m_shell->setCurrentMeetingId(m.id);
        m_shell->showTab(2);
        m_shell->identifyParticipants(m.id);
        QCOMPARE(shown, QStringList{QStringLiteral("Mindenki azonosítva|Újraellenőrzés")});
        QCOMPARE(m_toasts.last(), QStringLiteral("3 kétséges sort jelöltem meg — a Bizonytalan szűrőben találod."));
        QCOMPARE(ed->uncertainUtteranceIds(),
                 (QStringList{QStringLiteral("u15000"), QStringLiteral("u30000"), QStringLiteral("u45000")}));
        QCOMPARE(m_shell->currentTab(), 1);              // az Átirat fülre vált

        // Elutasítva: semmi sem történik (nincs újabb toast).
        answer = false;
        const int toasts = m_toasts.size();
        m_shell->recheckSpeakers(m.id);
        QCOMPARE(shown.last(), QStringLiteral("Beszélők újraellenőrzése|Újraellenőrzés"));
        QCOMPARE(m_toasts.size(), toasts);

        // A kétes sorok eldöntve → az újabb újraellenőrzés nem talál semmit.
        answer = true;
        QVERIFY(ed->confirmUtterances(ed->uncertainUtteranceIds()));
        m_shell->recheckSpeakers(m.id);
        QCOMPARE(m_toasts.last(), QStringLiteral("A megerősített sorok alapján nem találtam kétséges sort."));
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

        // Megszakítva: nem marad eredmény, értesítés jön.
        const tanara::Meeting other = recording(QStringLiteral("Megszakított"));
        m_bridge->previewCancelled = true;
        m_shell->identifyParticipants(other.id);
        QCOMPARE(guessed.count(), 1);
        QCOMPARE(m_shell->participantsGuess(other.id), QString());
    }

    void confirmWaitsForTheDialog()
    {
        // Nincs, aki megjelenítse → azonnal hamis (nem akad be).
        QVERIFY(!m_shell->confirm(QStringLiteral("Cím"), QStringLiteral("Szöveg"), QStringLiteral("Igen"), true));

        QStringList shown;
        bool answer = true;
        connect(m_shell.get(), &ShellActions::confirmRequested, this,
                [&](const QString& title, const QString&, const QString& label, bool danger) {
            shown << title + QLatin1Char('|') + label + (danger ? QStringLiteral("|danger") : QString());
            // A QML-ablak később válaszol (a felhasználó kattint).
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
        m_shell->renameMeeting(a.id, QStringLiteral("   "));        // üres → marad
        QCOMPARE(m_app->store()->load(a.id).title, QStringLiteral("Új cím"));

        // Törlés: előbb megerősítést kér (a címmel), a megerősítés után töröl és a kijelölés megszűnik.
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

        // Új felvétel elkészült → kijelölődik és értesítés jön.
        m_toasts.clear();
        emit m_app->recordingFinished(m_app->store()->load(b.id));
        QCOMPARE(m_shell->currentMeetingId(), b.id);
        QCOMPARE(m_toasts.size(), 1);

        // A felvevő „Megnyitás az elemzőben” gombja / `tanara --meeting <id>` (a híd jele):
        // kijelölés + a főablak előre; ismeretlen azonosítónál a kijelölés marad.
        m_shell->setCurrentMeetingId(QString());
        QSignalSpy activated(m_shell.get(), &ShellActions::windowActivationRequested);
        emit m_bridge->showMeetingRequested(b.id);
        QCOMPARE(m_shell->currentMeetingId(), b.id);
        QCOMPARE(activated.count(), 1);
        emit m_bridge->showMeetingRequested(QStringLiteral("nincs-ilyen"));
        QCOMPARE(m_shell->currentMeetingId(), b.id);
        QCOMPARE(activated.count(), 2);

        // A hibák nem modális ablakban, hanem értesítésként jelennek meg.
        QSignalSpy toasts(m_shell.get(), &ShellActions::toastRequested);
        emit m_app->errorOccurred(QStringLiteral("Nincs rögzíthető hangeszköz."));
        QCOMPARE(toasts.count(), 1);
        QCOMPARE(toasts.at(0).at(1).toString(), QStringLiteral("danger"));
        QVERIFY(!m_shell->recording());

        // A megnyitott megbeszélés feladat-hibája a hibasávban látszik: ugyanaz a szöveg nem
        // jön még egyszer toastban (más megbeszélésé igen).
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

        ShellMeetingModel model;
        model.setController(m_app.get());
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

        // Átnevezés → a fejléc követi.
        QSignalSpy changed(&model, &ShellMeetingModel::changed);
        m_app->renameMeeting(done.id, QStringLiteral("Új név"));
        QTRY_COMPARE(model.title(), QStringLiteral("Új név"));
        QVERIFY(changed.count() >= 1);

        // Futó feladat → megjelenik a sávban; az ELSŐ átírás nem (azt az átirat előtti nézet mutatja).
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

        // Törölt megbeszélés → nem létezik.
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

        // Százalék nélküli szakasz → határozatlan sáv (percent −1), a futó szakasz nevével.
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
        QCOMPARE(t.value("eta").toString(), QString());   // becslés nélkül nincs kitalált idő
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
            QVERIFY(!QFile::exists(path));      // késleltetve ír
            state.flush();
            QVERIFY(QFile::exists(path));
        }
        ShellUiState again;
        again.setFilePath(path);
        QCOMPARE(again.value(QStringLiteral("selectedMeetingId")).toString(), QStringLiteral("abc"));
        QCOMPARE(again.value(QStringLiteral("window")).toMap().value(QStringLiteral("w")).toInt(), 1400);

        // Útvonal nélkül (demó) csak memóriában él.
        ShellUiState memory;
        memory.setValue(QStringLiteral("k"), 1);
        memory.flush();
        QCOMPARE(memory.value(QStringLiteral("k")).toInt(), 1);
        QVERIFY(memory.filePath().isEmpty());
    }
};

QTEST_MAIN(TestShellActions)
#include "test_shell_actions.moc"
