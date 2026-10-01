#include "cloud/CloudAccountPanel.h"
#include "cloud/CloudLoginDialog.h"
#include "cloud/CloudTierWidget.h"
#include "cloud/CloudUi.h"

#include "tanara/AppController.h"
#include "tanara/SettingsManager.h"
#include "tanara/cloud/CloudAccount.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

namespace tanara_gui {

using namespace tanara;

namespace {
QLabel* muted(const QString& text, QWidget* parent)
{
    auto* l = new QLabel(text, parent);
    l->setWordWrap(true);
    cloudui::mute(l);
    return l;
}
} // namespace

CloudAccountPanel::CloudAccountPanel(AppController* app, QWidget* parent)
    : QWidget(parent), m_app(app)
{
    auto* lay = new QVBoxLayout(this);
    auto* title = new QLabel(tr("TANARA CLOUD FIÓK"), this);
    title->setStyleSheet(QStringLiteral("QLabel { font-weight: bold; }"));
    lay->addWidget(title);

    // Inline állapot-sáv (offline, leválasztva, felfüggesztve, túl régi, elfogyott).
    m_inline = new QFrame(this);
    m_inline->setFrameShape(QFrame::StyledPanel);
    auto* il = new QHBoxLayout(m_inline);
    m_inlineText = new QLabel(m_inline);
    m_inlineText->setWordWrap(true);
    m_inlineCta = new QPushButton(m_inline);
    il->addWidget(m_inlineText, 1);
    il->addWidget(m_inlineCta);
    connect(m_inlineCta, &QPushButton::clicked, this, [this]() { if (m_inlineAction) m_inlineAction(); });
    m_inline->setVisible(false);
    lay->addWidget(m_inline);

    // --- kijelentkezve ---
    m_outBox = new QWidget(this);
    auto* ol = new QVBoxLayout(m_outBox);
    ol->setContentsMargins(0, 0, 0, 0);
    ol->addWidget(new QLabel(tr("Egy bejelentkezés az átíráshoz és az összefoglalóhoz is. Jelszó nincs: "
                                "a böngészőben hagyod jóvá."), m_outBox));
    auto* login = new QPushButton(tr("Bejelentkezés a Tanara Cloudba"), m_outBox);
    login->setDefault(true);
    auto* lrow = new QHBoxLayout();
    lrow->addWidget(login);
    lrow->addStretch(1);
    ol->addLayout(lrow);
    ol->addWidget(muted(tr("Nincs még fiókod? A bejelentkezés során regisztrálhatsz."), m_outBox));
    connect(login, &QPushButton::clicked, this, [this]() {
        CloudLoginDialog dlg(m_app, this);
        if (dlg.exec() == QDialog::Accepted) {
            refresh();
            // Az első bejelentkezés után felajánljuk a használatot (ha még BYO a kiválasztott).
            if (!m_app->usesCloud(WorkflowStep::Transcribe) || !m_app->usesCloud(WorkflowStep::Summarize))
                emit useCloudRequested();
        }
    });
    lay->addWidget(m_outBox);

    // --- bejelentkezve ---
    m_inBox = new QWidget(this);
    auto* inl = new QVBoxLayout(m_inBox);
    inl->setContentsMargins(0, 0, 0, 0);
    auto* erow = new QHBoxLayout();
    m_email = new QLabel(m_inBox);
    erow->addWidget(m_email, 1);
    auto* dash = new QLabel(QStringLiteral("<a href=\"#\">%1</a>").arg(tr("Fiókom a weben ↗")), m_inBox);
    connect(dash, &QLabel::linkActivated, this, [this]() { cloudui::openUrl(m_app->cloud()->account().dashboardUrl); });
    erow->addWidget(dash);
    inl->addLayout(erow);

    auto* brow = new QHBoxLayout();
    m_balance = new QLabel(m_inBox);
    m_balance->setTextFormat(Qt::RichText);
    brow->addWidget(m_balance, 1);
    m_topup = new QPushButton(m_inBox);
    connect(m_topup, &QPushButton::clicked, this, [this]() { cloudui::startTopup(this, m_app); });
    brow->addWidget(m_topup);
    auto* reload = new QPushButton(QStringLiteral("↻"), m_inBox);
    reload->setToolTip(tr("Egyenleg frissítése"));
    reload->setFixedWidth(34);
    connect(reload, &QPushButton::clicked, this, [this]() {
        m_balance->setText(tr("Egyenleg: …"));
        m_app->cloud()->refreshAccount();
        m_app->cloud()->fetchModels();
    });
    brow->addWidget(reload);
    inl->addLayout(brow);
    m_hours = muted(QString(), m_inBox);
    inl->addWidget(m_hours);
    m_trial = muted(QString(), m_inBox);
    m_trial->setOpenExternalLinks(true);
    inl->addWidget(m_trial);
    m_notices = new QLabel(m_inBox);
    m_notices->setWordWrap(true);
    m_notices->setOpenExternalLinks(true);
    inl->addWidget(m_notices);

    m_useCloud = new QPushButton(tr("Átírás és összefoglaló a Tanara Clouddal"), m_inBox);
    m_useCloud->setToolTip(tr("A „Külső szolgáltatások” fülön mindkét feladat szolgáltatója a Tanara Cloud lesz."));
    connect(m_useCloud, &QPushButton::clicked, this, &CloudAccountPanel::useCloudRequested);
    inl->addWidget(m_useCloud);

    inl->addWidget(new QLabel(tr("<b>Alapértelmezett feldolgozás</b> (a becslésnél is módosítható)"), m_inBox));
    m_sttTier = new CloudTierWidget(m_app, WorkflowStep::Transcribe, /*showLanguage*/ true, m_inBox);
    m_llmTier = new CloudTierWidget(m_app, WorkflowStep::Summarize, /*showLanguage*/ false, m_inBox);
    inl->addWidget(m_sttTier);
    inl->addWidget(m_llmTier);

    auto* frow = new QHBoxLayout();
    auto* usage = new QLabel(QStringLiteral("<a href=\"#\">%1</a>").arg(tr("Napló a weben ↗")), m_inBox);
    usage->setToolTip(tr("A költések és az utolsó műveletek a weben (a kliensben később)."));
    connect(usage, &QLabel::linkActivated, this, [this]() { cloudui::openUrl(m_app->cloud()->account().usageUrl); });
    frow->addWidget(usage);
    frow->addStretch(1);
    auto* logout = new QPushButton(tr("Kijelentkezés"), m_inBox);
    connect(logout, &QPushButton::clicked, this, [this]() {
        const auto btn = QMessageBox::question(this, tr("Kijelentkezés"),
            tr("A kulcsot ezen a gépen töröljük és visszavonjuk."),
            QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel);
        if (btn == QMessageBox::Ok) m_app->cloud()->logout();
    });
    frow->addWidget(logout);
    inl->addLayout(frow);
    lay->addWidget(m_inBox);

    // --- adatkezelés (CLI-15, brief 7.7) ---
    lay->addWidget(muted(tr("Tanara Cloud módban a hangfelvétel és az átirat a feldolgozáshoz a szerverünkön át "
                            "a szolgáltatóhoz megy. Nem tároljuk: a feldolgozás után töröljük."), this));
    auto* more = new QToolButton(this);
    more->setText(tr("Mi hagyja el a gépet ebben a módban?"));
    more->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    more->setArrowType(Qt::RightArrow);
    more->setAutoRaise(true);
    more->setCheckable(true);
    lay->addWidget(more);
    auto* detail = muted(tr("Átírásnál: a felvétel hangfájlja. Összefoglalónál: az átirat szövege. A gépeden marad: "
                            "a felvétel, a beszélőfelismerés, a hangminták és minden fájl. Mi csak metaadatot "
                            "naplózunk: időpont, művelet, modell, hossz vagy tokenszám, összeg, eszköz."), this);
    detail->setVisible(false);
    lay->addWidget(detail);
    connect(more, &QToolButton::toggled, this, [more, detail](bool on) {
        more->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
        detail->setVisible(on);
    });
    lay->addStretch(1);

    CloudAccount* acc = m_app->cloud();
    connect(acc, &CloudAccount::accountUpdated, this, [this](const AccountInfo&) { m_lastError = {}; refresh(); });
    connect(acc, &CloudAccount::accountFailed, this, [this](const CloudError& e) { m_lastError = e; refresh(); });
    connect(acc, &CloudAccount::balanceChanged, this, &CloudAccountPanel::refresh);
    connect(acc, &CloudAccount::loggedIn, this, &CloudAccountPanel::refresh);
    connect(acc, &CloudAccount::loggedOut, this, [this]() { m_lastError = {}; refresh(); });
    connect(acc, &CloudAccount::clientTooOldDetected, this, &CloudAccountPanel::refresh);

    refresh();
    if (acc->isLoggedIn()) { acc->refreshAccount(); acc->fetchModels(); }
}

void CloudAccountPanel::showInline(const QString& glyph, const QString& text, const QString& cta,
                                   std::function<void()> action, bool critical)
{
    m_inline->setStyleSheet(critical
        ? QStringLiteral("QFrame { background: rgba(200,50,50,0.14); border: 1px solid rgba(180,40,40,0.6); border-radius: 4px; }")
        : QStringLiteral("QFrame { background: palette(alternate-base); border: 1px solid palette(midlight); border-radius: 4px; }"));
    m_inlineText->setText(glyph + QStringLiteral("  ") + text);
    m_inlineCta->setVisible(!cta.isEmpty());
    m_inlineCta->setText(cta);
    m_inlineAction = std::move(action);
    m_inline->setVisible(true);
}

void CloudAccountPanel::refresh()
{
    CloudAccount* acc = m_app->cloud();
    const bool in = acc->isLoggedIn();
    m_outBox->setVisible(!in);
    m_inBox->setVisible(in);
    m_inline->setVisible(false);

    if (acc->clientTooOld()) {
        showInline(QStringLiteral("■"), tr("Frissítsd a Tanarát (legalább %1; neked %2 van).").arg(acc->minClient(), libraryVersion()),
                   tr("Frissítés"), [acc]() { cloudui::openUrl(acc->downloadUrl()); }, true);
    }
    if (!in) return;

    const AccountInfo a = acc->account();
    m_email->setText(tr("Bejelentkezve: <b>%1</b>").arg((a.email.isEmpty() ? acc->email() : a.email).toHtmlEscaped()));
    if (a.valid) {
        QString bal = tr("Egyenleg: <b>%1</b>").arg(cloudui::money(a.balance));
        if (a.balanceEmpty) bal += QStringLiteral(" <span style='color:#c0392b;'>■ %1</span>").arg(tr("elfogyott"));
        else if (a.lowBalance) bal += QStringLiteral(" <span style='color:#b35900;'>▲ %1</span>").arg(tr("kevés"));
        bal += QStringLiteral(" <span style='color:gray;'>· %1</span>").arg(vatLabel(a.vatMode).toHtmlEscaped());
        m_balance->setText(bal);
        m_hours->setText(cloudui::hoursLine(a) + QStringLiteral(" · ")
                         + tr("Az összefoglaló költsége ezen felül, a szöveg hosszától függően."));
    } else {
        m_balance->setText(tr("Egyenleg: …"));
        m_hours->clear();
    }
    m_topup->setText(a.valid && !a.topupAvailable ? tr("Írj nekünk a feltöltéshez") : tr("Egyenleg feltöltése"));
    m_topup->setStyleSheet(a.lowBalance || a.balanceEmpty ? QStringLiteral("QPushButton { font-weight: bold; }") : QString());

    QString trial;
    if (a.trial == QLatin1String("awaiting_email"))
        trial = tr("A próbaegyenleg az e-mail címed megerősítése után jár.");
    else if (a.trial == QLatin1String("awaiting_card"))
        trial = tr("A próbaegyenleget a kártya-ellenőrzés után írjuk jóvá — <a href=\"%1\">a weben</a>.").arg(a.dashboardUrl.toHtmlEscaped());
    else if (a.trial == QLatin1String("denied_card_used"))
        trial = tr("Ezzel a kártyával már aktiváltak próbaegyenleget.");
    m_trial->setText(trial);
    m_trial->setVisible(!trial.isEmpty());

    QStringList notes;
    for (const Notice& n : a.notices) {
        const QString glyph = n.level == QLatin1String("critical") ? QStringLiteral("■")
                            : n.level == QLatin1String("warning")  ? QStringLiteral("▲") : QStringLiteral("ℹ");
        notes << glyph + QStringLiteral(" ") + n.message.toHtmlEscaped()
                 + (n.url.isEmpty() ? QString() : QStringLiteral(" <a href=\"%1\">%2</a>").arg(n.url.toHtmlEscaped(), tr("Részletek")));
    }
    m_notices->setText(notes.join(QStringLiteral("<br>")));
    m_notices->setVisible(!notes.isEmpty());
    m_useCloud->setVisible(!m_app->usesCloud(WorkflowStep::Transcribe) || !m_app->usesCloud(WorkflowStep::Summarize));

    if (acc->clientTooOld()) return;
    const CloudError& e = m_lastError;
    if (e.kind == CloudErrorKind::Unauthorized) {
        showInline(QStringLiteral("■"), tr("Ezt az eszközt leválasztották. Jelentkezz be újra."), tr("Bejelentkezés"), [this]() {
            CloudLoginDialog dlg(m_app, this);
            dlg.exec();
        }, true);
    } else if (e.kind == CloudErrorKind::Suspended) {
        const bool dispute = e.suspensionReason == QLatin1String("payment_dispute");
        showInline(QStringLiteral("■"), cloudui::shortErrorText(e), dispute ? tr("Megnyitás a weben") : tr("Support"),
                   [a, e, dispute]() { cloudui::openUrl(dispute ? a.dashboardUrl : (!e.supportUrl.isEmpty() ? e.supportUrl : a.contactUrl)); }, true);
    } else if (e.isError()) {
        const QString when = acc->accountFetchedAt().isValid()
            ? acc->accountFetchedAt().toString(QStringLiteral("yyyy-MM-dd HH:mm")) : QStringLiteral("—");
        showInline(QStringLiteral("…"), tr("Az egyenleg most nem elérhető — utolsó ismert egyenleg: %1 (%2).")
                                             .arg(cloudui::money(a.balance), when)
                                         + (e.requestId.isEmpty() ? QString() : QStringLiteral(" ") + tr("Hibaazonosító: %1").arg(e.requestId)),
                   QString(), nullptr, false);
    } else if (a.valid && a.balanceEmpty) {
        showInline(QStringLiteral("■"), tr("Elfogyott az egyenleged: %1.").arg(cloudui::money(a.balance)),
                   a.topupAvailable ? tr("Feltöltés") : tr("Írj nekünk"), [this]() { cloudui::startTopup(this, m_app); }, true);
    }
}

} // namespace tanara_gui
