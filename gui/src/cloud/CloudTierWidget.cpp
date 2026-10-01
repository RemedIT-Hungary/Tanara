#include "cloud/CloudTierWidget.h"
#include "cloud/CloudModelPickerDialog.h"
#include "cloud/CloudUi.h"

#include "tanara/AppController.h"
#include "tanara/SettingsManager.h"
#include "tanara/cloud/CloudAccount.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace tanara_gui {

using namespace tanara;

void CloudTierWidget::fillLanguages(QComboBox* combo, const QString& current)
{
    combo->clear();
    combo->addItem(tr("Automatikus"), QString());
    const QList<QPair<QString, QString>> langs = {
        { QStringLiteral("hu"), tr("Magyar") }, { QStringLiteral("en"), tr("Angol") },
        { QStringLiteral("de"), tr("Német") },  { QStringLiteral("fr"), tr("Francia") },
        { QStringLiteral("es"), tr("Spanyol") }, { QStringLiteral("it"), tr("Olasz") },
        { QStringLiteral("pl"), tr("Lengyel") }, { QStringLiteral("ro"), tr("Román") },
        { QStringLiteral("sk"), tr("Szlovák") },
    };
    for (const auto& l : langs)
        combo->addItem(QStringLiteral("%1 (%2)").arg(l.second, l.first), l.first);
    int i = combo->findData(current);
    if (i < 0 && !current.isEmpty()) { combo->addItem(current, current); i = combo->count() - 1; }
    combo->setCurrentIndex(i < 0 ? 0 : i);
}

CloudTierWidget::CloudTierWidget(AppController* app, WorkflowStep step, bool showLanguage, QWidget* parent)
    : QWidget(parent), m_app(app), m_step(step)
{
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    auto* row = new QHBoxLayout();
    row->addWidget(new QLabel(step == WorkflowStep::Transcribe ? tr("Átírás szintje:") : tr("Összefoglaló szintje:"), this));
    m_group = new QButtonGroup(this);
    m_fast = new QPushButton(tr("Gyors"), this);
    m_accurate = new QPushButton(tr("Pontos"), this);
    for (QPushButton* b : { m_fast, m_accurate }) { b->setCheckable(true); m_group->addButton(b); row->addWidget(b); }
    m_expertLabel = new QLabel(this);
    m_expertLabel->setVisible(false);
    row->addWidget(m_expertLabel);
    m_price = new QLabel(this);
    cloudui::mute(m_price);
    row->addWidget(m_price);
    auto* expert = new QPushButton(tr("Expert mód…"), this);
    expert->setFlat(true);
    row->addStretch(1);
    v->addLayout(row);
    // Második sor: nyelv (ha kell) + Expert — így keskeny panelen sem lóg ki.
    auto* row2 = new QHBoxLayout();
    if (showLanguage) {
        row2->addWidget(new QLabel(tr("A meeting nyelve:"), this));
        m_lang = new QComboBox(this);
        row2->addWidget(m_lang);
    }
    row2->addWidget(expert);
    row2->addStretch(1);
    v->addLayout(row2);

    m_warn = new QLabel(this);
    m_warn->setWordWrap(true);
    m_warn->setStyleSheet(QStringLiteral("QLabel { background: rgba(230,160,40,0.18); border: 1px solid rgba(200,130,20,0.6); "
                                         "border-radius: 4px; padding: 4px 6px; }"));
    m_warn->setVisible(false);
    v->addWidget(m_warn);

    auto commitTier = [this](const QString& tier) {
        if (m_updating) return;
        AppSettings s = m_app->settings()->settings();
        if (m_step == WorkflowStep::Transcribe) { s.cloudSttTier = tier; s.cloudSttModel.clear(); }
        else { s.cloudLlmTier = tier; s.cloudLlmModel.clear(); }
        m_app->settings()->setSettings(s);
        refresh();
        emit changed();
    };
    connect(m_fast, &QPushButton::clicked, this, [commitTier]() { commitTier(QStringLiteral("fast")); });
    connect(m_accurate, &QPushButton::clicked, this, [commitTier]() { commitTier(QStringLiteral("accurate")); });
    if (m_lang) {
        connect(m_lang, &QComboBox::currentIndexChanged, this, [this]() {
            if (m_updating) return;
            AppSettings s = m_app->settings()->settings();
            const QString code = m_lang->currentData().toString();
            s.languageHints = code.isEmpty() ? QStringList() : QStringList{ code };
            m_app->settings()->setSettings(s);
            refresh();
            emit changed();
        });
    }
    connect(expert, &QPushButton::clicked, this, [this]() {
        CloudModelPickerDialog dlg(m_app, m_step == WorkflowStep::Transcribe ? QStringLiteral("stt") : QStringLiteral("llm"), this);
        if (dlg.exec() == QDialog::Accepted) { refresh(); emit changed(); }
    });
    connect(m_app->cloud(), &CloudAccount::modelsUpdated, this, &CloudTierWidget::refresh);
    refresh();
}

void CloudTierWidget::refresh()
{
    m_updating = true;
    const AppSettings s = m_app->settings()->settings();
    const bool stt = m_step == WorkflowStep::Transcribe;
    const QString kind = stt ? QStringLiteral("stt") : QStringLiteral("llm");
    const QString tier = stt ? s.cloudSttTier : s.cloudLlmTier;
    const QString modelId = m_app->cloudModelFor(m_step);
    const QVector<CloudModel> cat = m_app->cloud()->models();
    const std::optional<CloudModel> model = findModel(cat, modelId);
    const bool expert = !(stt ? s.cloudSttModel : s.cloudLlmModel).isEmpty() && model && !model->isVirtual;

    m_fast->setChecked(!expert && tier == QLatin1String("fast"));
    m_accurate->setChecked(!expert && tier != QLatin1String("fast"));
    m_expertLabel->setVisible(expert);
    if (expert)
        m_expertLabel->setText(tr("Expert: <b>%1</b>").arg(model->displayName.toHtmlEscaped()));
    const QString lang = s.languageHints.value(0);
    if (m_lang) fillLanguages(m_lang, lang);

    // Tájékoztató ár (a katalógusból; a kliens nem számol — a becslés a gateway dolga).
    m_price->clear();
    if (model && stt && model->perHour.isValid())
        m_price->setText(tr("%1 / óra").arg(cloudui::money(model->perHour, MoneyStyle::Charge)));

    QStringList warns;
    if (model && stt && !model->diarization)
        warns << tr("Ez a szint nem különíti el a beszélőket: az átiratban mindenki egy beszélőként "
                    "jelenik meg. A felvételed sávjai megmaradnak. Ha fontos, ki mit mondott, válaszd "
                    "a Pontos szintet.");
    if (model && model->notRecommendedFor(lang))
        warns << (lang == QLatin1String("hu")
                      ? tr("Magyar nyelvhez nem ajánljuk — a pontosság gyengébb lehet.")
                      : tr("Ehhez a nyelvhez nem ajánljuk — a pontosság gyengébb lehet."));
    if (!(stt ? s.cloudSttModel : s.cloudLlmModel).isEmpty() && !cat.isEmpty() && !model)
        warns << tr("A korábban választott modell már nem érhető el; a %1 szintet használjuk.").arg(cloudui::tierName(tier));
    if (cat.isEmpty())
        warns << tr("A modell-lista még nem töltődött le — a szintek a bejelentkezés után frissülnek.");
    m_warn->setText(QStringLiteral("▲ ") + warns.join(QStringLiteral("\n▲ ")));
    m_warn->setVisible(!warns.isEmpty());
    Q_UNUSED(kind);
    m_updating = false;
}

} // namespace tanara_gui
