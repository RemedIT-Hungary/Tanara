#include "cloud/CloudUi.h"
#include "cloud/CloudTermsDialog.h"

#include "tanara/AppController.h"
#include "tanara/Localization.h"
#include "tanara/Types.h"
#include "tanara/cloud/CloudAccount.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPointer>
#include <QPushButton>
#include <QStyle>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace tanara_gui::cloudui {

using namespace tanara;

QString lang() { return activeUiLanguage(); }

QString money(const Money& m, MoneyStyle style) { return formatMoney(m, style, lang()); }

QString hours(double h) { return formatHours(h, lang()); }

QString tierName(const QString& tier)
{
    if (tier == QLatin1String("fast")) return QCoreApplication::translate("CloudUi", "Gyors");
    if (tier == QLatin1String("accurate")) return QCoreApplication::translate("CloudUi", "Pontos");
    return QCoreApplication::translate("CloudUi", "Expert");
}

void mute(QWidget* w)
{
    if (w) w->setForegroundRole(QPalette::PlaceholderText);
}

void openUrl(const QString& url)
{
    if (!url.isEmpty()) QDesktopServices::openUrl(QUrl(url));
}

QWidget* requestIdRow(const QString& requestId, QWidget* parent)
{
    auto* w = new QWidget(parent);
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 4, 0, 0);
    auto* row = new QHBoxLayout();
    auto* id = new QLabel(QCoreApplication::translate("CloudUi", "Hibaazonosító: <b>%1</b>").arg(requestId.toHtmlEscaped()), w);
    id->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* copy = new QPushButton(QCoreApplication::translate("CloudUi", "Másolás"), w);
    QObject::connect(copy, &QPushButton::clicked, w, [requestId, copy]() {
        QApplication::clipboard()->setText(requestId);
        copy->setText(QCoreApplication::translate("CloudUi", "Másolva ✓"));
    });
    row->addWidget(id);
    row->addWidget(copy);
    row->addStretch(1);
    v->addLayout(row);
    auto* note = new QLabel(QCoreApplication::translate("CloudUi",
        "Ha írsz nekünk, küldd el ezt is. A meeting tartalmát nem látjuk."), w);
    note->setWordWrap(true);
    mute(note);
    v->addWidget(note);
    return w;
}

QString shortErrorText(const CloudError& e)
{
    switch (e.kind) {
    case CloudErrorKind::Network:       return QCoreApplication::translate("CloudUi", "Nem értük el a szervert. Ellenőrizd az internetkapcsolatot.");
    case CloudErrorKind::Unauthorized:  return QCoreApplication::translate("CloudUi", "Ezt az eszközt leválasztották. Jelentkezz be újra.");
    case CloudErrorKind::Suspended:
        return e.suspensionReason == QLatin1String("payment_dispute")
            ? QCoreApplication::translate("CloudUi", "A fiókod fizetési vita miatt fel van függesztve. Részletek a weben.")
            : QCoreApplication::translate("CloudUi", "A fiókod fel van függesztve. Írj a supportnak.");
    case CloudErrorKind::ClientTooOld:  return QCoreApplication::translate("CloudUi", "Frissítsd a Tanarát (legalább %1; neked %2 van).").arg(e.minClient, libraryVersion());
    case CloudErrorKind::Maintenance:   return QCoreApplication::translate("CloudUi", "Karbantartás miatt a Tanara Cloud most nem elérhető.");
    case CloudErrorKind::Upstream:      return QCoreApplication::translate("CloudUi", "A feldolgozó szolgáltatás átmenetileg nem elérhető.");
    case CloudErrorKind::RateLimited:   return QCoreApplication::translate("CloudUi", "Túl sok kérés érkezett. Próbáld újra %1 mp múlva.").arg(qMax(1, e.retryAfterSec));
    case CloudErrorKind::SpendLimit:    return QCoreApplication::translate("CloudUi", "Elérted ennek az eszköznek a költési limitjét.");
    case CloudErrorKind::TermsRequired: return QCoreApplication::translate("CloudUi", "A feldolgozáshoz el kell fogadnod az új ÁSZF-et.");
    case CloudErrorKind::InsufficientBalance: return QCoreApplication::translate("CloudUi", "Nincs elég egyenleg.");
    case CloudErrorKind::DisposableEmail: return QCoreApplication::translate("CloudUi", "Eldobható e-mail címmel nem lehet feliratkozni.");
    default: break;
    }
    return e.message.isEmpty() ? QCoreApplication::translate("CloudUi", "Hiba történt.") : e.message;
}

namespace {

// Egy közös hiba-dialógus váz: ikon + cím + szöveg (+ „Nem terheltünk semmit.”) + hibaazonosító.
struct Dlg {
    QDialog d;
    QVBoxLayout* lay = nullptr;
    QDialogButtonBox* box = nullptr;
    ErrorAction result = ErrorAction::None;

    Dlg(QWidget* parent, const QString& windowTitle, QStyle::StandardPixmap icon, const QString& title,
        const QString& text) : d(parent)
    {
        d.setWindowTitle(windowTitle);
        d.setMinimumWidth(460);
        auto* h = new QHBoxLayout(&d);
        auto* ic = new QLabel(&d);
        ic->setPixmap(d.style()->standardIcon(icon).pixmap(40, 40));
        ic->setAlignment(Qt::AlignTop);
        h->addWidget(ic);
        lay = new QVBoxLayout();
        h->addLayout(lay, 1);
        auto* t = new QLabel(title, &d);
        t->setWordWrap(true);
        t->setStyleSheet(QStringLiteral("QLabel { font-weight: bold; font-size: 15px; }"));
        lay->addWidget(t);
        if (!text.isEmpty()) {
            auto* b = new QLabel(text, &d);
            b->setWordWrap(true);
            b->setTextInteractionFlags(Qt::TextSelectableByMouse);
            lay->addWidget(b);
        }
        box = new QDialogButtonBox(&d);
    }
    void addLine(const QString& s, bool muted = false)
    {
        auto* l = new QLabel(s, &d);
        l->setWordWrap(true);
        if (muted) mute(l);
        lay->addWidget(l);
    }
    QPushButton* button(const QString& label, ErrorAction a, bool primary = false)
    {
        auto* b = box->addButton(label, primary ? QDialogButtonBox::AcceptRole : QDialogButtonBox::ActionRole);
        if (primary) b->setDefault(true);
        QObject::connect(b, &QPushButton::clicked, &d, [this, a]() { result = a; d.accept(); });
        return b;
    }
    QPushButton* link(const QString& label, const QString& url, bool primary = false)
    {
        auto* b = box->addButton(label, primary ? QDialogButtonBox::AcceptRole : QDialogButtonBox::ActionRole);
        if (primary) b->setDefault(true);
        QObject::connect(b, &QPushButton::clicked, &d, [this, url]() { openUrl(url); d.accept(); });
        return b;
    }
    ErrorAction exec(const QString& requestId)
    {
        if (!requestId.isEmpty()) lay->addWidget(requestIdRow(requestId, &d));
        auto* close = box->addButton(QCoreApplication::translate("CloudUi", "Bezárás"), QDialogButtonBox::RejectRole);
        QObject::connect(close, &QPushButton::clicked, &d, &QDialog::reject);
        lay->addStretch(1);
        lay->addWidget(box);
        d.exec();
        return result;
    }
};

QString localTime(const QDateTime& t)
{
    return t.toLocalTime().toString(QStringLiteral("HH:mm"));
}

} // namespace

ErrorAction showCloudError(QWidget* parent, AppController* app, const CloudError& e, const QString& kind,
                           const Money& chargedSoFar, bool canContinue)
{
    const bool partial = chargedSoFar.isValid() && chargedSoFar.micros > 0;
    const AccountInfo acc = app ? app->cloud()->account() : AccountInfo{};
    // „Nem terheltünk semmit.” csak ha valóban semmi nem terhelődött (kiegészítés-2 1.12).
    const QString noCharge = QCoreApplication::translate("CloudUi", "Nem terheltünk semmit.");
    const QString partialText = QCoreApplication::translate("CloudUi", "Az eddig elkészült részek díja: %1. Folytathatod, ekkor csak a "
                                  "hátralévő részekért fizetsz.").arg(money(chargedSoFar, MoneyStyle::Charge));
    auto chargeLine = [&](Dlg& d) {
        if (partial) d.addLine(partialText);
        else if (e.kind != CloudErrorKind::Network) d.addLine(noCharge);
    };

    switch (e.kind) {
    case CloudErrorKind::TermsRequired:
        return (app && CloudTermsDialog::requireFromError(app, e, parent)) ? ErrorAction::Retry : ErrorAction::None;

    case CloudErrorKind::InsufficientBalance: {   // K-09
        const QString text = e.neededBasis == QLatin1String("hold")
            ? QCoreApplication::translate("CloudUi", "Ehhez a lépéshez legalább %1 fedezet kell az egyenlegeden. A tényleges díj ennél kevesebb lehet. "
                "Az egyenleged: %2.").arg(money(e.needed, MoneyStyle::Charge), money(e.balance))
            : QCoreApplication::translate("CloudUi", "Ehhez a lépéshez %1 kell, az egyenleged %2.").arg(money(e.needed, MoneyStyle::Charge), money(e.balance));
        Dlg d(parent, QCoreApplication::translate("CloudUi", "Nincs elég egyenleg"), QStyle::SP_MessageBoxWarning, QCoreApplication::translate("CloudUi", "Nincs elég egyenleg"), text);
        chargeLine(d);
        if (!acc.vatMode.isEmpty()) d.addLine(vatLabel(acc.vatMode), true);
        const bool online = !e.topupUrl.isEmpty() || acc.topupAvailable;
        if (online) d.button(QCoreApplication::translate("CloudUi", "Egyenleg feltöltése"), ErrorAction::Topup, true);
        else {
            d.addLine(QCoreApplication::translate("CloudUi", "A feltöltéshez írj nekünk."));
            d.link(QCoreApplication::translate("CloudUi", "Írj nekünk"), !e.contactUrl.isEmpty() ? e.contactUrl : acc.contactUrl, true);
        }
        if (partial && canContinue) d.button(QCoreApplication::translate("CloudUi", "Folytatás"), ErrorAction::Continue);
        const ErrorAction a = d.exec(e.requestId);
        if (a == ErrorAction::Topup) startTopup(parent, app);
        return a;
    }

    case CloudErrorKind::ClientTooOld: {   // K-10
        Dlg d(parent, QCoreApplication::translate("CloudUi", "Frissítés szükséges"), QStyle::SP_MessageBoxCritical,
              QCoreApplication::translate("CloudUi", "Frissítsd a Tanarát a Tanara Cloudhoz"),
              QCoreApplication::translate("CloudUi", "Legalább %1 kell; neked %2 van. A saját kulcsos mód addig is működik.")
                  .arg(e.minClient.isEmpty() && app ? app->cloud()->minClient() : e.minClient, libraryVersion()));
        const QString url = !e.downloadUrl.isEmpty() ? e.downloadUrl : (app ? app->cloud()->downloadUrl() : QString());
        if (!url.isEmpty()) d.link(QCoreApplication::translate("CloudUi", "Letöltés"), url, true);
        return d.exec(QString());
    }

    case CloudErrorKind::Maintenance: {   // K-11
        QString text = QCoreApplication::translate("CloudUi", "Karbantartás miatt a Tanara Cloud most nem elérhető.");
        if (!e.message.isEmpty()) text += QLatin1Char(' ') + e.message;
        if (e.windowStart.isValid() && e.windowEnd.isValid())
            text += QLatin1Char(' ') + QCoreApplication::translate("CloudUi", "Tervezett karbantartás %1–%2 között.").arg(localTime(e.windowStart), localTime(e.windowEnd));
        Dlg d(parent, QCoreApplication::translate("CloudUi", "Karbantartás"), QStyle::SP_MessageBoxWarning, QCoreApplication::translate("CloudUi", "Karbantartás"), text);
        chargeLine(d);
        if (e.retryAfterSec > 0)
            d.addLine(QCoreApplication::translate("CloudUi", "Próbáld újra kb. %1 perc múlva.").arg(qMax(1, (e.retryAfterSec + 59) / 60)), true);
        d.button(QCoreApplication::translate("CloudUi", "Újra"), ErrorAction::Retry);
        return d.exec(e.requestId);
    }
    case CloudErrorKind::Upstream: {
        Dlg d(parent, QCoreApplication::translate("CloudUi", "A szolgáltatás átmenetileg nem elérhető"), QStyle::SP_MessageBoxWarning,
              QCoreApplication::translate("CloudUi", "A szolgáltatás átmenetileg nem elérhető"),
              QCoreApplication::translate("CloudUi", "A feldolgozó szolgáltatás átmenetileg nem elérhető. Próbáld újra később."));
        chargeLine(d);
        if (partial && canContinue) d.button(QCoreApplication::translate("CloudUi", "Folytatás"), ErrorAction::Continue, true);
        else d.button(QCoreApplication::translate("CloudUi", "Újra"), ErrorAction::Retry, true);
        return d.exec(e.requestId);
    }
    case CloudErrorKind::RateLimited: {   // CLI-19
        const int secs = qMax(1, e.retryAfterSec);
        Dlg d(parent, QCoreApplication::translate("CloudUi", "Túl sok kérés"), QStyle::SP_MessageBoxInformation, QCoreApplication::translate("CloudUi", "Túl sok kérés"),
              QCoreApplication::translate("CloudUi", "Túl sok kérés érkezett. Próbáld újra %1 mp múlva.").arg(secs));
        chargeLine(d);
        QPushButton* retry = d.button(QCoreApplication::translate("CloudUi", "Újra (%1)").arg(secs), partial && canContinue ? ErrorAction::Continue : ErrorAction::Retry);
        retry->setEnabled(false);
        auto* timer = new QTimer(&d.d);
        auto left = std::make_shared<int>(secs);
        QObject::connect(timer, &QTimer::timeout, &d.d, [retry, timer, left]() {
            if (--(*left) <= 0) { timer->stop(); retry->setEnabled(true);
                                  retry->setText(QCoreApplication::translate("CloudUi", "Újra")); }
            else retry->setText(QCoreApplication::translate("CloudUi", "Újra (%1)").arg(*left));
        });
        timer->start(1000);
        return d.exec(e.requestId);
    }
    case CloudErrorKind::SpendLimit: {
        QString text = QCoreApplication::translate("CloudUi", "Elérted ennek az eszköznek a költési limitjét.");
        if (e.limitAmount.isValid())
            text += QLatin1Char(' ') + (e.limitPeriod == QLatin1String("monthly")
                ? QCoreApplication::translate("CloudUi", "Havi limit: %1.") : QCoreApplication::translate("CloudUi", "Napi limit: %1.")).arg(money(e.limitAmount));
        if (e.limitResetsAt.isValid())
            text += QLatin1Char(' ') + QCoreApplication::translate("CloudUi", "Újraindul: %1.").arg(e.limitResetsAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")));
        Dlg d(parent, QCoreApplication::translate("CloudUi", "Költési limit"), QStyle::SP_MessageBoxInformation, QCoreApplication::translate("CloudUi", "Költési limit"), text);
        chargeLine(d);
        d.link(QCoreApplication::translate("CloudUi", "Beállítások a weben"), !e.settingsUrl.isEmpty() ? e.settingsUrl : acc.dashboardUrl, true);
        return d.exec(e.requestId);
    }
    case CloudErrorKind::Unauthorized: {
        Dlg d(parent, QCoreApplication::translate("CloudUi", "Az eszköz le lett választva"), QStyle::SP_MessageBoxCritical,
              QCoreApplication::translate("CloudUi", "Az eszköz le lett választva"),
              QCoreApplication::translate("CloudUi", "Ezt az eszközt leválasztották a fiókodról. Jelentkezz be újra."));
        chargeLine(d);
        d.button(QCoreApplication::translate("CloudUi", "Bejelentkezés"), ErrorAction::Login, true);
        return d.exec(e.requestId);
    }
    case CloudErrorKind::Suspended: {
        const bool dispute = e.suspensionReason == QLatin1String("payment_dispute");
        Dlg d(parent, QCoreApplication::translate("CloudUi", "Fiók felfüggesztve"), QStyle::SP_MessageBoxCritical, QCoreApplication::translate("CloudUi", "Fiók felfüggesztve"),
              dispute ? QCoreApplication::translate("CloudUi", "A fiókod fizetési vita miatt fel van függesztve. Részletek a weben.")
                      : QCoreApplication::translate("CloudUi", "A Tanara Cloud fiókod fel van függesztve. Kérdés esetén írj a supportnak."));
        chargeLine(d);
        if (dispute) d.link(QCoreApplication::translate("CloudUi", "Megnyitás a weben"), acc.dashboardUrl, true);
        else d.link(QCoreApplication::translate("CloudUi", "Support"), !e.supportUrl.isEmpty() ? e.supportUrl : acc.contactUrl, true);
        return d.exec(e.requestId);
    }
    case CloudErrorKind::Network: {   // K-12, azonosító nélkül
        Dlg d(parent, QCoreApplication::translate("CloudUi", "Nincs kapcsolat"), QStyle::SP_MessageBoxWarning, QCoreApplication::translate("CloudUi", "Nem értük el a szervert"),
              QCoreApplication::translate("CloudUi", "Ellenőrizd az internetkapcsolatot, és próbáld újra."));
        if (partial) d.addLine(partialText);
        d.button(partial && canContinue ? QCoreApplication::translate("CloudUi", "Folytatás") : QCoreApplication::translate("CloudUi", "Újra"),
                 partial && canContinue ? ErrorAction::Continue : ErrorAction::Retry, true);
        return d.exec(QString());
    }
    default: break;
    }

    // K-12 — általános hiba hibaazonosítóval (500 internal_error, 400, egyéb).
    QString title = kind == QLatin1String("transcribe") ? QCoreApplication::translate("CloudUi", "Az átírás nem készült el")
                  : kind == QLatin1String("estimate")   ? QCoreApplication::translate("CloudUi", "A becslés nem készült el")
                  : partial                             ? QCoreApplication::translate("CloudUi", "Az összefoglaló nem készült el teljesen")
                                                        : QCoreApplication::translate("CloudUi", "Az összefoglaló nem készült el");
    Dlg d(parent, QCoreApplication::translate("CloudUi", "Hiba történt"), QStyle::SP_MessageBoxCritical, title,
          e.message.isEmpty() ? QCoreApplication::translate("CloudUi", "A feldolgozás hibával leállt.") : e.message);
    chargeLine(d);
    if (partial && canContinue) d.button(QCoreApplication::translate("CloudUi", "Folytatás"), ErrorAction::Continue, true);
    d.link(QCoreApplication::translate("CloudUi", "Support"), !e.supportUrl.isEmpty() ? e.supportUrl : acc.contactUrl);
    return d.exec(e.requestId);
}

void startTopup(QWidget* parent, AppController* app)
{
    if (!app) return;
    CloudAccount* acc = app->cloud();
    const AccountInfo a = acc->account();
    if (a.valid && !a.topupAvailable) {   // P0: nincs online feltöltés → „Írj nekünk”
        openUrl(a.contactUrl);
        return;
    }
    QPointer<QWidget> p(parent);
    auto conns = std::make_shared<QList<QMetaObject::Connection>>();
    auto done = [conns]() { for (const auto& c : *conns) QObject::disconnect(c); };
    conns->append(QObject::connect(acc, &CloudAccount::topupLinkReady, acc, [done](const QString& url) {
        done();
        openUrl(url);
    }));
    conns->append(QObject::connect(acc, &CloudAccount::topupFailed, acc, [done, p, app](const CloudError& e) {
        done();
        if (e.kind == CloudErrorKind::TopupUnavailable) { openUrl(e.contactUrl); return; }
        showCloudError(p, app, e, QStringLiteral("topup"));
    }));
    acc->createTopupLink();
}

} // namespace tanara_gui::cloudui
