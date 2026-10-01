#include "cloud/CloudEstimateDialog.h"
#include "cloud/CloudLoginDialog.h"
#include "cloud/CloudTermsDialog.h"
#include "cloud/CloudTierWidget.h"
#include "cloud/CloudUi.h"

#include "tanara/AppController.h"
#include "tanara/cloud/CloudAccount.h"
#include "tanara/store/MeetingStore.h"

#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

namespace tanara_gui {

using namespace tanara;

CloudEstimateDialog::CloudEstimateDialog(AppController* app, const QString& meetingId, const QString& task,
                                         const QString& summaryMode, QWidget* parent)
    : QDialog(parent), m_app(app), m_meetingId(meetingId), m_task(task), m_mode(summaryMode)
{
    const bool stt = task == QLatin1String("transcribe");
    setWindowTitle(stt ? tr("Átírás — Tanara Cloud") : tr("Összefoglaló — Tanara Cloud"));
    setMinimumWidth(500);
    auto* lay = new QVBoxLayout(this);

    auto* tier = new CloudTierWidget(app, stt ? WorkflowStep::Transcribe : WorkflowStep::Summarize,
                                     /*showLanguage*/ stt, this);
    connect(tier, &CloudTierWidget::changed, this, &CloudEstimateDialog::requestEstimate);
    lay->addWidget(tier);
    if (!stt)
        lay->addWidget(new QLabel(m_mode == QLatin1String("complex") ? tr("Mód: Komplex összefoglaló (témánként)")
                                                                   : tr("Mód: Gyors összefoglaló"), this));

    auto* card = new QFrame(this);
    card->setFrameShape(QFrame::StyledPanel);
    auto* cl = new QVBoxLayout(card);
    cl->addWidget(new QLabel(tr("Becsült költség"), card));
    m_main = new QLabel(QStringLiteral("≈ $ —"), card);
    m_main->setStyleSheet(QStringLiteral("QLabel { font-size: 22px; font-weight: bold; }"));
    cl->addWidget(m_main);
    m_range = new QLabel(tr("Becslés készül…"), card);
    cloudui::mute(m_range);
    cl->addWidget(m_range);
    m_lines = new QVBoxLayout();
    cl->addLayout(m_lines);
    lay->addWidget(card);

    m_balance = new QLabel(this);
    m_balance->setWordWrap(true);
    lay->addWidget(m_balance);
    m_after = new QLabel(this);
    m_after->setWordWrap(true);
    lay->addWidget(m_after);
    m_vat = new QLabel(this);
    cloudui::mute(m_vat);
    lay->addWidget(m_vat);
    m_error = new QLabel(this);
    m_error->setWordWrap(true);
    m_error->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_error->setVisible(false);
    lay->addWidget(m_error);

    auto* box = new QDialogButtonBox(this);
    m_start = box->addButton(tr("Indítás"), QDialogButtonBox::AcceptRole);
    m_start->setDefault(true);
    m_start->setEnabled(false);
    m_topup = box->addButton(tr("Egyenleg feltöltése"), QDialogButtonBox::ActionRole);
    m_topup->setVisible(false);
    box->addButton(QDialogButtonBox::Cancel)->setText(tr("Mégse"));
    lay->addWidget(box);

    connect(m_start, &QPushButton::clicked, this, [this]() {
        if (m_retryMode) { m_retryMode = false; requestEstimate(); return; }
        if (m_last.valid && estimateLevel(m_last) == EstimateLevel::NotEnough) {
            cloudui::startTopup(this, m_app);   // az indítás helyén a feltöltés
            return;
        }
        accept();
    });
    connect(m_topup, &QPushButton::clicked, this, [this]() { cloudui::startTopup(this, m_app); });
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    // Feltöltés után (a böngészőből visszatérve) a fiók frissül → újrabecslés.
    connect(m_app->cloud(), &CloudAccount::accountUpdated, this, [this](const AccountInfo& a) {
        if (m_last.valid && a.balance.micros != m_last.balance.micros) requestEstimate();
    });
}

void CloudEstimateDialog::showEvent(QShowEvent* e)
{
    QDialog::showEvent(e);
    if (!m_started) { m_started = true; requestEstimate(); }
}

void CloudEstimateDialog::requestEstimate()
{
    const int seq = ++m_seq;
    m_start->setEnabled(false);
    m_start->setText(tr("Indítás"));
    m_main->setText(QStringLiteral("≈ $ —"));
    m_range->setText(tr("Becslés készül…"));
    m_range->setVisible(true);
    m_error->setVisible(false);
    const EstimateRequest req = m_app->makeEstimateRequest(m_meetingId, m_task, m_mode);
    QPointer<CloudEstimateDialog> self(this);
    m_app->cloud()->estimate(req, [self, seq](const EstimateResult& r, const CloudError& e) {
        if (!self || seq != self->m_seq) return;
        if (e.isError()) self->onError(e);
        else self->render(r);
    });
}

void CloudEstimateDialog::render(const EstimateResult& e)
{
    m_last = e;
    const auto level = estimateLevel(e);
    m_main->setText(QStringLiteral("≈ ") + cloudui::money(e.estimate, MoneyStyle::Charge));
    // Pontos (csak STT) becslésnél nincs sáv.
    m_range->setText(e.low.micros == e.high.micros ? QString()
                     : tr("%1 – %2 között").arg(cloudui::money(e.low, MoneyStyle::Charge),
                                                cloudui::money(e.high, MoneyStyle::Charge)));
    m_range->setVisible(!m_range->text().isEmpty());
    while (QLayoutItem* it = m_lines->takeAt(0)) { delete it->widget(); delete it; }
    const Meeting m = m_app->store()->load(m_meetingId);
    for (const EstimateLine& l : e.breakdown) {
        auto* row = new QWidget(this);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        const QString tierTxt = cloudui::tierName(l.tier);
        const QString label = l.item == QLatin1String("stt")
            ? tr("Átírás (%1), %2").arg(tierTxt, formatDuration(m.durationMs))
            : tr("Összefoglaló (%1, %2)").arg(tierTxt, l.summaryMode == QLatin1String("complex") ? tr("komplex") : tr("gyors"));
        h->addWidget(new QLabel(label, row), 1);
        auto* tag = new QLabel(l.exact ? tr("pontos") : tr("becsült"), row);
        tag->setStyleSheet(l.exact
            ? QStringLiteral("QLabel { background: rgba(40,160,80,0.18); border-radius: 3px; padding: 1px 6px; }")
            : QStringLiteral("QLabel { background: rgba(230,160,40,0.22); border-radius: 3px; padding: 1px 6px; }"));
        h->addWidget(tag);
        const QString amount = cloudui::money(l.amount, MoneyStyle::Charge);
        h->addWidget(new QLabel(l.exact ? amount : tr("kb. %1").arg(amount), row));
        m_lines->addWidget(row);
    }
    if (m_task == QLatin1String("transcribe"))
        m_lines->addWidget(new QLabel(QStringLiteral("<span style='color:gray;'>%1</span>")
                                          .arg(tr("Az összefoglaló költsége ezen felül, a szöveg hosszától függően.")), this));

    const QString est = cloudui::money(e.estimate, MoneyStyle::Charge);
    m_topup->setVisible(false);
    m_start->setEnabled(true);
    m_after->setStyleSheet(QString());
    switch (level) {
    case EstimateLevel::Enough:
        m_balance->setText(tr("Egyenleged: %1").arg(cloudui::money(e.balance)));
        m_after->setText(tr("Utána kb. %1 marad").arg(cloudui::money(e.balanceAfter))
                         + (e.lowBalanceAfter ? QStringLiteral(" — ") + tr("ez a beállított küszöb alatt van.") : QString()));
        cloudui::mute(m_after);
        m_start->setText(tr("Indítás — ≈ %1").arg(est));
        break;
    case EstimateLevel::LittleReserve:
        m_balance->setText(tr("Egyenleged: %1").arg(cloudui::money(e.balance)));
        m_after->setText(tr("<b>Valószínűleg elég, de kevés tartalék marad.</b> Ha menet közben elfogy, a már "
                            "elkészült részek díja terhelődik, és feltöltés után folytathatod."));
        m_after->setStyleSheet(QStringLiteral("QLabel { color: #b35900; }"));
        m_start->setText(tr("Indítás — ≈ %1").arg(est));
        m_topup->setVisible(true);
        m_topup->setText(m_app->cloud()->account().topupAvailable ? tr("Feltöltés") : tr("Írj nekünk"));
        break;
    case EstimateLevel::NotEnough:
        m_balance->setText(tr("Ehhez kb. %1 kell, az egyenleged %2.").arg(est, cloudui::money(e.balance)));
        m_after->setText(tr("Az indításhoz tölts fel. Nem terheltünk semmit."));
        m_after->setStyleSheet(QStringLiteral("QLabel { color: #c0392b; }"));
        m_start->setText(m_app->cloud()->account().valid && !m_app->cloud()->account().topupAvailable
                             ? tr("Írj nekünk a feltöltéshez") : tr("Egyenleg feltöltése"));
        break;
    }
    m_vat->setText(vatLabel(e.vatMode));
}

void CloudEstimateDialog::onError(const CloudError& e)
{
    m_main->setText(QStringLiteral("≈ $ —"));
    m_range->setText(QString());
    switch (e.kind) {
    case CloudErrorKind::TermsRequired:
        if (CloudTermsDialog::requireFromError(m_app, e, this)) { requestEstimate(); return; }
        reject();
        return;
    case CloudErrorKind::Network:
        m_error->setText(tr("Nem értük el a szervert. Ellenőrizd az internetkapcsolatot, és próbáld újra."));
        m_error->setVisible(true);
        m_start->setText(tr("Újra"));
        m_start->setEnabled(true);
        m_retryMode = true;
        return;
    default: break;
    }
    // A többi (401/403/426/429/503/500…) a közös hiba-dialógusban; a becslés nem terhel.
    const cloudui::ErrorAction a = cloudui::showCloudError(this, m_app, e, QStringLiteral("estimate"));
    if (a == cloudui::ErrorAction::Retry) { requestEstimate(); return; }
    if (a == cloudui::ErrorAction::Login) {
        CloudLoginDialog dlg(m_app, this);
        if (dlg.exec() == QDialog::Accepted) { requestEstimate(); return; }
    }
    reject();
}

} // namespace tanara_gui
