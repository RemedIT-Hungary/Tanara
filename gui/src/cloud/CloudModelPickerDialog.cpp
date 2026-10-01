#include "cloud/CloudModelPickerDialog.h"
#include "cloud/CloudUi.h"

#include "tanara/AppController.h"
#include "tanara/SettingsManager.h"
#include "tanara/cloud/CloudAccount.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTabBar>
#include <QVBoxLayout>

namespace tanara_gui {

using namespace tanara;

CloudModelPickerDialog::CloudModelPickerDialog(AppController* app, const QString& kind, QWidget* parent)
    : QDialog(parent), m_app(app)
{
    setWindowTitle(tr("Expert mód — modell választása"));
    resize(520, 420);
    auto* lay = new QVBoxLayout(this);
    m_tabs = new QTabBar(this);
    m_tabs->addTab(tr("Átírás"));
    m_tabs->addTab(tr("Összefoglaló"));
    m_tabs->setCurrentIndex(kind == QLatin1String("llm") ? 1 : 0);
    lay->addWidget(m_tabs);

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Keresés név vagy azonosító szerint…"));
    m_search->setClearButtonEnabled(true);
    lay->addWidget(m_search);

    m_list = new QListWidget(this);
    m_list->setAlternatingRowColors(true);
    lay->addWidget(m_list, 1);
    m_empty = new QLabel(tr("Nincs találat erre a keresésre."), this);
    m_empty->setVisible(false);
    lay->addWidget(m_empty);

    auto* note = new QLabel(tr("Az árak tájékoztató jellegűek (%1); a tényleges díjat a becslés és a "
                               "feldolgozás után a Tanara Cloud mutatja.")
                                .arg(vatLabel(m_app->cloud()->account().vatMode)), this);
    note->setWordWrap(true);
    cloudui::mute(note);
    lay->addWidget(note);

    auto* box = new QDialogButtonBox(this);
    auto* back = box->addButton(tr("← Vissza a Gyors / Pontos szintekhez"), QDialogButtonBox::ResetRole);
    auto* pick = box->addButton(tr("Kiválasztás"), QDialogButtonBox::AcceptRole);
    box->addButton(QDialogButtonBox::Cancel)->setText(tr("Mégse"));
    pick->setDefault(true);
    lay->addWidget(box);

    connect(m_tabs, &QTabBar::currentChanged, this, &CloudModelPickerDialog::rebuild);
    connect(m_search, &QLineEdit::textChanged, this, &CloudModelPickerDialog::rebuild);
    connect(back, &QPushButton::clicked, this, [this]() { store(QString()); accept(); });
    connect(pick, &QPushButton::clicked, this, [this]() {
        if (QListWidgetItem* it = m_list->currentItem()) { store(it->data(Qt::UserRole).toString()); accept(); }
    });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
        store(it->data(Qt::UserRole).toString());
        accept();
    });
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_app->cloud(), &CloudAccount::modelsUpdated, this, &CloudModelPickerDialog::rebuild);
    if (m_app->cloud()->models().isEmpty()) m_app->cloud()->fetchModels();
    rebuild();
}

QString CloudModelPickerDialog::currentKind() const
{
    return m_tabs->currentIndex() == 1 ? QStringLiteral("llm") : QStringLiteral("stt");
}

void CloudModelPickerDialog::rebuild()
{
    m_list->clear();
    const AppSettings s = m_app->settings()->settings();
    const QString kind = currentKind();
    const QString selected = kind == QLatin1String("stt") ? s.cloudSttModel : s.cloudLlmModel;
    const QString lang = s.languageHints.value(0);
    const QString q = m_search->text().trimmed().toLower();
    for (const CloudModel& m : m_app->cloud()->models()) {
        if (m.kind != kind || m.isVirtual || !m.expert) continue;
        if (!q.isEmpty() && !(m.displayName + m.id).toLower().contains(q)) continue;
        const QString price = kind == QLatin1String("stt")
            ? tr("%1 / óra").arg(cloudui::money(m.perHour, MoneyStyle::Charge))
            : tr("%1 / 1M bemeneti token · %2 / 1M kimeneti token")
                  .arg(cloudui::money(m.perMInput, MoneyStyle::Charge), cloudui::money(m.perMOutput, MoneyStyle::Charge));
        QStringList flags;
        if (kind == QLatin1String("stt"))
            flags << (m.diarization ? tr("✓ beszélők elkülönítve") : tr("▲ nem különíti el a beszélőket"));
        if (m.notRecommendedFor(lang))
            flags << (lang == QLatin1String("hu") ? tr("▲ magyarhoz nem ajánlott") : tr("▲ ehhez a nyelvhez nem ajánlott"));
        auto* it = new QListWidgetItem(QStringLiteral("%1 — %2\n%3%4").arg(m.displayName, price, m.id,
                                           flags.isEmpty() ? QString() : QStringLiteral("  ·  ") + flags.join(QStringLiteral("  ·  "))),
                                       m_list);
        it->setData(Qt::UserRole, m.id);
        if (m.id == selected) m_list->setCurrentItem(it);
    }
    m_empty->setVisible(m_list->count() == 0);
}

void CloudModelPickerDialog::store(const QString& modelId)
{
    AppSettings s = m_app->settings()->settings();
    if (currentKind() == QLatin1String("stt")) s.cloudSttModel = modelId;
    else s.cloudLlmModel = modelId;
    m_app->settings()->setSettings(s);
}

} // namespace tanara_gui
