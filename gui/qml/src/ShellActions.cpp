#include "ShellActions.h"

#include "AppContext.h"
#include "LibraryDemoData.h"
#include "PlayerController.h"
#include "ShellBridge.h"

#include "tanara/AppController.h"
#include "tanara/cloud/CloudTypes.h"
#include "tanara/edit/SpeakerEditor.h"
#include "tanara/jobs/MeetingJobTracker.h"
#include "tanara/store/MeetingArchive.h"
#include "tanara/store/MeetingStore.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QEventLoop>
#include <QMetaMethod>
#include <QTimer>
#include <QUrl>

namespace tanara_qml {

using tanara::BlockerKind;
using tanara::JobKind;
using tanara::WorkflowStep;

ShellActions::ShellActions(QObject* parent) : QObject(parent)
{
    AppContext* ctx = AppContext::instance();
    m_controller = ctx->controller();
    m_bridge = qobject_cast<ShellBridge*>(ctx->bridge());
    connect(ctx, &AppContext::controllerChanged, this, [this, ctx] {
        if (m_controllerInjected) return;
        m_controller = ctx->controller();
        attachController();
    });
    connect(ctx, &AppContext::bridgeChanged, this, [this, ctx] {
        if (m_bridgeInjected) return;
        m_bridge = qobject_cast<ShellBridge*>(ctx->bridge());
        attachBridge();
    });
    attachController();
    attachBridge();
}

void ShellActions::setController(tanara::AppController* controller)
{
    m_controllerInjected = true;
    m_controller = controller;
    attachController();
}

void ShellActions::setBridge(ShellBridge* bridge)
{
    m_bridgeInjected = true;
    m_bridge = bridge;
    attachBridge();
}

ShellBridge* ShellActions::bridge() const
{
    return m_bridge.data();
}

void ShellActions::attachController()
{
    if (m_controller == m_attachedController)
        return;
    if (m_attachedController)
        m_attachedController->disconnect(this);
    m_attachedController = m_controller;
    emit recordingChanged();
    tanara::AppController* c = m_controller;
    if (!c)
        return;

    connect(c, &tanara::AppController::recordingStateChanged, this,
            [this](tanara::RecordingState) { emit recordingChanged(); });
    // Új felvétel: megjelenik a könyvtárban (a modell a store jelére frissül) és kijelölődik.
    connect(c, &tanara::AppController::recordingFinished, this, [this](const tanara::Meeting& m) {
        showMeeting(m.id);
        toast(tr("Felvétel kész: %1").arg(m.title));
    });
    // Importálás kész: ugyanúgy, mint egy új felvétel — megjelenik és kijelölődik (az átirat
    // előtti nézettel; átírás nem indul magától).
    connect(c, &tanara::AppController::importFinished, this, [this](const tanara::Meeting& m) {
        showMeeting(m.id);
        setCurrentTab(0);
        toast(tr("Importálva: %1").arg(m.title));
    });
    // Kész az átirat / összefoglaló: értesítés; ha épp ez a megbeszélés van nyitva, a fülre vált.
    connect(c, &tanara::AppController::transcriptReady, this, [this](const QString& id, const QString&) {
        toast(tr("Elkészült az átirat: %1").arg(meeting(id).title));
        if (id == m_currentMeetingId) setCurrentTab(0);
    });
    connect(c, &tanara::AppController::summaryReady, this, [this](const QString& id, const QString&) {
        toast(tr("Elkészült az összefoglaló: %1").arg(meeting(id).title));
        if (id == m_currentMeetingId) setCurrentTab(1);
    });
    connect(c, &tanara::AppController::topicsReady, this,
            [this](const QString& id, const QVector<tanara::SummaryTopic>&) {
        if (id == m_currentMeetingId) setCurrentTab(1);
    });
    connect(c, &tanara::AppController::topicAnalysisQueueFinished, this,
            [this](const QString&, int okCount, int failCount) {
        if (failCount == 0)
            toast(tr("%n téma elemzése kész.", nullptr, okCount));
        else
            toast(tr("%1, %2 — a hibásak a kártyájukon újrafuttathatók.")
                      .arg(tr("%n téma kész", nullptr, okCount), tr("%n hibázott", nullptr, failCount)));
    });
    // Archívum-művelet vége (export: a feladat-sáv mutatta a haladást; import: kijelölés).
    connect(c, &tanara::AppController::archiveFinished, this,
            [this](const QString& opId, bool ok, const QString& meetingId, const QString& path,
                   const QString& message) {
        const bool isExport = opId.startsWith(QLatin1String("export:"));
        if (isExport) {
            if (ok)
                emit toastRequested(tr("Exportálva: %1").arg(QDir::toNativeSeparators(path)), {}, {}, false,
                                    {}, path);
            else if (message == tanara::AppController::tr("Megszakítva."))
                toast(tr("Az exportálás megszakítva."));
            else
                emit toastRequested(tr("Az exportálás nem sikerült: %1").arg(message),
                                    QStringLiteral("danger"), {}, false);
            return;
        }
        if (!ok) {
            emit toastRequested(tr("Az importálás nem sikerült: %1").arg(message),
                                QStringLiteral("danger"), {}, false);
            return;
        }
        showMeeting(meetingId);
        setCurrentTab(0);
        toast(tr("Importálva archívumból: %1").arg(meeting(meetingId).title));
    });
    connect(c, &tanara::AppController::mixdownUpdated, this, [this](const QString&, bool ok) {
        if (!ok) emit toastRequested(tr("A lekeverés nem sikerült."), QStringLiteral("danger"), {}, false);
    });
    // Feladathoz nem köthető hibák (felvétel, eszköz …). A feladatok hibáit a tracker őrzi
    // (a nézetek hibakártyája mutatja), de a szöveges jel itt is látszik, nem modálisan.
    // Ha a hiba a megnyitott megbeszélés hibasávjában már látszik, nem ismételjük toastban.
    connect(c, &tanara::AppController::errorOccurred, this, [this](const QString& message) {
        if (!m_errorInBanner.isEmpty() && message.contains(m_errorInBanner))
            return;
        emit toastRequested(message, QStringLiteral("danger"), {}, false);
    });
    if (tanara::MeetingJobTracker* jobs = c->jobs()) {
        connect(jobs, &tanara::MeetingJobTracker::jobFinished, this, &ShellActions::onJobFinished);
        connect(jobs, &tanara::MeetingJobTracker::errorChanged, this,
                [this, jobs](const QString& meetingId, JobKind kind) {
            if (meetingId != m_currentMeetingId) return;
            const tanara::JobError e = jobs->lastError(meetingId, kind);
            if (!e.isValid()) return;
            m_errorInBanner = e.message;
            QTimer::singleShot(0, this, [this] { m_errorInBanner.clear(); });
        });
    }
    // A kijelölt megbeszélést törölték (innen vagy máshonnan) → nincs kijelölés.
    if (tanara::MeetingStore* store = c->store())
        connect(store, &tanara::MeetingStore::meetingRemoved, this, [this](const QString& id) {
            m_participantGuesses.remove(id);
            if (id == m_currentMeetingId) setCurrentMeetingId(QString());
        });
}

void ShellActions::attachBridge()
{
    if (m_bridge == m_attachedBridge)
        return;
    if (m_attachedBridge)
        m_attachedBridge->disconnect(this);
    m_attachedBridge = m_bridge;
    ShellBridge* b = m_bridge;
    if (!b)
        return;
    connect(b, &ShellBridge::toastRequested, this,
            [this](const QString& text, const QString& requestId, bool usageLink) {
        emit toastRequested(text, QString(), requestId, usageLink);
    });
    connect(b, &ShellBridge::retryRequested, this, &ShellActions::onRetry);
    connect(b, &ShellBridge::readinessChanged, this, &ShellActions::bumpReadiness);
    connect(b, &ShellBridge::showMeetingRequested, this, [this](const QString& meetingId) {
        if (meetingExists(meetingId))
            showMeeting(meetingId);
        activateWindow();
    });
    connect(b, &ShellBridge::tagFilterRequested, this, [this](const QString& tagId) {
        filterByTag(tagId);
        activateWindow();
    });
}

bool ShellActions::recording() const
{
    return m_controller && m_controller->recordingState() != tanara::RecordingState::Idle;
}

void ShellActions::bumpReadiness()
{
    ++m_readinessRevision;
    emit readinessRevisionChanged();
}

tanara::Meeting ShellActions::meeting(const QString& meetingId) const
{
    if (m_controller && m_controller->store() && !meetingId.isEmpty())
        return m_controller->store()->load(meetingId);
    return {};
}

bool ShellActions::meetingExists(const QString& meetingId) const
{
    if (meetingId.isEmpty())
        return false;
    if (m_controller)
        return !meeting(meetingId).id.isEmpty();
    return AppContext::instance()->demo() && demo::find(meetingId);
}

// ---- navigáció -------------------------------------------------------------------------

void ShellActions::setCurrentMeetingId(const QString& id)
{
    if (id == m_currentMeetingId)
        return;
    m_currentMeetingId = id;
    emit currentMeetingIdChanged();
}

void ShellActions::setCurrentTab(int index)
{
    index = qBound(0, index, 2);
    if (index == m_currentTab)
        return;
    m_currentTab = index;
    emit currentTabChanged();
}

void ShellActions::setPlayer(PlayerController* player)
{
    if (player == m_player)
        return;
    m_player = player;
    emit playerChanged();
}

void ShellActions::showMeeting(const QString& meetingId)
{
    setCurrentMeetingId(meetingId);
}

void ShellActions::showTab(int index)
{
    setCurrentTab(index);
}

void ShellActions::seekTo(const QString& meetingId, int ms)
{
    if (!meetingId.isEmpty())
        showMeeting(meetingId);
    showTab(0);
    if (m_player) {
        // A lejátszó meetingId-je a Main.qml kötésén át követi a kijelölést; ha a kötés még
        // nem futott le (vagy nincs), itt biztosítjuk, hogy a jó hangot tekerjük.
        if (!m_currentMeetingId.isEmpty() && m_player->meetingId() != m_currentMeetingId)
            m_player->setMeetingId(m_currentMeetingId);
        m_player->seek(ms);
    }
    emit transcriptPositionRequested(ms);
}

void ShellActions::toast(const QString& text, const QString& undoKey)
{
    if (!text.isEmpty())
        emit toastRequested(text, QString(), QString(), false, undoKey);
}

void ShellActions::undoFromToast(const QString& undoKey)
{
    if (!undoKey.isEmpty())
        emit undoRequested(undoKey);
}

void ShellActions::filterByTag(const QString& tagId)
{
    if (!tagId.isEmpty())
        emit tagFilterRequested(tagId);
}

// ---- Widgets-ablakok -------------------------------------------------------------------

void ShellActions::openSettings(const QString& page, const QString& focusField)
{
    if (ShellBridge* b = bridge()) {
        // A Beállítások külön, nem modális ablak: a mentés a híd readinessChanged jelén át
        // frissíti a nézeteket (itt csak a régi, modális hidak kedvéért léptetünk).
        if (focusField.isEmpty()) b->openSettings(page);
        else b->openSettingsAt(page, focusField);
        bumpReadiness();
    } else {
        toast(tr("A Beállítások ebben a módban nem érhetők el."));
    }
}

void ShellActions::openPeople(const QString& person)
{
    if (ShellBridge* b = bridge())
        person.trimmed().isEmpty() ? b->openPeople() : b->openPeopleAt(person.trimmed());
    else
        toast(tr("A Személyek ebben a módban nem érhetők el."));
}

void ShellActions::openTags(const QString& tagId)
{
    if (ShellBridge* b = bridge())
        tagId.trimmed().isEmpty() ? b->openTags() : b->openTagsAt(tagId.trimmed());
    else
        toast(tr("A Címkék ebben a módban nem érhetők el."));
}

void ShellActions::openOnboarding()
{
    if (ShellBridge* b = bridge())
        b->openOnboarding();
    else
        toast(tr("Az Első lépések ebben a módban nem érhetők el."));
}

void ShellActions::openRecorder()
{
    if (ShellBridge* b = bridge())
        b->openRecorder();
    else
        toast(tr("A felvevő ebben a módban nem érhető el."));
}

QString ShellActions::pickAudioFile()
{
    ShellBridge* b = bridge();
    return b ? b->pickAudioFile() : QString();
}

QStringList ShellActions::pickAudioFiles()
{
    ShellBridge* b = bridge();
    return b ? b->pickAudioFiles() : QStringList();
}

void ShellActions::openImport(const QVariantList& files)
{
    QVariantList list = files;
    const bool busy = m_controller && m_controller->importer() && m_controller->importer()->busy();
    if (list.isEmpty() && !busy && bridge()) {
        const QStringList picked = pickAudioFiles();
        if (picked.isEmpty())
            return;                       // visszalépett a választóból
        for (const QString& p : picked) list << p;
    }
    emit importDialogRequested(list);
}

// ---- megbeszélés-archívum --------------------------------------------------------------

void ShellActions::exportArchive(const QString& meetingId)
{
    ShellBridge* b = bridge();
    if (!m_controller || !b)
        return;
    const tanara::Meeting m = meeting(meetingId);
    if (m.id.isEmpty())
        return;
    QString dir = m_lastArchiveDir;
    if (dir.isEmpty() || !QFileInfo(dir).isDir()) {
        const QString tanaraDir = QDir::home().filePath(QStringLiteral("Tanara"));
        dir = QFileInfo(tanaraDir).isDir() ? tanaraDir : QDir::homePath();
    }
    QString path = b->pickSaveFile(tr("Exportálás archívumba"),
                                   QDir(dir).filePath(tanara::MeetingArchive::suggestedFileName(m)),
                                   tr("Tanara-archívum (*.tanara.zip)"));
    if (path.isEmpty())
        return;                           // visszalépett
    if (!path.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive))
        path += tanara::MeetingArchive::fileSuffix();
    m_lastArchiveDir = QFileInfo(path).absolutePath();
    m_controller->exportMeetingArchive(meetingId, path);   // hiba: errorOccurred → toast
}

namespace {
QString localPath(const QVariant& pathOrUrl)
{
    const QUrl url = pathOrUrl.toUrl();
    return url.isLocalFile() ? url.toLocalFile() : pathOrUrl.toString();
}
} // namespace

void ShellActions::importArchive(const QVariant& pathOrUrl)
{
    if (!m_controller)
        return;
    QString file = localPath(pathOrUrl);
    if (file.isEmpty()) {
        ShellBridge* b = bridge();
        if (!b)
            return;
        file = b->pickArchiveFile();
        if (file.isEmpty())
            return;                       // visszalépett
    }
    if (!m_controller->importMeetingArchive(file).isEmpty())
        toast(tr("Archívum importálása: %1…").arg(QFileInfo(file).fileName()));
}

void ShellActions::importFolder(const QVariant& pathOrUrl)
{
    if (!m_controller)
        return;
    QString folder = localPath(pathOrUrl);
    if (folder.isEmpty()) {
        ShellBridge* b = bridge();
        if (!b)
            return;
        folder = b->pickMeetingFolder();
        if (folder.isEmpty())
            return;                       // visszalépett
    }
    if (!m_controller->importMeetingFolder(folder).isEmpty())
        toast(tr("Mappa importálása: %1…").arg(QFileInfo(folder).fileName()));
}

bool ShellActions::isDirectory(const QVariant& pathOrUrl) const
{
    const QString p = localPath(pathOrUrl);
    return !p.isEmpty() && QFileInfo(p).isDir();
}

bool ShellActions::isArchiveFile(const QVariant& pathOrUrl) const
{
    return localPath(pathOrUrl).endsWith(QLatin1String(".zip"), Qt::CaseInsensitive);
}

void ShellActions::revealFile(const QString& path)
{
    if (!path.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
}

// ---- kapuzás ---------------------------------------------------------------------------

bool ShellActions::gate(WorkflowStep step, const QString& meetingId)
{
    if (!m_controller || meetingId.isEmpty())
        return false;
    const tanara::ReadinessResult r = m_controller->canRun(step, meetingId);
    if (r.runnable)
        return true;
    ShellBridge* b = bridge();
    // Tanara Cloud: pontosan egy teendő (bejelentkezés / feltöltés / frissítés).
    if (b && b->handleCloudBlocker(r)) {
        bumpReadiness();
        return false;
    }
    if (r.blockerKind == BlockerKind::ProviderConfig || r.blockerKind == BlockerKind::Auth) {
        // Hiányzó szolgáltató-beállítás → a Beállítások „Külső szolgáltatások” lapja.
        openSettings(QStringLiteral("providers"),
                     step == WorkflowStep::Transcribe ? QStringLiteral("stt") : QStringLiteral("llm"));
    } else {
        toast(tr("Nem indítható: %1").arg(r.detail));
    }
    return false;
}

bool ShellActions::estimateOk(const QString& meetingId, const QString& task, const QString& mode)
{
    if (!m_controller)
        return false;
    const WorkflowStep step = task == QLatin1String("transcribe") ? WorkflowStep::Transcribe
                                                                  : WorkflowStep::Summarize;
    if (!m_controller->usesCloud(step))
        return true;                      // saját kulcs: nincs becslés
    ShellBridge* b = bridge();
    // Híd nélkül cloud-futás NEM indulhat megerősítés nélkül.
    return b && b->confirmCloudEstimate(meetingId, task, mode);
}

// ---- feldolgozás -----------------------------------------------------------------------

void ShellActions::startTranscription(const QString& meetingId)
{
    if (!gate(WorkflowStep::Transcribe, meetingId))
        return;
    if (!estimateOk(meetingId, QStringLiteral("transcribe"), QString()))
        return;
    m_controller->transcribeMeeting(meetingId);
}

void ShellActions::retranscribe(const QString& meetingId)
{
    if (meetingId.isEmpty())
        return;
    emit retranscribeDialogRequested(meetingId);
}

QVariantMap ShellActions::retranscribeImpact(const QString& meetingId) const
{
    int corrections = 23;                 // demó: az M10 minta számai
    int named = 0;
    if (m_controller) {
        const tanara::RetranscribeImpact imp = m_controller->retranscribeImpact(meetingId);
        corrections = imp.manualCorrections();
        named = imp.namedSpeakers + imp.addedParticipants;
    }
    QString text;
    if (corrections > 0)
        text = tr("A mostani átirat és a benne lévő %n kézi javítás elvész, az összefoglaló "
                  "elavulttá válik.", nullptr, corrections);
    else
        text = tr("A mostani átirat elvész, az összefoglaló elavulttá válik.");
    if (named > 0)
        text += QLatin1Char(' ') + tr("A beszélők elnevezése (%n beszélő) törlődik, mert az új "
                                      "átirat másképp oszthatja fel a beszélőket.", nullptr, named);
    text += QLatin1Char(' ') + tr("A hanglenyomatokba tanított javítások megmaradnak.");
    return {{QStringLiteral("text"), text},
            {QStringLiteral("corrections"), corrections},
            {QStringLiteral("namedSpeakers"), named}};
}

void ShellActions::confirmRetranscribe(const QString& meetingId, bool keepBackup)
{
    if (!gate(WorkflowStep::Transcribe, meetingId))
        return;
    if (!estimateOk(meetingId, QStringLiteral("transcribe"), QString()))
        return;
    m_controller->retranscribeMeeting(meetingId, keepBackup);
}

void ShellActions::startQuickSummary(const QString& meetingId)
{
    if (!gate(WorkflowStep::Summarize, meetingId))
        return;
    if (!estimateOk(meetingId, QStringLiteral("summarize"), QStringLiteral("quick")))
        return;
    m_controller->summarizeMeeting(meetingId);
}

void ShellActions::startTopicExtraction(const QString& meetingId)
{
    if (!gate(WorkflowStep::Summarize, meetingId))
        return;
    // Ha már van (szerkesztett) téma-lista, a kinyerés nem hív modellt → nincs becslés.
    const bool haveTopics = !m_controller->meetingTopics(meetingId).isEmpty();
    if (!haveTopics
        && !estimateOk(meetingId, QStringLiteral("summarize"), QStringLiteral("complex")))
        return;
    m_controller->extractMeetingTopics(meetingId);
}

void ShellActions::startTopicAnalysis(const QString& meetingId)
{
    if (!gate(WorkflowStep::Summarize, meetingId))
        return;
    const QVector<tanara::SummaryTopic> topics = m_controller->meetingTopics(meetingId);
    if (topics.isEmpty()) {
        toast(tr("Adj meg legalább egy témát."));
        return;
    }
    // Cloud: becslés a hátralévő (még elemzetlen) témákra + a záró összegzésre.
    if (!estimateOk(meetingId, QStringLiteral("summarize"), QStringLiteral("complex")))
        return;
    m_controller->generateComplexSummary(meetingId, topics);
}

void ShellActions::analyzeTopic(const QString& meetingId, const QString& topicId)
{
    if (!gate(WorkflowStep::Summarize, meetingId))
        return;
    const QVector<tanara::SummaryTopic> topics = m_controller->meetingTopics(meetingId);
    for (const tanara::SummaryTopic& t : topics) {
        if (t.id != topicId) continue;
        if (t.title.trimmed().isEmpty()) {
            toast(tr("A témához cím kell az elemzéshez."));
            return;
        }
        m_controller->analyzeTopic(meetingId, t);
        return;
    }
    toast(tr("Ez a téma már nem létezik."));
}

void ShellActions::identifyParticipants(const QString& meetingId)
{
    if (!m_controller || meetingId.isEmpty())
        return;
    const tanara::Meeting m = meeting(meetingId);
    if (m.id.isEmpty())
        return;
    if (m.hasTranscript) {
        // Átirat után: aszinkron, a feladat-sáv mutatja („3 / 5 beszélő”), megszakítható.
        if (m_controller->jobs() && m_controller->jobs()->isRunning(meetingId, JobKind::Identify))
            return;
        if (m_controller->identifyMeetingAsync(meetingId)) {
            m_identifyRequested.insert(meetingId);
            return;
        }
        // Nem indult: mondjuk meg, MIÉRT (a core csak igaz/hamisat ad).
        if (m_controller->jobs() && m_controller->jobs()->isRunning(meetingId, JobKind::Transcribe)) {
            toast(tr("Az átírás még fut — a végén magától azonosítja a résztvevőket."));
            return;
        }
        bool anonymous = false;
        bool anySpeaker = false;
        if (tanara::SpeakerEditor* ed = m_controller->speakerEditor(meetingId))
            for (const tanara::EditorSpeaker& sp : ed->speakers()) {
                if (sp.utteranceCount <= 0) continue;
                anySpeaker = true;
                anonymous = anonymous || (sp.anonymous && !sp.added);
            }
        if (anySpeaker && !anonymous) {
            // Mindenkinek van neve: azonosítani nincs mit, de a sorok újraellenőrizhetők.
            runRecheck(meetingId, tr("Mindenki azonosítva"),
                       tr("Ebben a megbeszélésben már minden beszélőnek van neve. Újraellenőrizzem a "
                          "sorokat a megerősített és javított sorok hangja alapján? A kétséges sorokat "
                          "bizonytalanként jelölöm meg, hogy átnézhesd őket."));
        } else if (!m_controller->voiceIdentificationAvailable()) {
            toast(tr("Az azonosításhoz nincs telepítve a hangmodell."));
        } else {
            toast(anonymous
                      ? tr("A névtelen beszélőkhöz nem találtam használható hangot, ezért nincs mit azonosítani.")
                      : tr("Nincs mit azonosítani: ebben a megbeszélésben nincs beszélő."));
        }
        return;
    }
    // Átirat előtt: előnézet a hangsávokból (nem ír a megbeszélésbe) — modális haladás-ablakkal.
    ShellBridge* b = bridge();
    if (!b)
        return;
    bool cancelled = false;
    const QString summary = b->identifyParticipantsPreview(meetingId, &cancelled);
    if (cancelled) {
        toast(tr("Azonosítás megszakítva."));
        return;
    }
    m_participantGuesses.insert(meetingId, summary);
    emit participantsGuessed(meetingId, summary);
    toast(tr("Résztvevők: %1").arg(summary));
}

void ShellActions::recheckSpeakers(const QString& meetingId)
{
    runRecheck(meetingId, tr("Beszélők újraellenőrzése"),
               tr("Újraellenőrizzem a sorokat a megerősített és javított sorok hangja alapján? "
                  "A kétséges sorokat bizonytalanként jelölöm meg, hogy átnézhesd őket."));
}

void ShellActions::runRecheck(const QString& meetingId, const QString& title, const QString& text)
{
    if (!m_controller || meetingId.isEmpty())
        return;
    tanara::SpeakerEditor* ed = m_controller->speakerEditor(meetingId);
    if (!ed || !ed->hasTranscript()) {
        toast(tr("Ennek a megbeszélésnek nincs szerkeszthető átirata."));
        return;
    }
    if (!ed->canRecheck()) {
        toast(ed->recheckBlocker());
        return;
    }
    if (!confirm(title, text, tr("Újraellenőrzés"), false))
        return;
    // A párbeszéd alatt változhatott az állapot (pl. elindult a hang-elemzés).
    if (!ed->canRecheck()) {
        toast(ed->recheckBlocker());
        return;
    }
    // A „Bizonytalan" szűrőt a szerkesztő nézetmodellje kapcsolja be (recheckFinished jel).
    const tanara::SpeakerEditor::RecheckResult r = ed->recheckFromConfirmed();
    QString msg;
    if (r.flagged > 0) {
        if (meetingId == m_currentMeetingId) showTab(0);
        msg = tr("%n kétséges sort jelöltem meg — a Bizonytalan szűrőben találod.", "", r.flagged);
    } else {
        msg = tr("A megerősített sorok alapján nem találtam kétséges sort.");
    }
    // Ha tárolt lenyomat is beszállt a referenciába: miből állt össze.
    const QString refs = r.referenceSummary();
    if (!refs.isEmpty()) msg += QLatin1Char(' ') + refs;
    toast(msg);
}

QString ShellActions::participantsGuess(const QString& meetingId) const
{
    return m_participantGuesses.value(meetingId);
}

QString ShellActions::speakerSummary(const QString& meetingId) const
{
    const tanara::Meeting m = meeting(meetingId);
    QStringList named;
    for (auto it = m.speakerMap.constBegin(); it != m.speakerMap.constEnd(); ++it)
        if (!it.value().trimmed().isEmpty())
            named << it.value().trimmed();
    named.removeDuplicates();
    if (named.isEmpty())
        return tr("Az azonosítás kész: egyik beszélő hangja sem ismert még. A neveket az "
                  "átiratban adhatod meg.");
    return tr("Az azonosítás kész. Felismert résztvevők: %1.").arg(named.join(QStringLiteral(", ")));
}

void ShellActions::onJobFinished(const QString& meetingId, JobKind kind, tanara::JobOutcome outcome)
{
    if (kind != JobKind::Identify || !m_identifyRequested.remove(meetingId))
        return;
    if (outcome == tanara::JobOutcome::Cancelled)
        toast(tr("Azonosítás megszakítva. A már megtalált nevek megmaradtak."));
    else if (outcome == tanara::JobOutcome::Done)
        toast(speakerSummary(meetingId));
}

void ShellActions::requestLlmContext(int tokens)
{
    if (m_controller) m_controller->requestLlmContext(tokens);
}

void ShellActions::cancelJob(const QString& meetingId, int jobKind)
{
    if (m_controller)
        m_controller->cancelJob(meetingId, static_cast<JobKind>(jobKind));
}

void ShellActions::onRetry(const QString& meetingId, const QString& kind)
{
    // Újra / Folytatás egy cloud-hiba után: ugyanaz a lépés, új futás (a folytatás csak a
    // hátralévő részekért fizet — a kész téma-elemzések a lemezen vannak). A cél MINDIG az a
    // megbeszélés, amelyikhez a hiba tartozik; ha már nem az van kijelölve (a hibaablak alatt
    // váltott a kijelölés), nem indítunk a háttérben fizetős futást — ahogy a régi ablak sem.
    if (!m_controller || meetingId.isEmpty())
        return;
    const tanara::Meeting m = meeting(meetingId);
    if (m.id.isEmpty())
        return;
    if (meetingId != m_currentMeetingId) {
        toast(tr("Az ismétlés nem indult el, mert közben másik megbeszélésre váltottál: %1").arg(m.title));
        return;
    }
    // Minden ág a kapun és a költségbecslésen megy át (Tanara Cloud: a felhasználó újra látja
    // az árat ehhez a megbeszéléshez).
    if (kind == QLatin1String("transcribe")) {
        // Ha a megbeszélésnek VAN átirata, a hiba egy újra-átírásé volt: az a megerősítő
        // ablakon át indul újra (kézi javítások, másolat), nem sima átírásként.
        if (m.hasTranscript) retranscribe(meetingId);
        else startTranscription(meetingId);
    } else if (kind == QLatin1String("summary"))
        startQuickSummary(meetingId);
    else if (kind == QLatin1String("topics"))
        startTopicExtraction(meetingId);
    else if (kind == QLatin1String("complex"))
        startTopicAnalysis(meetingId);
}

// ---- megbeszélés-műveletek -------------------------------------------------------------

void ShellActions::revealInFolder(const QString& meetingId)
{
    const tanara::Meeting m = meeting(meetingId);
    if (!m.folder.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(m.folder));
}

void ShellActions::renameMeeting(const QString& meetingId, const QString& title)
{
    const QString trimmed = title.trimmed();
    if (!m_controller || trimmed.isEmpty())
        return;
    if (meeting(meetingId).title != trimmed)
        m_controller->renameMeeting(meetingId, trimmed);
}

void ShellActions::requestRename(const QString& meetingId)
{
    if (meetingId.isEmpty())
        return;
    showMeeting(meetingId);
    emit renameRequested(meetingId);
}

void ShellActions::requestDelete(const QString& meetingId)
{
    if (meetingId.isEmpty())
        return;
    QString title = meeting(meetingId).title;
    if (title.isEmpty())
        if (const demo::DemoMeeting* d = demo::find(meetingId))
            title = d->entry.title;
    emit deleteDialogRequested(meetingId, title);
}

void ShellActions::deleteMeeting(const QString& meetingId)
{
    if (!m_controller || meetingId.isEmpty())
        return;
    // Törlés előtt a futó feladatok megszakadnak (ne írjanak a törölt mappába), a lejátszó
    // pedig elengedi a hangfájlt.
    m_controller->cancelAllJobs(meetingId);
    if (m_player && m_player->meetingId() == meetingId)
        m_player->setMeetingId(QString());
    m_controller->closeSpeakerEditor(meetingId);
    if (meetingId == m_currentMeetingId)
        setCurrentMeetingId(QString());
    m_controller->deleteMeeting(meetingId);
}

void ShellActions::stopRecording()
{
    if (m_controller && m_controller->recordingState() == tanara::RecordingState::Recording)
        m_controller->stopRecording();
}

void ShellActions::activateWindow()
{
    emit windowActivationRequested();
}

// ---- confirm(): M10-mintájú, modális megerősítés a QML-ablakban --------------------------

bool ShellActions::confirm(const QString& title, const QString& text, const QString& confirmLabel,
                           bool danger)
{
    static const QMetaMethod sig = QMetaMethod::fromSignal(&ShellActions::confirmRequested);
    // Nincs, aki megjelenítse (önállóan renderelt komponens, teszt) vagy már nyitva van egy.
    if (!isSignalConnected(sig) || m_confirmLoop)
        return false;
    QEventLoop loop;
    m_confirmLoop = &loop;
    m_confirmResult = false;
    emit confirmRequested(title, text, confirmLabel, danger);
    if (m_confirmLoop)                    // a QML azonnal is válaszolhatott
        loop.exec();
    m_confirmLoop = nullptr;
    return m_confirmResult;
}

void ShellActions::resolveConfirm(bool accepted)
{
    m_confirmResult = accepted;
    if (m_confirmLoop) {
        QEventLoop* loop = m_confirmLoop;
        m_confirmLoop = nullptr;
        loop->quit();
    }
}

} // namespace tanara_qml
