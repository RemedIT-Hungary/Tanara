#include "SettingsEmbeddingModel.h"
#include "tanara/app/AppSettings.h"
#include "tanara/providers/core/ProviderConfig.h"

#include "SettingsProviderModel.h"
#include "SettingsViewModel.h"
#include "ShellFormat.h"

#include "tanara/app/ISettingsFacade.h"
#include "tanara/providers/embedding/ITextEmbedder.h"
#include "tanara/providers/core/ProviderRegistry.h"

#include <cmath>

using namespace tanara;

namespace tanara_qml {

namespace {

// Estimated time to prepare one meeting, until it can be measured from the running preparation.
constexpr int kSecondsPerMeeting = 9;

// Hungarian suffix choice for "at 31 / 40" / "at 31 / 30": the spoken last part of the number decides.
bool highVowelNumber(int n)
{
    n = std::abs(n);
    if (n == 0) return false;                                   // zero
    static const bool ones[10] = {false, true, true, false, true, true, false, true, false, true};
    static const bool tens[10] = {false, true, false, false, true, true, false, true, false, true};
    if (n % 10) return ones[n % 10];                            // one, two, four, five, seven, nine
    if (n % 100) return tens[(n / 10) % 10];                    // ten, forty, fifty, seventy, ninety
    if (n % 1000) return false;                                 // hundred
    return true;                                                // ezer
}

} // namespace

SettingsEmbeddingModel::SettingsEmbeddingModel(QObject* parent) : QObject(parent)
{
    m_vm = qobject_cast<SettingsViewModel*>(parent);
    if (m_vm) {
        m_card = new SettingsProviderModel(m_vm, providers::CapabilityKind::TextEmbedding);
        // The local endpoint's fields / test also affect the pill and the warning.
        connect(m_card, &SettingsProviderModel::testChanged, this, &SettingsEmbeddingModel::changed);
        connect(m_card, &SettingsProviderModel::valuesChanged, this, &SettingsEmbeddingModel::changed);
    }
}

app::ISettingsFacade* SettingsEmbeddingModel::facade() const
{
    return m_vm ? m_vm->facade() : nullptr;
}

void SettingsEmbeddingModel::attach()
{
    disconnect(m_prepConn);
    disconnect(m_rejectConn);
    if (app::ISettingsFacade* f = facade()) {
        m_prepConn = connect(f, &app::ISettingsFacade::embeddingStateChanged, this, &SettingsEmbeddingModel::changed);
        m_rejectConn = connect(f, &app::ISettingsFacade::rejectedSuggestionsChanged, this, &SettingsEmbeddingModel::rejectedChanged);
    }
    emit changed();
    emit rejectedChanged();
}

void SettingsEmbeddingModel::reset()
{
    if (m_card) m_card->reset();
    emit changed();
}

QString SettingsEmbeddingModel::modeOf(const QString& providerId)
{
    if (providerId == providers::embeddingproviders::LocalId) return QStringLiteral("local");
    if (providerId == providers::embeddingproviders::CloudId) return QStringLiteral("cloud");
    return QStringLiteral("none");
}

QString SettingsEmbeddingModel::providerFor(const QString& mode)
{
    if (mode == QLatin1String("local")) return providers::embeddingproviders::LocalId;
    if (mode == QLatin1String("cloud")) return providers::embeddingproviders::CloudId;
    return QString();
}

QString SettingsEmbeddingModel::effectiveModel(const tanara::app::AppSettings& s) const
{
    const QString id = s.providers.embedding.selectedId;
    const app::ISettingsFacade* f = facade();
    if (!f || !f->registry().has(providers::CapabilityKind::TextEmbedding, id)) return QString();
    const QString model = s.providers.embedding.selected().model.trimmed();
    if (model.isEmpty() && id == providers::embeddingproviders::CloudId) return QStringLiteral("default");
    return model;
}

void SettingsEmbeddingModel::normalize(tanara::app::AppSettings& s) const
{
    if (!m_card || !facade()) return;
    const providers::ProviderDescriptor d = facade()->registry().descriptor(providers::CapabilityKind::TextEmbedding, s.providers.embedding.selectedId);
    if (!d.id.isEmpty()) m_card->fillDefaults(s.providers.embedding.configs[s.providers.embedding.selectedId], d);
}

// ---- mode ---------------------------------------------------------------------------------

QString SettingsEmbeddingModel::mode() const
{
    if (m_vm) return modeOf(m_vm->draft().providers.embedding.selectedId);
    return QStringLiteral("none");
}

void SettingsEmbeddingModel::setMode(const QString& mode)
{
    if (!m_vm || mode == this->mode()) return;
    if (mode != QLatin1String("none") && mode != QLatin1String("local") && mode != QLatin1String("cloud")) return;
    if (mode == QLatin1String("cloud")) return;   // Tanara Cloud is "coming soon": not selectable
    const QString id = providerFor(mode);
    tanara::app::AppSettings& d = m_vm->draft();
    d.providers.embedding.selectedId = id;
    if (!id.isEmpty()) {
        // The other mode's settings stay in the map; the new one starts with the defaults.
        providers::ProviderConfig cfg = d.providers.embedding.configs.value(id);
        m_card->fillDefaults(cfg, facade()->registry().descriptor(providers::CapabilityKind::TextEmbedding, id));
        d.providers.embedding.configs.insert(id, cfg);
    }
    m_card->reset();
    m_vm->touch();
    emit changed();
}

QVariantList SettingsEmbeddingModel::modeOptions() const
{
    auto opt = [](const QString& value, const QString& label, bool enabled = true, const QString& tip = QString()) {
        return QVariantMap{{QStringLiteral("value"), value}, {QStringLiteral("label"), label},
                           {QStringLiteral("enabled"), enabled}, {QStringLiteral("toolTip"), tip}};
    };
    QVariantList out{opt(QStringLiteral("none"), tr("Nincs (alap)")),
                     opt(QStringLiteral("local"), tr("Helyi végpont"))};
    // Tanara Cloud is "coming soon": the item is shown, but it cannot be chosen.
    out << opt(QStringLiteral("cloud"), tr("Tanara Cloud · hamarosan"), false,
               tr("A Tanara Cloud hamarosan érkezik."));
    return out;
}

QString SettingsEmbeddingModel::description() const
{
    const QString m = mode();
    if (m == QLatin1String("cloud"))
        return tr("A rokon témákat is felismeri, akkor is, ha más szavakkal beszéltetek róla. "
                  "Nem kell hozzá helyi modell.");
    if (m == QLatin1String("local"))
        return tr("A rokon témákat is felismeri, akkor is, ha más szavakkal beszéltetek róla. "
                  "Beágyazó modell kell hozzá (pl. LM Studio). Alap szinten is működnek a "
                  "javaslatok: közös résztvevők, nevek, hasonló cím.");
    return tr("A javaslatok alap szinten működnek: közös résztvevők, nevek, hasonló cím alapján. "
              "Nem kell hozzá modell, és semmi nem hagyja el a gépet.");
}

// ---- status pill ------------------------------------------------------------------------

QString SettingsEmbeddingModel::status() const
{
    const QString m = mode();
    if (m == QLatin1String("none")) return QStringLiteral("neutral");
    if (m == QLatin1String("local") && m_card && !m_card->testState().isEmpty()) return m_card->testState();
    if (m == QLatin1String("cloud")) return QStringLiteral("neutral");   // not available yet
    const QString prep = prepStatus();
    if (prep == QLatin1String("error")) return QStringLiteral("failed");
    if (m == QLatin1String("local") && m_card && !m_card->configured()) return QStringLiteral("neutral");
    return QStringLiteral("ok");
}

QString SettingsEmbeddingModel::statusText() const
{
    const QString m = mode();
    if (m == QLatin1String("none")) return tr("Alap szint");
    if (m == QLatin1String("local") && m_card && !m_card->testState().isEmpty()) {
        // The card's "Connected · 31 ms": the header only needs the state.
        return m_card->testState() == QLatin1String("ok") ? tr("Kapcsolódva") : m_card->statusText();
    }
    if (m == QLatin1String("cloud")) return tr("Nem elérhető");
    const QString prep = prepStatus();
    if (prep == QLatin1String("error")) return tr("Nem érhető el");
    if (m == QLatin1String("local") && m_card && !m_card->configured()) return tr("Hiányos");
    // The preparation already talked to the endpoint -> connected; otherwise only configured.
    const bool talked = (prep == QLatin1String("running") && prepState().prepared > 0) || prep == QLatin1String("done");
    return talked ? tr("Kapcsolódva") : tr("Beállítva");
}

// ---- LIBRARY PREPARATION ----------------------------------------------------------------

library::EmbeddingState SettingsEmbeddingModel::prepState() const
{
    const app::ISettingsFacade* f = facade();
    return f ? f->embeddingState() : library::EmbeddingState();
}

bool SettingsEmbeddingModel::prepVisible() const
{
    return mode() != QLatin1String("none");
}

QString SettingsEmbeddingModel::prepStatus() const
{
    if (mode() == QLatin1String("none")) return QString();
    // The selected embedding is not saved yet: the preparation starts after the save.
    if (m_vm && m_vm->base().providers.embedding.selectedId.isEmpty()) return QStringLiteral("pending");
    const library::EmbeddingState s = prepState();
    switch (s.status) {
    case library::EmbeddingState::Running: return QStringLiteral("running");
    case library::EmbeddingState::Error:   return QStringLiteral("error");
    case library::EmbeddingState::Done:    return QStringLiteral("done");
    case library::EmbeddingState::Idle:    break;
    }
    if (s.total > 0 && s.prepared >= s.total && s.lastRun.isValid()) return QStringLiteral("done");
    return QStringLiteral("idle");
}

double SettingsEmbeddingModel::progress() const
{
    const library::EmbeddingState s = prepState();
    return s.total > 0 ? qBound(0.0, double(s.prepared) / double(s.total), 1.0) : 0.0;
}

QString SettingsEmbeddingModel::countText() const
{
    const library::EmbeddingState s = prepState();
    return tr("%1 / %2 megbeszélés").arg(s.prepared).arg(s.total);
}

QString SettingsEmbeddingModel::etaText() const
{
    const library::EmbeddingState s = prepState();
    if (s.status != library::EmbeddingState::Running || s.etaSec < 0) return QString();
    const int minutes = qMax(1, int(std::ceil(s.etaSec / 60.0)));
    return tr("kb. %n perc van hátra", nullptr, minutes);
}

QString SettingsEmbeddingModel::errorText() const
{
    const library::EmbeddingState s = prepState();
    if (s.status != library::EmbeddingState::Error) return QString();
    const QString why = s.error.trimmed().isEmpty() ? tr("ismeretlen hiba") : s.error.trimmed();
    return highVowelNumber(s.total) ? tr("Megállt %1 / %2-nél: %3").arg(s.prepared).arg(s.total).arg(why)
                                    : tr("Megállt %1 / %2-nál: %3").arg(s.prepared).arg(s.total).arg(why);
}

QString SettingsEmbeddingModel::doneText() const
{
    const library::EmbeddingState s = prepState();
    QString text = tr("Naprakész · %n megbeszélés", nullptr, s.total);
    if (s.lastRun.isValid())
        text += QStringLiteral(" · ") + fmt::uiLocale().toString(s.lastRun, QStringLiteral("MMM d. HH:mm"));
    return text;
}

void SettingsEmbeddingModel::startPreparation()
{
    if (app::ISettingsFacade* f = facade()) f->startEmbedding();
}

void SettingsEmbeddingModel::cancelPreparation()
{
    if (app::ISettingsFacade* f = facade()) f->cancelEmbedding();
}

void SettingsEmbeddingModel::resumePreparation()
{
    if (app::ISettingsFacade* f = facade()) f->resumeEmbedding();
}

void SettingsEmbeddingModel::reprepare()
{
    if (app::ISettingsFacade* f = facade()) f->reprepareEmbeddings();
}

// ---- model change ------------------------------------------------------------------------

bool SettingsEmbeddingModel::restartPending() const
{
    if (!m_vm) return false;
    const QString before = effectiveModel(m_vm->base());
    const QString after = effectiveModel(m_vm->draft());
    return !before.isEmpty() && !after.isEmpty() && before != after;
}

int SettingsEmbeddingModel::estimateMinutes(int meetings) const
{
    const library::EmbeddingState s = prepState();
    double perMeeting = kSecondsPerMeeting;
    const int left = s.total - s.prepared;
    if (s.status == library::EmbeddingState::Running && s.etaSec > 0 && left > 0)
        perMeeting = double(s.etaSec) / left;              // the running preparation's measured pace
    return qMax(1, int(std::ceil(meetings * perMeeting / 60.0)));
}

QString SettingsEmbeddingModel::restartWarning() const
{
    if (!restartPending()) return QString();
    const bool otherProvider = m_vm && m_vm->base().providers.embedding.selectedId != m_vm->draft().providers.embedding.selectedId;
    const QString lead = otherProvider ? tr("Másik beágyazásra váltasz.") : tr("Másik modellre váltasz.");
    const int total = prepState().total;
    if (total <= 0)
        return lead + QLatin1Char(' ')
             + tr("Mentés után a könyvtár előkészítése elölről indul. Addig a javaslatok az alap szinten működnek.");
    return lead + QLatin1Char(' ')
         + tr("Mentés után a könyvtár előkészítése elölről indul (%1, kb. %2). Addig a javaslatok az alap szinten működnek.")
               .arg(tr("%n megbeszélés", nullptr, total), tr("%n perc", nullptr, estimateMinutes(total)));
}

// ---- TAG SUGGESTIONS ---------------------------------------------------------------------

bool SettingsEmbeddingModel::tagSuggestions() const
{
    return m_vm ? m_vm->draft().processing.tagSuggestions : true;
}

void SettingsEmbeddingModel::setTagSuggestions(bool on)
{
    if (!m_vm || m_vm->draft().processing.tagSuggestions == on) return;
    m_vm->draft().processing.tagSuggestions = on;
    m_vm->touch();
    emit changed();
}

bool SettingsEmbeddingModel::llmTagSuggestions() const
{
    return m_vm ? m_vm->draft().processing.llmTagSuggestions : true;
}

void SettingsEmbeddingModel::setLlmTagSuggestions(bool on)
{
    if (!m_vm || m_vm->draft().processing.llmTagSuggestions == on) return;
    m_vm->draft().processing.llmTagSuggestions = on;
    m_vm->touch();
    emit changed();
}

int SettingsEmbeddingModel::rejectedCount() const
{
    const app::ISettingsFacade* f = facade();
    return f ? f->rejectedSuggestionCount() : 0;
}

void SettingsEmbeddingModel::clearRejected()
{
    if (app::ISettingsFacade* f = facade()) f->clearRejectedSuggestions();   // → rejectedChanged
}

} // namespace tanara_qml
