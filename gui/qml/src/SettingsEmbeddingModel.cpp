#include "SettingsEmbeddingModel.h"
#include "tanara/app/AppSettings.h"
#include "tanara/providers/core/ProviderConfig.h"

#include "SettingsProviderModel.h"
#include "SettingsViewModel.h"
#include "ShellFormat.h"

#include "tanara/app/AppController.h"
#include "tanara/library/index/EmbeddingIndex.h"
#include "tanara/providers/embedding/ITextEmbedder.h"
#include "tanara/providers/core/ProviderRegistry.h"
#include "tanara/library/tags/TagService.h"

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

app::AppController* SettingsEmbeddingModel::controller() const
{
    return m_vm ? m_vm->controller() : nullptr;
}

void SettingsEmbeddingModel::attach()
{
    disconnect(m_prepConn);
    disconnect(m_rejectConn);
    m_preparer = nullptr;
    app::AppController* c = controller();
    if (c) {
        m_preparer = c->embeddingPreparer();
        if (m_preparer)
            m_prepConn = connect(m_preparer, &library::EmbeddingPreparer::stateChanged, this, &SettingsEmbeddingModel::changed);
        if (c->tags())
            m_rejectConn = connect(c->tags(), &library::TagService::rejectedChanged, this, &SettingsEmbeddingModel::rejectedChanged);
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

QString SettingsEmbeddingModel::effectiveModel(const tanara::app::AppSettings& s)
{
    const QString id = s.providers.embedding.selectedId;
    if (!providers::ProviderRegistry::defaultInstance().has(providers::CapabilityKind::TextEmbedding, id)) return QString();
    const QString model = s.providers.embedding.selected().model.trimmed();
    if (model.isEmpty() && id == providers::embeddingproviders::CloudId) return QStringLiteral("default");
    return model;
}

void SettingsEmbeddingModel::normalize(tanara::app::AppSettings& s) const
{
    if (!m_card) return;
    const providers::ProviderDescriptor d = providers::ProviderRegistry::defaultInstance().descriptor(providers::CapabilityKind::TextEmbedding, s.providers.embedding.selectedId);
    if (!d.id.isEmpty()) m_card->fillDefaults(s.providers.embedding.configs[s.providers.embedding.selectedId], d);
}

// ---- mode ---------------------------------------------------------------------------------

QString SettingsEmbeddingModel::mode() const
{
    if (m_vm) return modeOf(m_vm->draft().providers.embedding.selectedId);
    if (m_demoState == QLatin1String("cloud")) return QStringLiteral("cloud");
    if (m_demoState.isEmpty() || m_demoState == QLatin1String("none")) return QStringLiteral("none");
    return QStringLiteral("local");
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
        m_card->fillDefaults(cfg, providers::ProviderRegistry::defaultInstance().descriptor(providers::CapabilityKind::TextEmbedding, id));
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
    if (controller() && m_preparer) return m_preparer->state();
    return m_demo;
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
    if (controller()) {
        if (m_preparer) m_preparer->start();
        return;
    }
    m_demo.status = library::EmbeddingState::Running;
    if (m_demo.prepared >= m_demo.total) m_demo.prepared = 0;
    m_demo.etaSec = (m_demo.total - m_demo.prepared) * kSecondsPerMeeting;
    emit changed();
}

void SettingsEmbeddingModel::cancelPreparation()
{
    if (controller()) {
        if (m_preparer) m_preparer->cancel();
        return;
    }
    m_demo.status = library::EmbeddingState::Idle;
    m_demo.etaSec = -1;
    emit changed();
}

void SettingsEmbeddingModel::resumePreparation()
{
    if (controller()) {
        if (m_preparer) m_preparer->resume();
        return;
    }
    m_demo.status = library::EmbeddingState::Running;
    m_demo.error.clear();
    m_demo.etaSec = (m_demo.total - m_demo.prepared) * kSecondsPerMeeting;
    emit changed();
}

void SettingsEmbeddingModel::reprepare()
{
    if (app::AppController* c = controller()) {
        if (c->embeddings()) c->embeddings()->invalidateAll();
        if (m_preparer) m_preparer->start();
        return;
    }
    m_demo.prepared = 0;
    startPreparation();
}

// ---- model change ------------------------------------------------------------------------

bool SettingsEmbeddingModel::restartPending() const
{
    if (!m_vm) return m_demoState == QLatin1String("modelChange");
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
    if (app::AppController* c = controller())
        return c->tags() ? c->tags()->rejectedCount() : 0;
    return m_demoRejected;
}

void SettingsEmbeddingModel::clearRejected()
{
    if (app::AppController* c = controller()) {
        if (c->tags()) c->tags()->clearRejected();   // rejectedChanged → rejectedChanged
        return;
    }
    if (m_demoRejected == 0) return;
    m_demoRejected = 0;
    emit rejectedChanged();
}

// ---- demo --------------------------------------------------------------------------------

void SettingsEmbeddingModel::setDemoState(const QString& state)
{
    if (controller()) return;                 // with real data the demo has no effect
    loadDemo(state);
    if (m_vm) m_vm->touch();
    emit demoStateChanged();
}

void SettingsEmbeddingModel::loadDemo(const QString& state)
{
    m_demoState = state;
    const bool local = state == QLatin1String("local") || state == QLatin1String("localRunning")
                    || state == QLatin1String("localError") || state == QLatin1String("localDone")
                    || state == QLatin1String("modelChange");
    const bool cloud = state == QLatin1String("cloud");

    if (m_vm) {
        tanara::app::AppSettings& base = m_vm->m_base;
        tanara::app::AppSettings& draft = m_vm->m_draft;
        for (tanara::app::AppSettings* s : {&base, &draft}) {
            s->providers.embedding.configs.clear();
            s->providers.embedding.selectedId = local ? providers::embeddingproviders::LocalId
                                   : cloud ? providers::embeddingproviders::CloudId : QString();
            providers::ProviderConfig cfg;
            cfg.type = providers::embeddingproviders::LocalId;
            cfg.baseUrl = QStringLiteral("http://localhost:1234/v1");
            cfg.model = QStringLiteral("text-embedding-nomic-embed-text-v1.5");
            s->providers.embedding.configs.insert(providers::embeddingproviders::LocalId, cfg);
            if (cloud) {
                providers::ProviderConfig c;
                c.type = providers::embeddingproviders::CloudId;
                s->providers.embedding.configs.insert(providers::embeddingproviders::CloudId, c);
            }
            s->processing.tagSuggestions = true;
            s->processing.llmTagSuggestions = true;
        }
        // The model was edited, not saved yet.
        if (state == QLatin1String("modelChange"))
            draft.providers.embedding.configs[providers::embeddingproviders::LocalId].model = QStringLiteral("nomic-embed-text-v2-moe");
        if (m_card) m_card->reset();
    }

    m_demo = {};
    m_demo.provider = local ? providers::embeddingproviders::LocalId : cloud ? providers::embeddingproviders::CloudId : QString();
    m_demo.total = 40;
    if (state == QLatin1String("localRunning")) {
        m_demo.status = library::EmbeddingState::Running;
        m_demo.prepared = 23;
        m_demo.etaSec = 230;
    } else if (state == QLatin1String("localError") || state == QLatin1String("modelChange")) {
        m_demo.status = library::EmbeddingState::Error;
        m_demo.prepared = 31;
        m_demo.error = tr("a végpont nem válaszol. Fut a helyi szerver?");
    } else if (state == QLatin1String("localDone") || cloud) {
        m_demo.status = library::EmbeddingState::Done;
        m_demo.prepared = 40;
        m_demo.lastRun = QDateTime(QDate(2026, 10, 5), QTime(9, 12));
    } else if (local) {
        m_demo.prepared = 12;                  // interrupted preparation: "Prepare" button
    }
    m_demoRejected = 12;
    emit changed();
    emit rejectedChanged();
}

} // namespace tanara_qml
