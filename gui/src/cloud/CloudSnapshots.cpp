// Fejlesztői QA-mód: `tanara --ui-snapshots <mappa>` — a Tanara Cloud képernyőit (K-01…K-15,
// MKT-02) sorban megnyitja, PNG-be menti (QWidget::grab), majd kilép. Kattintgatás nélküli
// vizuális ellenőrzéshez (pl. QT_QPA_PLATFORM=offscreen + mock-gateway). A kimenet nem
// kerül a repóba.
#include "cloud/CloudSnapshots.h"
#include "cloud/CloudAccountPanel.h"
#include "cloud/CloudEstimateDialog.h"
#include "cloud/CloudLoginDialog.h"
#include "cloud/CloudModeDialog.h"
#include "cloud/CloudModelPickerDialog.h"
#include "cloud/CloudTermsDialog.h"
#include "cloud/CloudUi.h"
#include "cloud/CloudWaitlistPanel.h"
#include "MainWindow.h"
#include "SettingsDialog.h"

#include "tanara/AppController.h"
#include "tanara/cloud/CloudAccount.h"
#include "tanara/store/MeetingStore.h"

#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QTableView>
#include <QTimer>
#include <QWidget>

namespace tanara_gui {

using namespace tanara;

namespace {
void wait(int ms) { QEventLoop l; QTimer::singleShot(ms, &l, &QEventLoop::quit); l.exec(); }
void save(QWidget* w, const QString& dir, const QString& name, bool fit = true)
{
    if (fit) w->adjustSize();
    w->grab().save(QDir(dir).filePath(name + QStringLiteral(".png")));
}
// Modális dialógus (exec) lefotózása: a megnyitás után elkapjuk és bezárjuk.
void snapModal(const QString& dir, const QString& name, int delayMs = 400)
{
    QTimer::singleShot(delayMs, [dir, name]() {
        if (QWidget* w = QApplication::activeModalWidget()) {
            save(w, dir, name);
            if (auto* d = qobject_cast<QDialog*>(w)) d->reject(); else w->close();
        }
    });
}
} // namespace

int runCloudSnapshots(AppController& app, MainWindow& window, const QString& dir)
{
    QDir().mkpath(dir);
    window.resize(1100, 760);
    window.show();
    wait(800);
    if (auto* table = window.findChild<QTableView*>()) table->selectRow(0);   // State A / fülek
    wait(1200);
    save(&window, dir, QStringLiteral("main"), false);

    {   // Beállítások → Tanara Cloud fül (teaser: MKT-02 / élő: K-02)
        SettingsDialog dlg(&app, &window);
        dlg.showCloudTab();
        dlg.resize(820, 760);
        dlg.show();
        wait(1200);
        save(&dlg, dir, app.cloudLive() ? QStringLiteral("k02-settings-cloud") : QStringLiteral("mkt02-settings-teaser"), false);
        dlg.close();
    }
    if (!app.cloudLive()) return 0;

    {   CloudModeDialog dlg(&app, &window); dlg.show(); wait(300); save(&dlg, dir, QStringLiteral("k01-mode")); }
    {   CloudLoginDialog dlg(&app, &window); dlg.show(); wait(1500); save(&dlg, dir, QStringLiteral("k03-login")); dlg.reject(); }
    {   CloudModelPickerDialog dlg(&app, QStringLiteral("stt"), &window); dlg.show(); wait(800); save(&dlg, dir, QStringLiteral("k05-expert")); }
    const QVector<Meeting> ms = app.store()->loadAll();
    if (!ms.isEmpty()) {
        CloudEstimateDialog dlg(&app, ms.first().id, QStringLiteral("transcribe"), QString(), &window);
        dlg.show(); wait(1500); save(&dlg, dir, QStringLiteral("k06-estimate-transcribe"));
        CloudEstimateDialog dlg2(&app, ms.first().id, QStringLiteral("summarize"), QStringLiteral("complex"), &window);
        dlg2.show(); wait(1500); save(&dlg2, dir, QStringLiteral("k06-estimate-summary"));
    }
    {
        CloudTermsDialog dlg(&app, false, QStringLiteral("2026-11-01"), QStringLiteral("https://example.com/terms"),
                             QDateTime::currentDateTimeUtc().addDays(10), QStringLiteral("Új visszatérítési szabályok."), &window);
        dlg.show(); wait(300); save(&dlg, dir, QStringLiteral("k15-terms-early"));
    }

    // Hiba-dialógusok szintetikus hibákkal (K-09 … K-12).
    auto err = [](CloudErrorKind k, int status, const QString& code) {
        CloudError e; e.kind = k; e.httpStatus = status; e.code = code; e.requestId = QStringLiteral("req_01J9Z6");
        e.message = QStringLiteral("…"); return e; };
    struct Case { QString name; CloudError e; QString kind; Money charged; };
    QVector<Case> cases;
    { CloudError e = err(CloudErrorKind::InsufficientBalance, 402, "insufficient_balance");
      e.needed = { 460000, "USD" }; e.balance = { 200000, "USD" }; e.neededBasis = "charge"; e.contactUrl = "https://example.com/contact";
      cases.append({ "k09-402-charge", e, "transcribe", {} });
      e.neededBasis = "hold"; e.needed = { 620000, "USD" };
      cases.append({ "k09-402-hold-partial", e, "complex", { 210000, "USD" } }); }
    { CloudError e = err(CloudErrorKind::ClientTooOld, 426, "client_too_old"); e.minClient = "0.2.0"; e.downloadUrl = "https://example.com/dl";
      cases.append({ "k10-426", e, "transcribe", {} }); }
    { CloudError e = err(CloudErrorKind::Maintenance, 503, "maintenance"); e.message = QStringLiteral("Tervezett karbantartás.");
      e.windowStart = QDateTime::currentDateTime(); e.windowEnd = e.windowStart.addSecs(1800); e.retryAfterSec = 1500;
      cases.append({ "k11-503-maintenance", e, "transcribe", {} }); }
    cases.append({ "k11-503-upstream", err(CloudErrorKind::Upstream, 503, "upstream_unavailable"), "summary", {} });
    { CloudError e = err(CloudErrorKind::RateLimited, 429, "rate_limited"); e.retryAfterSec = 30;
      cases.append({ "k11-429-rate", e, "transcribe", {} }); }
    { CloudError e = err(CloudErrorKind::SpendLimit, 429, "spend_limit_reached"); e.limitPeriod = "daily"; e.limitAmount = { 5000000, "USD" };
      e.limitResetsAt = QDateTime::currentDateTime().addSecs(3600); e.settingsUrl = "https://example.com/keys";
      cases.append({ "k11-429-spend", e, "transcribe", {} }); }
    cases.append({ "k11-401", err(CloudErrorKind::Unauthorized, 401, "unauthorized"), "transcribe", {} });
    { CloudError e = err(CloudErrorKind::Suspended, 403, "account_suspended"); e.suspensionReason = "admin";
      cases.append({ "k11-403-suspended", e, "transcribe", {} });
      e.suspensionReason = "payment_dispute"; cases.append({ "k11-403-dispute", e, "transcribe", {} }); }
    { CloudError e = err(CloudErrorKind::TermsRequired, 403, "terms_acceptance_required"); e.termsVersion = "2026-11-01";
      cases.append({ "k15-terms-required", e, "summary", {} }); }
    cases.append({ "k12-500", err(CloudErrorKind::Internal, 500, "internal_error"), "summary", {} });
    cases.append({ "k12-500-partial", err(CloudErrorKind::Internal, 500, "internal_error"), "complex", { 210000, "USD" } });
    { CloudError e; e.kind = CloudErrorKind::Network; cases.append({ "k12-network", e, "transcribe", {} }); }
    for (const Case& c : cases) {
        snapModal(dir, c.name);
        cloudui::showCloudError(&window, &app, c.e, c.kind, c.charged, c.kind == QLatin1String("complex"));
        wait(100);
    }
    return 0;
}

} // namespace tanara_gui
