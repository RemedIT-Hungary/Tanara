#include "QmlShellBridge.h"

#include "PeopleManagerDialog.h"
#include "SettingsDialog.h"
#include "ShellRecorderHost.h"
#include "cloud/CloudEstimateDialog.h"
#include "cloud/CloudLoginDialog.h"
#include "cloud/CloudModeDialog.h"
#include "cloud/CloudTermsDialog.h"
#include "cloud/CloudUi.h"

#include "tanara/AppController.h"
#include "tanara/SettingsManager.h"
#include "tanara/cloud/CloudAccount.h"
#include "tanara/store/MeetingStore.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QEvent>
#include <QFileDialog>
#include <QProgressDialog>
#include <QStandardPaths>
#include <QTimer>
#include <QWindow>

namespace tanara_gui {

namespace {

// Emberi összegző mondat: nincs név → „N különböző partner azonosítva";
// van név → „A, B és X ismeretlen partner" (X==0 → csak a nevek).
QString participantsSummary(QStringList named, int unknownCount, int totalDistinct)
{
    named.removeDuplicates();
    if (named.isEmpty())
        return QCoreApplication::translate("QmlShellBridge", "%n különböző partner azonosítva",
                                           nullptr, totalDistinct);
    QString s = named.join(QStringLiteral(", "));
    if (unknownCount > 0)
        s += QCoreApplication::translate("QmlShellBridge", " és %n ismeretlen partner",
                                         nullptr, unknownCount);
    return s;
}

} // namespace

QmlShellBridge::QmlShellBridge(tanara::AppController* controller, QObject* parent)
    : tanara_qml::ShellBridge(parent), m_controller(controller)
{
    qApp->installEventFilter(this);

    // A felvevő-kötés (régi Widgets-felvevő + singleton) külön, cserélhető osztályban él.
    m_recorder = new ShellRecorderHost(m_controller, this);
    // Háttér-módban (rejtett főablak) a felvevő az utolsó látható ablak — ha az is eltűnne,
    // a Qt kiléptetne felvétel közben. Ilyenkor előbb a főablak jön vissza.
    connect(m_recorder, &ShellRecorderHost::aboutToHide, this, [this]() {
        if (mainWindowHidden()) emit showWindowRequested();
    });
    // Felvétel vége: (a) „Leállítom és kilépek” → a főablak bezárható; (b) háttér-módban
    // (rejtett főablak) → a főablak visszajön; (c) singleton-figyelés újrapróbálása.
    connect(m_controller, &tanara::AppController::recordingStateChanged, this,
            [this](tanara::RecordingState st) {
                if (st != tanara::RecordingState::Idle) return;
                if (m_quitAfterStop) { emit quitRequested(); return; }
                if (mainWindowHidden()) emit showWindowRequested();
                m_recorder->retryListening();
            });

    wireCloud();
}

QmlShellBridge::~QmlShellBridge()
{
    shutdown();
}

void QmlShellBridge::setMainWindow(QWindow* window)
{
    m_window = window;
}

bool QmlShellBridge::mainWindowHidden() const
{
    return m_window && !m_window->isVisible();
}

bool QmlShellBridge::eventFilter(QObject* watched, QEvent* event)
{
    // A QML-főablak nem QWidget, ezért a Widgets-párbeszédablakoknak nincs szülő-widgetje.
    // Megjelenéskor az ablakkezelőnek megmondjuk, hogy a főablakhoz tartoznak (fölötte
    // maradnak, hozzá igazodnak) — a felvevő (nem QDialog) önálló ablak marad.
    if (event->type() == QEvent::Show && m_window && m_window->isVisible()) {
        auto* dialog = qobject_cast<QDialog*>(watched);
        if (dialog && dialog->isWindow() && !dialog->parentWidget()) {
            if (QWindow* handle = dialog->windowHandle())
                if (!handle->transientParent())
                    handle->setTransientParent(m_window);
        }
    }
    return tanara_qml::ShellBridge::eventFilter(watched, event);
}

// ---- Beállítások / Személyek ---------------------------------------------------------------

void QmlShellBridge::openSettings(const QString& page)
{
    SettingsDialog dlg(m_controller, nullptr);
    if (!page.isEmpty())
        dlg.showPage(page);
    if (dlg.exec() == QDialog::Accepted)
        m_recorder->refreshFromSettings();    // a felvevő tükrözze az új eszköz-policyt
    refreshCloudChrome();
    emit readinessChanged();
}

void QmlShellBridge::openPeople()
{
    // Nem-modális, hogy a háttérben az átnevezés / törlés hatása (speakerMapChanged) azonnal
    // látszódjon a nyitott átiraton. Egy példány: a második kérés az elsőt hozza előre.
    if (!m_peopleDialog) {
        m_peopleDialog = new PeopleManagerDialog(m_controller, nullptr);
        m_peopleDialog->setAttribute(Qt::WA_DeleteOnClose);
    }
    m_peopleDialog->show();
    m_peopleDialog->raise();
    m_peopleDialog->activateWindow();
}

QString QmlShellBridge::pickAudioFile()
{
    const QString start = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    return QFileDialog::getOpenFileName(
        nullptr, tr("Hangfájl kiválasztása"), start,
        tr("Hangfájlok (*.ogg *.opus *.flac *.wav *.mp3 *.m4a *.aac);;Minden fájl (*)"));
}

// ---- felvevő (lásd ShellRecorderHost) ----------------------------------------------------

void QmlShellBridge::startRecorderListening()
{
    m_recorder->startListening();
}

void QmlShellBridge::openRecorder()
{
    m_recorder->open();
}

void QmlShellBridge::continueRecordingInBackground()
{
    // A lebegő felvevő önálló top-level ablak → az app életben marad; a főablakot a QML
    // rejti el, és a felvétel végén magától visszajön (showWindowRequested).
    openRecorder();
}

void QmlShellBridge::stopRecordingAndQuit()
{
    if (m_controller->recordingState() == tanara::RecordingState::Idle) {
        emit quitRequested();
        return;
    }
    m_quitAfterStop = true;
    m_controller->stopRecording();   // aszinkron (kódolás) → Idle-nél quitRequested()
}

void QmlShellBridge::shutdown()
{
    if (m_shutDown)
        return;
    m_shutDown = true;
    qApp->removeEventFilter(this);
    if (m_peopleDialog)
        m_peopleDialog->close();
    m_recorder->shutdown();
}

// ---- résztvevők tippelése átirat előtt ----------------------------------------------------

QString QmlShellBridge::identifyParticipantsPreview(const QString& meetingId, bool* cancelled)
{
    if (cancelled) *cancelled = false;
    // Megszakítható progress: a számítás a fő szálon fut, a callback minden lépésnél frissíti
    // a dialógust + processEvents-szel életben tartja a UI-t és figyeli a „Megszakítás”-t.
    QProgressDialog dlg(tr("Résztvevők azonosítása a hang alapján…"), tr("Megszakítás"), 0, 0, nullptr);
    dlg.setWindowTitle(tr("Résztvevők azonosítása"));
    dlg.setWindowModality(Qt::ApplicationModal);
    dlg.setMinimumDuration(0);
    dlg.setAutoClose(false);
    dlg.setAutoReset(false);
    dlg.setValue(0);
    auto progress = [&dlg](int done, int total) -> bool {
        if (total > 0) { dlg.setMaximum(total); dlg.setValue(done); }
        QCoreApplication::processEvents();
        return !dlg.wasCanceled();
    };
    const auto guesses = m_controller->identifyParticipants(meetingId, progress);
    if (dlg.wasCanceled()) {
        if (cancelled) *cancelled = true;
        return {};
    }
    QStringList named;
    int unknown = 0;
    for (const auto& g : guesses) {
        if (g.name.trimmed().isEmpty()) ++unknown;
        else named << g.name.trimmed();
    }
    dlg.reset();
    return participantsSummary(named, unknown, int(guesses.size()));
}

// ============================================================================
// Tanara Cloud — K-01, K-06…K-12, K-15 (a régi MainWindow cloud-ágának megfelelője)
// ============================================================================

void QmlShellBridge::wireCloud()
{
    if (!m_controller->cloudLive())
        return;   // teaser-mód: nincs chip, sáv, hálózat
    tanara::CloudAccount* acc = m_controller->cloud();

    connect(m_controller, &tanara::AppController::cloudCharged, this, &QmlShellBridge::onCloudCharged);
    connect(m_controller, &tanara::AppController::cloudRefunded, this, &QmlShellBridge::onCloudRefunded);
    connect(m_controller, &tanara::AppController::cloudError, this, &QmlShellBridge::onCloudError);
    connect(acc, &tanara::CloudAccount::accountUpdated, this, [this](const tanara::AccountInfo&) {
        m_lastCloudRefresh = QDateTime::currentDateTime();
        refreshCloudChrome();
        // K-15 előzetes: indításkor / frissítéskor, naponta legfeljebb egyszer.
        if (!m_termsOffered && CloudTermsDialog::shouldOfferEarly(m_controller)) {
            m_termsOffered = true;
            if (CloudTermsDialog::offerEarly(m_controller, nullptr))
                cloudToast(tr("Elfogadtad az ÁSZF %1 verzióját.")
                               .arg(m_controller->cloud()->account().terms.acceptedVersion),
                           QString(), false);
        }
    });
    connect(acc, &tanara::CloudAccount::balanceChanged, this,
            [this](const tanara::Money&) { refreshCloudChrome(); });
    connect(acc, &tanara::CloudAccount::loggedIn, this,
            [this]() { refreshCloudChrome(); emit readinessChanged(); });
    connect(acc, &tanara::CloudAccount::loggedOut, this,
            [this]() { refreshCloudChrome(); emit readinessChanged(); });
    connect(acc, &tanara::CloudAccount::modelsUpdated, this, [this]() { emit readinessChanged(); });
    connect(acc, &tanara::CloudAccount::clientTooOldDetected, this, [this](const QString& minClient) {
        refreshCloudChrome();
        emit readinessChanged();
        if (m_tooOldShown) return;   // K-10: modális az első találkozáskor, utána tartós sáv
        m_tooOldShown = true;
        tanara::CloudError e;
        e.kind = tanara::CloudErrorKind::ClientTooOld;
        e.minClient = minClient;
        e.downloadUrl = m_controller->cloud()->downloadUrl();
        QTimer::singleShot(0, this, [this, e]() {
            cloudui::showCloudError(nullptr, m_controller, e, QStringLiteral("account"));
        });
    });
    connect(acc, &tanara::CloudAccount::previousTranscriptionRefunded, this,
            [this](const tanara::Money& refund, const tanara::Money&) {
        cloudToast(tr("Az előző átírás a szolgáltató hibája miatt nem sikerült. "
                      "A díjat (%1) visszaírtuk.")
                       .arg(cloudui::money(refund, tanara::MoneyStyle::Charge)));
    });
    connect(m_controller->settings(), &tanara::SettingsManager::settingsChanged,
            this, &QmlShellBridge::refreshCloudChrome);
    refreshCloudChrome();
}

void QmlShellBridge::windowShown()
{
    if (m_cloudStartupDone)
        return;
    m_cloudStartupDone = true;
    QTimer::singleShot(0, this, &QmlShellBridge::startupCloudChecks);
}

void QmlShellBridge::startupCloudChecks()
{
    if (!m_controller->cloudLive())
        return;
    tanara::CloudAccount* acc = m_controller->cloud();
    // K-01 — csak az első indításkor (élő cloud-módban).
    if (m_controller->settings()->isFirstRun()) {
        CloudModeDialog dlg(m_controller, nullptr);
        dlg.exec();
        if (dlg.choice() == CloudModeDialog::Byo) {
            openSettings(QString());
        } else if (dlg.choice() == CloudModeDialog::Cloud && cloudLogin()) {
            tanara::AppSettings s = m_controller->settings()->settings();
            s.sttProviderId = tanara::cloud::ProviderId;
            s.llmProviderId = tanara::cloud::ProviderId;
            s.sttConfigs[tanara::cloud::ProviderId].type = tanara::cloud::ProviderId;
            s.llmConfigs[tanara::cloud::ProviderId].type = tanara::cloud::ProviderId;
            m_controller->settings()->setSettings(s);
            emit readinessChanged();
        }
    }
    if (!acc->isLoggedIn())
        return;
    acc->refreshAccount();
    acc->fetchModels();
    acc->checkPendingTranscriptions();   // „Az előző átírás … visszaírtuk” (CLI-13)
}

void QmlShellBridge::windowActivated()
{
    // Feltöltés a böngészőben → az ablak újra fókuszt kap → az egyenleg frissül (brief 5.4).
    if (m_controller->cloudLive() && m_controller->cloud()->isLoggedIn()
        && (!m_lastCloudRefresh.isValid()
            || m_lastCloudRefresh.secsTo(QDateTime::currentDateTime()) > 60)) {
        m_lastCloudRefresh = QDateTime::currentDateTime();
        m_controller->cloud()->refreshAccount();
    }
}

void QmlShellBridge::refreshCloudChrome()
{
    if (!m_controller->cloudLive())
        return;
    tanara::CloudAccount* acc = m_controller->cloud();
    const bool cloudUsed = m_controller->usesCloud(tanara::WorkflowStep::Transcribe)
                           || m_controller->usesCloud(tanara::WorkflowStep::Summarize);
    const tanara::AccountInfo a = acc->account();

    // --- egyenleg-chip (K-08) — saját kulcsos módban nincs ---
    m_chipVisible = cloudUsed;
    m_chipTone = QStringLiteral("normal");
    m_chipToolTip.clear();
    if (!acc->isLoggedIn()) {
        m_chipText = tr("Tanara Cloud: nincs bejelentkezve");
        m_chipToolTip = tr("Bejelentkezés a Beállításokban");
    } else if (a.valid) {
        m_chipText = tr("%1 · ≈ %2 Pontos").arg(cloudui::money(a.balance), cloudui::hours(a.hoursAccurate));
        m_chipTone = a.balanceEmpty ? QStringLiteral("danger")
                   : a.lowBalance   ? QStringLiteral("warn") : QStringLiteral("normal");
        m_chipToolTip = tr("Tanara Cloud egyenleg (%1)").arg(tanara::vatLabel(a.vatMode));
    } else {
        m_chipText = tr("Tanara Cloud: …");
    }

    // --- sávok (K-08 / K-10 / notice): szint + szöveg + legfeljebb egy teendő ---
    QVariantList banners;
    auto add = [&banners](const QString& key, const QString& level, const QString& text,
                          const QString& cta, bool closable) {
        banners.append(QVariantMap{{QStringLiteral("key"), key}, {QStringLiteral("level"), level},
                                   {QStringLiteral("text"), text}, {QStringLiteral("cta"), cta},
                                   {QStringLiteral("closable"), closable}});
    };
    if (cloudUsed) {
        if (acc->clientTooOld())   // K-10 tartós sáv
            add(QStringLiteral("tooOld"), QStringLiteral("critical"),
                tr("A Tanara Cloudhoz frissítés kell (legalább %1). A saját kulcsos mód működik.")
                    .arg(acc->minClient()),
                tr("Frissítés most"), false);
        if (acc->isLoggedIn() && a.valid) {
            const QString topupLabel = a.topupAvailable ? tr("Feltöltés") : tr("Írj nekünk");
            if (a.balanceEmpty) {
                add(QStringLiteral("empty"), QStringLiteral("critical"),
                    tr("Elfogyott az egyenleged. A Tanara Cloud feldolgozás a feltöltésig szünetel; "
                       "a saját kulcsos mód továbbra is működik."),
                    topupLabel, false);
            } else if (a.lowBalance) {
                // ✕ → elrejtve a következő küszöbátlépésig (low_balance_since > az elrejtés ideje).
                const QDateTime dismissed = acc->lowBannerDismissedAt();
                if (!dismissed.isValid()
                    || (a.lowBalanceSince.isValid() && a.lowBalanceSince > dismissed))
                    add(QStringLiteral("low"), QStringLiteral("warning"),
                        tr("Kevés az egyenleged: %1 (≈ %2 Pontos átírás).")
                            .arg(cloudui::money(a.balance), cloudui::hours(a.hoursAccurate)),
                        topupLabel, true);
            }
            for (const tanara::Notice& n : a.notices) {   // CLI-18
                if (acc->isNoticeDismissed(n.id)) continue;
                add(QStringLiteral("notice:") + n.id, n.level, n.message,
                    n.url.isEmpty() ? QString() : tr("Részletek"), true);
            }
        }
    }
    m_banners = banners;
    emit cloudChromeChanged();
}

void QmlShellBridge::cloudBannerAction(const QString& key)
{
    tanara::CloudAccount* acc = m_controller->cloud();
    if (key == QLatin1String("tooOld")) {
        cloudui::openUrl(acc->downloadUrl());
    } else if (key == QLatin1String("empty") || key == QLatin1String("low")) {
        cloudui::startTopup(nullptr, m_controller);
    } else if (key.startsWith(QLatin1String("notice:"))) {
        const QString id = key.mid(7);
        for (const tanara::Notice& n : acc->account().notices)
            if (n.id == id && !n.url.isEmpty())
                cloudui::openUrl(n.url);
    }
}

void QmlShellBridge::cloudBannerDismiss(const QString& key)
{
    tanara::CloudAccount* acc = m_controller->cloud();
    if (key == QLatin1String("low"))
        acc->dismissLowBanner();
    else if (key.startsWith(QLatin1String("notice:")))
        acc->dismissNotice(key.mid(7));
    refreshCloudChrome();
}

void QmlShellBridge::openUsageLog()
{
    cloudui::openUrl(m_controller->cloud()->account().usageUrl);
}

void QmlShellBridge::cloudToast(const QString& text, const QString& requestId, bool usageLink)
{
    emit toastRequested(text, requestId, usageLink);
}

void QmlShellBridge::onCloudCharged(const QString&, const QString& kind, const tanara::Money& total,
                                    int calls, const tanara::Money& balance, const QString& vatMode)
{
    const QString sum = cloudui::money(total, tanara::MoneyStyle::Charge);
    const QString bal = balance.isValid() ? cloudui::money(balance) : QStringLiteral("—");
    QString text;
    if (kind == QLatin1String("transcribe"))
        text = tr("Ez az átírás %1 volt. Egyenleg: %2.").arg(sum, bal);
    else if (kind == QLatin1String("complex"))
        text = tr("Az összefoglaló %1 volt (%n rész). Egyenleg: %2.", nullptr, calls).arg(sum, bal);
    else if (kind == QLatin1String("topics"))
        text = tr("A témák kigyűjtése %1 volt. Egyenleg: %2.").arg(sum, bal);
    else
        text = tr("Az összefoglaló %1 volt. Egyenleg: %2.").arg(sum, bal);
    if (!vatMode.isEmpty())
        text += QStringLiteral(" (") + tanara::vatLabel(vatMode) + QLatin1Char(')');
    cloudToast(text);
    refreshCloudChrome();
}

void QmlShellBridge::onCloudRefunded(const QString&, const tanara::Money& refund,
                                     const tanara::Money&, const QString& requestId)
{
    cloudToast(tr("Az átírás a szolgáltató hibája miatt nem sikerült. A díjat (%1) visszaírtuk.")
                   .arg(cloudui::money(refund, tanara::MoneyStyle::Charge)),
               requestId);
    emit readinessChanged();
}

void QmlShellBridge::onCloudError(const QString& meetingId, const QString& kind,
                                  const tanara::CloudError& e, const tanara::Money& charged)
{
    const bool canContinue = kind == QLatin1String("complex");
    const cloudui::ErrorAction a =
        cloudui::showCloudError(nullptr, m_controller, e, kind, charged, canContinue);
    refreshCloudChrome();
    emit readinessChanged();
    if (a == cloudui::ErrorAction::Login) { cloudLogin(); return; }
    if (a != cloudui::ErrorAction::Retry && a != cloudui::ErrorAction::Continue) return;
    // Újra / Folytatás: ugyanaz a lépés — új futás, új X-Tanara-Job-Id (a folytatás csak a
    // hátralévő részekért fizet: a kész téma-elemzések a lemezen vannak).
    emit retryRequested(meetingId, kind);
}

bool QmlShellBridge::confirmCloudEstimate(const QString& meetingId, const QString& task,
                                          const QString& mode)
{
    const tanara::WorkflowStep step = task == QLatin1String("transcribe")
        ? tanara::WorkflowStep::Transcribe : tanara::WorkflowStep::Summarize;
    if (!m_controller->usesCloud(step))
        return true;   // BYO: nincs becslés
    CloudEstimateDialog dlg(m_controller, meetingId, task, mode, nullptr);
    return dlg.exec() == QDialog::Accepted;
}

bool QmlShellBridge::handleCloudBlocker(const tanara::ReadinessResult& r)
{
    if (r.blockerKind == tanara::BlockerKind::Auth && r.providerId == tanara::cloud::ProviderId) {
        cloudLogin();
        return true;
    }
    if (r.blockerKind != tanara::BlockerKind::Cloud)
        return false;
    if (r.fixActionHint == QLatin1String("cloud:topup")) {
        cloudui::startTopup(nullptr, m_controller);
    } else if (r.fixActionHint == QLatin1String("cloud:update")) {
        tanara::CloudError e;
        e.kind = tanara::CloudErrorKind::ClientTooOld;
        e.minClient = m_controller->cloud()->minClient();
        e.downloadUrl = m_controller->cloud()->downloadUrl();
        cloudui::showCloudError(nullptr, m_controller, e, QStringLiteral("account"));
    }
    return true;
}

bool QmlShellBridge::cloudLogin()
{
    CloudLoginDialog dlg(m_controller, nullptr);
    const bool ok = dlg.exec() == QDialog::Accepted;
    refreshCloudChrome();
    emit readinessChanged();
    return ok;
}

} // namespace tanara_gui
