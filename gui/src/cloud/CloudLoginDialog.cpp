#include "cloud/CloudLoginDialog.h"
#include "cloud/CloudUi.h"

#include "tanara/AppController.h"
#include "tanara/cloud/CloudAccount.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace tanara_gui {

using namespace tanara;

CloudLoginDialog::CloudLoginDialog(AppController* app, QWidget* parent)
    : QDialog(parent), m_app(app)
{
    setWindowTitle(tr("Bejelentkezés a Tanara Cloudba"));
    setMinimumWidth(460);
    auto* lay = new QVBoxLayout(this);

    m_intro = new QLabel(tr("Megnyitottuk a böngészőt. Ellenőrizd, hogy ugyanez a kód látszik-e, és hagyd jóvá."), this);
    m_intro->setWordWrap(true);
    lay->addWidget(m_intro);

    m_codeLabel = new QLabel(QStringLiteral("—"), this);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPixelSize(34);
    mono.setBold(true);
    m_codeLabel->setFont(mono);
    m_codeLabel->setAlignment(Qt::AlignCenter);
    m_codeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lay->addWidget(m_codeLabel);

    m_wait = new QLabel(tr("Kód kérése…"), this);
    m_wait->setAlignment(Qt::AlignCenter);
    lay->addWidget(m_wait);

    m_state = new QLabel(this);
    m_state->setWordWrap(true);
    m_state->setAlignment(Qt::AlignCenter);
    m_state->setVisible(false);
    lay->addWidget(m_state);

    auto* row = new QHBoxLayout();
    m_copy = new QPushButton(tr("Kód másolása"), this);
    m_reopen = new QPushButton(tr("Böngésző megnyitása újra"), this);
    m_retry = new QPushButton(this);
    m_retry->setVisible(false);
    row->addStretch(1);
    row->addWidget(m_copy);
    row->addWidget(m_reopen);
    row->addWidget(m_retry);
    row->addStretch(1);
    lay->addLayout(row);

    // A böngésző nem nyílt meg → kézi út (mindig látszik, halványan).
    m_fallback = new QWidget(this);
    auto* fl = new QVBoxLayout(m_fallback);
    fl->setContentsMargins(0, 8, 0, 0);
    m_fallbackUri = new QLabel(m_fallback);
    m_fallbackUri->setWordWrap(true);
    m_fallbackUri->setTextInteractionFlags(Qt::TextBrowserInteraction);
    m_fallbackUri->setOpenExternalLinks(true);
    cloudui::mute(m_fallbackUri);
    fl->addWidget(m_fallbackUri);
    lay->addWidget(m_fallback);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    box->button(QDialogButtonBox::Cancel)->setText(tr("Mégse"));
    connect(box, &QDialogButtonBox::rejected, this, &CloudLoginDialog::reject);
    lay->addWidget(box);

    m_timer = new QTimer(this);
    m_timer->setInterval(1000);
    connect(m_timer, &QTimer::timeout, this, &CloudLoginDialog::tick);

    connect(m_copy, &QPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(m_code.userCode);
        m_copy->setText(tr("Másolva ✓"));
    });
    connect(m_reopen, &QPushButton::clicked, this, [this]() {
        QDesktopServices::openUrl(QUrl(m_code.verificationUriComplete));
    });
    connect(m_retry, &QPushButton::clicked, this, &CloudLoginDialog::start);

    CloudAccount* acc = m_app->cloud();
    connect(acc, &CloudAccount::deviceCodeReady, this, &CloudLoginDialog::onCode);
    connect(acc, &CloudAccount::deviceFlowSucceeded, this, [this](const QString& email) {
        m_done = true;
        m_timer->stop();
        setState(QStringLiteral("✓"), tr("Sikeres bejelentkezés: %1").arg(email), QString());
        m_copy->setVisible(false);
        m_reopen->setVisible(false);
        QTimer::singleShot(1200, this, &QDialog::accept);
    });
    connect(acc, &CloudAccount::deviceFlowFailed, this, [this](const CloudError& e) {
        m_timer->stop();
        if (e.kind == CloudErrorKind::ClientTooOld) {
            cloudui::showCloudError(this, m_app, e, QStringLiteral("login"));
            QDialog::reject();
            return;
        }
        if (e.code == QLatin1String("access_denied"))
            setState(QStringLiteral("✕"), tr("A kapcsolódást elutasítottad a böngészőben."), tr("Újrapróbálom"));
        else if (e.code == QLatin1String("expired_token") || e.code == QLatin1String("invalid_device_code"))
            setState(QStringLiteral("▲"), tr("A kód lejárt."), tr("Új kód kérése"));
        else
            setState(QStringLiteral("■"), cloudui::shortErrorText(e)
                         + (e.requestId.isEmpty() ? QString() : QStringLiteral("\n") + tr("Hibaazonosító: %1").arg(e.requestId)),
                     tr("Újra"));
    });
}

void CloudLoginDialog::showEvent(QShowEvent* e)
{
    QDialog::showEvent(e);
    if (!m_started) { m_started = true; start(); }
}

void CloudLoginDialog::start()
{
    m_state->setVisible(false);
    m_retry->setVisible(false);
    m_copy->setVisible(true);
    m_copy->setEnabled(false);
    m_reopen->setVisible(true);
    m_reopen->setEnabled(false);
    m_codeLabel->setText(QStringLiteral("—"));
    m_wait->setText(tr("Kód kérése…"));
    m_wait->setVisible(true);
    m_app->cloud()->startDeviceFlow();
}

void CloudLoginDialog::onCode(const DeviceCode& code)
{
    m_code = code;
    m_expires = QDateTime::currentDateTimeUtc().addSecs(code.expiresIn);
    m_codeLabel->setText(code.userCode);
    m_copy->setEnabled(true);
    m_copy->setText(tr("Kód másolása"));
    m_reopen->setEnabled(true);
    m_fallbackUri->setText(tr("Ha a böngésző nem nyílt meg, nyisd meg ezt a címet: <a href=\"%1\">%1</a>, "
                              "és írd be a kódot: <b>%2</b>. Telefonon is jóváhagyhatod, ha ott vagy belépve.")
                               .arg(code.verificationUri.toHtmlEscaped(), code.userCode.toHtmlEscaped()));
    const bool opened = QDesktopServices::openUrl(QUrl(code.verificationUriComplete));
    m_intro->setText(opened
        ? tr("Megnyitottuk a böngészőt. Ellenőrizd, hogy ugyanez a kód látszik-e, és hagyd jóvá.")
        : tr("Nem sikerült megnyitni a böngészőt. Nyisd meg kézzel az alábbi címet, és írd be a kódot."));
    tick();
    m_timer->start();
}

void CloudLoginDialog::tick()
{
    const qint64 left = QDateTime::currentDateTimeUtc().secsTo(m_expires);
    if (left <= 0) { m_wait->setText(tr("A kód lejárt.")); return; }
    m_wait->setText(tr("Várakozás a jóváhagyásra · lejár %1:%2 múlva")
                        .arg(left / 60).arg(left % 60, 2, 10, QLatin1Char('0')));
}

void CloudLoginDialog::setState(const QString& glyph, const QString& text, const QString& retryLabel)
{
    m_wait->setVisible(false);
    m_state->setText(glyph + QStringLiteral("  ") + text);
    m_state->setVisible(true);
    m_retry->setVisible(!retryLabel.isEmpty());
    m_retry->setText(retryLabel);
    if (!retryLabel.isEmpty()) { m_copy->setVisible(false); m_reopen->setVisible(false); }
}

void CloudLoginDialog::reject()
{
    if (!m_done) m_app->cloud()->cancelDeviceFlow();
    m_timer->stop();
    QDialog::reject();
}

} // namespace tanara_gui
