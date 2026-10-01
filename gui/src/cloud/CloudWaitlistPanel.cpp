#include "cloud/CloudWaitlistPanel.h"
#include "cloud/CloudUi.h"

#include "tanara/AppController.h"
#include "tanara/SettingsManager.h"
#include "tanara/cloud/CloudAccount.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace tanara_gui {

using namespace tanara;

CloudWaitlistPanel::CloudWaitlistPanel(AppController* app, QWidget* parent)
    : QWidget(parent), m_app(app)
{
    auto* outer = new QVBoxLayout(this);
    auto* title = new QLabel(tr("Tanara Cloud — hamarosan"), this);
    title->setStyleSheet(QStringLiteral("QLabel { font-weight: bold; font-size: 15px; }"));
    outer->addWidget(title);

    auto* intro = new QLabel(tr(
        "Átírás és összefoglaló saját API-kulcsok nélkül: bejelentkezel, és működik. "
        "Havidíj nélkül, csak a használatért fizetsz, a feltöltött egyenleg nem jár le. "
        "A saját kulcsos (BYO) mód ingyenes marad, és továbbra is így működik."), this);
    intro->setWordWrap(true);
    outer->addWidget(intro);

    m_stack = new QStackedWidget(this);
    outer->addWidget(m_stack);

    // --- 0: űrlap ---
    auto* form = new QWidget(m_stack);
    auto* fl = new QVBoxLayout(form);
    fl->setContentsMargins(0, 6, 0, 0);
    auto* grid = new QFormLayout();
    m_email = new QLineEdit(form);
    m_email->setPlaceholderText(tr("nev@example.com"));
    grid->addRow(tr("E-mail cím:"), m_email);

    m_useCase = new QComboBox(form);
    m_useCase->addItem(tr("— (nem kötelező)"), QString());
    m_useCase->addItem(tr("Meetingek"), QStringLiteral("meetings"));
    m_useCase->addItem(tr("Interjúk"), QStringLiteral("interviews"));
    m_useCase->addItem(tr("Hangfájlok"), QStringLiteral("audio_files"));
    m_useCase->addItem(tr("Egyéb"), QStringLiteral("other"));
    grid->addRow(tr("Mire használnád?"), m_useCase);

    auto* langs = new QWidget(form);
    auto* lh = new QHBoxLayout(langs);
    lh->setContentsMargins(0, 0, 0, 0);
    m_langHu = new QCheckBox(tr("Magyar"), langs);
    m_langEn = new QCheckBox(tr("Angol"), langs);
    m_langOther = new QCheckBox(tr("Egyéb"), langs);
    lh->addWidget(m_langHu); lh->addWidget(m_langEn); lh->addWidget(m_langOther); lh->addStretch(1);
    grid->addRow(tr("Milyen nyelvű meetingek? (nem kötelező)"), langs);
    fl->addLayout(grid);

    m_consent = new QCheckBox(tr("Értesítést kérek a Tanara Cloud indulásáról. Bármikor leiratkozhatok."), form);
    fl->addWidget(m_consent);
    auto* privacy = new QLabel(QStringLiteral("<a href=\"%1\">%2</a>")
                                   .arg(cloud::PrivacyUrl, tr("Adatkezelési tájékoztató")), form);
    privacy->setOpenExternalLinks(true);
    fl->addWidget(privacy);

    auto* row = new QHBoxLayout();
    m_submit = new QPushButton(tr("Értesítést kérek"), form);
    m_submit->setDefault(true);
    row->addWidget(m_submit);
    row->addStretch(1);
    fl->addLayout(row);

    m_status = new QLabel(form);
    m_status->setWordWrap(true);
    m_status->setVisible(false);
    fl->addWidget(m_status);

    auto* note = new QLabel(tr(
        "Adatot csak a gombra kattintva küldünk: az e-mail címed, a válaszaid, a platform és a "
        "Tanara verziója. Megerősítő e-mailt kapsz; csak a megerősített cím kerül a listára."), form);
    note->setWordWrap(true);
    cloudui::mute(note);
    fl->addWidget(note);
    fl->addStretch(1);
    m_stack->addWidget(form);

    // --- 1: feliratkozott ---
    auto* done = new QWidget(m_stack);
    auto* dl = new QVBoxLayout(done);
    dl->setContentsMargins(0, 6, 0, 0);
    m_joined = new QLabel(done);
    m_joined->setWordWrap(true);
    dl->addWidget(m_joined);
    auto* other = new QLabel(QStringLiteral("<a href=\"#\">%1</a>").arg(tr("Másik címmel iratkozom fel")), done);
    connect(other, &QLabel::linkActivated, this, [this]() {
        AppSettings s = m_app->settings()->settings();
        s.waitlistEmail.clear();
        m_app->settings()->setSettings(s);
        m_status->setVisible(false);
        m_stack->setCurrentIndex(0);
    });
    dl->addWidget(other);
    dl->addStretch(1);
    m_stack->addWidget(done);

    connect(m_email, &QLineEdit::textChanged, this, &CloudWaitlistPanel::updateButton);
    connect(m_consent, &QCheckBox::toggled, this, &CloudWaitlistPanel::updateButton);
    connect(m_submit, &QPushButton::clicked, this, &CloudWaitlistPanel::submit);
    connect(m_app->cloud(), &CloudAccount::waitlistJoined, this, [this](const QString& email) {
        if (!m_busy) return;
        m_busy = false;
        AppSettings s = m_app->settings()->settings();
        s.waitlistEmail = email;
        m_app->settings()->setSettings(s);
        showJoined(email);
    });
    connect(m_app->cloud(), &CloudAccount::waitlistFailed, this, &CloudWaitlistPanel::onFailed);

    const QString joined = m_app->settings()->settings().waitlistEmail;
    if (!joined.isEmpty()) showJoined(joined);
    updateButton();
}

void CloudWaitlistPanel::updateButton()
{
    WaitlistSignup s;
    s.email = m_email->text();
    s.consent = m_consent->isChecked();
    m_submit->setEnabled(!m_busy && validateWaitlist(s).isEmpty());
}

void CloudWaitlistPanel::submit()
{
    WaitlistSignup s;
    s.email = m_email->text().trimmed();
    s.consent = m_consent->isChecked();
    if (!validateWaitlist(s).isEmpty()) return;
    s.uiLanguage = cloudui::lang();
    s.useCase = m_useCase->currentData().toString();
    if (m_langHu->isChecked()) s.meetingLanguages << QStringLiteral("hu");
    if (m_langEn->isChecked()) s.meetingLanguages << QStringLiteral("en");
    if (m_langOther->isChecked()) s.meetingLanguages << QStringLiteral("other");
    m_busy = true;
    m_status->setText(tr("Küldés…"));
    m_status->setStyleSheet(QString());
    m_status->setVisible(true);
    updateButton();
    m_app->cloud()->joinWaitlist(s);
}

void CloudWaitlistPanel::showJoined(const QString& email)
{
    m_joined->setText(tr("✓ Feliratkoztál: <b>%1</b><br>Küldtünk egy megerősítő e-mailt. "
                         "A feliratkozás a benne lévő linkre kattintva él; az indulásról ide írunk.")
                          .arg(email.toHtmlEscaped()));
    m_stack->setCurrentIndex(1);
}

void CloudWaitlistPanel::onFailed(const CloudError& e)
{
    if (!m_busy) return;
    m_busy = false;
    QString text;
    if (e.kind == CloudErrorKind::DisposableEmail)
        text = tr("Eldobható e-mail címmel nem lehet feliratkozni. Adj meg egy állandó címet.");
    else if (e.kind == CloudErrorKind::Validation && e.fieldErrors.contains(QStringLiteral("email")))
        text = tr("Ellenőrizd az e-mail címet.");
    else
        text = cloudui::shortErrorText(e);
    if (!e.requestId.isEmpty())
        text += QStringLiteral(" (") + tr("Hibaazonosító: %1").arg(e.requestId) + QLatin1Char(')');
    m_status->setText(text);
    m_status->setStyleSheet(QStringLiteral("QLabel { color: #c0392b; }"));
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_status->setVisible(true);
    updateButton();
}

} // namespace tanara_gui
