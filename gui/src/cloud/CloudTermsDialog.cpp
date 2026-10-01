#include "cloud/CloudTermsDialog.h"
#include "cloud/CloudUi.h"

#include "tanara/AppController.h"
#include "tanara/cloud/CloudAccount.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>

namespace tanara_gui {

using namespace tanara;

CloudTermsDialog::CloudTermsDialog(AppController* app, bool mandatory, const QString& version,
                                   const QString& url, const QDateTime& effective,
                                   const QString& summary, QWidget* parent)
    : QDialog(parent), m_app(app), m_version(version)
{
    // Dátum (brief 7.4): HU „2026. 11. 01.”, EN „Nov 1, 2026”. A magyar ragozás
    // („-tól/-től”) miatt a címben semleges alak: „Új ÁSZF, hatályos: …”.
    const QDate d = effective.toLocalTime().date();
    const QString date = !effective.isValid() ? QString()
        : cloudui::lang() == QLatin1String("hu") ? d.toString(QStringLiteral("yyyy. MM. dd."))
                                                 : QLocale(QLocale::English).toString(d, QStringLiteral("MMM d, yyyy"));
    setWindowTitle(mandatory ? tr("Az ÁSZF megváltozott")
                             : tr("Új ÁSZF, hatályos: %1").arg(date));
    auto* lay = new QVBoxLayout(this);

    auto* title = new QLabel(mandatory ? tr("A feldolgozáshoz el kell fogadnod az új ÁSZF-et.")
                                       : tr("Új ÁSZF, hatályos: %1").arg(date), this);
    title->setStyleSheet(QStringLiteral("QLabel { font-weight: bold; font-size: 15px; }"));
    title->setWordWrap(true);
    lay->addWidget(title);

    if (!summary.isEmpty()) {
        auto* sum = new QLabel(summary, this);
        sum->setWordWrap(true);
        lay->addWidget(sum);
    }
    auto* body = new QLabel(mandatory
        ? tr("A futó feldolgozás nem szakad meg. A fiókod, a modellek listája és a kijelentkezés "
             "addig is működik.")
        : tr("Elfogadhatod most, vagy később. Hatálybalépés után a Tanara Cloud használatához el "
             "kell fogadnod."), this);
    body->setWordWrap(true);
    lay->addWidget(body);

    auto* del = new QLabel(QStringLiteral("<a href=\"#\">%1</a>").arg(tr("Nem fogadom el, fiók törlése")), this);
    del->setToolTip(tr("A fiók törlése a weben (Tanara Cloud dashboard) történik."));
    connect(del, &QLabel::linkActivated, this, [this]() {
        cloudui::openUrl(m_app->cloud()->account().dashboardUrl);
    });
    lay->addWidget(del);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setVisible(false);
    lay->addWidget(m_status);

    auto* box = new QDialogButtonBox(this);
    auto* view = box->addButton(tr("Megtekintés"), QDialogButtonBox::ActionRole);
    m_accept = box->addButton(tr("Elfogadom"), QDialogButtonBox::AcceptRole);
    auto* later = box->addButton(mandatory ? tr("Mégse") : tr("Később"), QDialogButtonBox::RejectRole);
    m_accept->setDefault(true);
    lay->addWidget(box);

    connect(view, &QPushButton::clicked, this, [url]() { cloudui::openUrl(url); });
    connect(later, &QPushButton::clicked, this, [this, mandatory]() {
        if (!mandatory) m_app->cloud()->postponeTerms();
        reject();
    });
    connect(m_accept, &QPushButton::clicked, this, [this]() {
        m_accept->setEnabled(false);
        m_status->setText(tr("Elfogadás…"));
        m_status->setVisible(true);
        m_app->cloud()->acceptTerms(m_version);
    });
    connect(m_app->cloud(), &CloudAccount::termsAccepted, this, [this](const TermsStatus&) { accept(); });
    connect(m_app->cloud(), &CloudAccount::termsFailed, this, [this](const CloudError& e) {
        m_accept->setEnabled(true);
        m_status->setText(cloudui::shortErrorText(e));
    });
}

bool CloudTermsDialog::shouldOfferEarly(AppController* app)
{
    if (!app || !app->cloudLive() || !app->cloud()->isLoggedIn()) return false;
    const TermsStatus t = app->cloud()->account().terms;
    if (!t.needsEarlyAcceptance()) return false;
    const QDateTime postponed = app->cloud()->termsPostponedAt();
    return !postponed.isValid() || postponed.date() < QDate::currentDate();   // naponta legfeljebb egyszer
}

bool CloudTermsDialog::offerEarly(AppController* app, QWidget* parent)
{
    const TermsStatus t = app->cloud()->account().terms;
    CloudTermsDialog dlg(app, false, t.upcomingVersion, t.upcomingUrl, t.upcomingEffectiveFrom,
                         t.upcomingSummary, parent);
    return dlg.exec() == QDialog::Accepted;
}

bool CloudTermsDialog::requireFromError(AppController* app, const CloudError& e, QWidget* parent)
{
    const TermsStatus t = app->cloud()->account().terms;
    const QString version = !e.termsVersion.isEmpty() ? e.termsVersion : t.currentVersion;
    const QString url = !e.termsUrl.isEmpty() ? e.termsUrl : t.currentUrl;
    CloudTermsDialog dlg(app, true, version, url, QDateTime(), QString(), parent);
    return dlg.exec() == QDialog::Accepted;
}

} // namespace tanara_gui
