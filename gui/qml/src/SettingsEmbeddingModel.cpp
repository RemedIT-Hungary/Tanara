#include "SettingsEmbeddingModel.h"

#include "SettingsCloudModel.h"
#include "SettingsProviderModel.h"
#include "SettingsViewModel.h"
#include "ShellFormat.h"

#include "tanara/AppController.h"
#include "tanara/embedding/EmbeddingIndex.h"
#include "tanara/embedding/EmbeddingProviderRegistry.h"
#include "tanara/tags/TagService.h"

#include <cmath>

using namespace tanara;

namespace tanara_qml {

namespace {

// A Tanara Cloud beágyazásának kredit-igénye megbeszélésenként. HELYŐRZŐ: a gateway még nem
// közli az árat (a szerver-oldal külön feladat); addig ezzel a becsléssel írjuk a tájékoztatót.
constexpr int kCloudCreditsPerMeeting = 3;
// Egy megbeszélés előkészítésének becsült ideje, amíg a futó előkészítésből nem mérhető.
constexpr int kSecondsPerMeeting = 9;

// „31 / 40-nél” / „31 / 30-nál”: a szám kiejtett utolsó tagja dönti el a toldalékot.
bool highVowelNumber(int n)
{
    n = std::abs(n);
    if (n == 0) return false;                                   // nulla
    static const bool ones[10] = {false, true, true, false, true, true, false, true, false, true};
    static const bool tens[10] = {false, true, false, false, true, true, false, true, false, true};
    if (n % 10) return ones[n % 10];                            // egy, kettő, négy, öt, hét, kilenc
    if (n % 100) return tens[(n / 10) % 10];                    // tíz, negyven, ötven, hetven, kilencven
    if (n % 1000) return false;                                 // száz
    return true;                                                // ezer
}

} // namespace

SettingsEmbeddingModel::SettingsEmbeddingModel(QObject* parent) : QObject(parent)
{
    m_vm = qobject_cast<SettingsViewModel*>(parent);
    if (m_vm) {
        m_card = new SettingsProviderModel(m_vm, ProviderKind::Embedding);
        // A helyi végpont mezői / tesztje a pirulát és a figyelmeztetést is érinti.
        connect(m_card, &SettingsProviderModel::testChanged, this, &SettingsEmbeddingModel::changed);
        connect(m_card, &SettingsProviderModel::valuesChanged, this, &SettingsEmbeddingModel::changed);
    }
}

AppController* SettingsEmbeddingModel::controller() const
{
    return m_vm ? m_vm->controller() : nullptr;
}

void SettingsEmbeddingModel::attach()
{
    disconnect(m_prepConn);
    disconnect(m_rejectConn);
    m_preparer = nullptr;
    AppController* c = controller();
    if (c) {
        m_preparer = c->embeddingPreparer();
        if (m_preparer)
            m_prepConn = connect(m_preparer, &EmbeddingPreparer::stateChanged, this, &SettingsEmbeddingModel::changed);
        if (c->tags())
            m_rejectConn = connect(c->tags(), &TagService::rejectedChanged, this, &SettingsEmbeddingModel::rejectedChanged);
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
    if (providerId == embeddingproviders::LocalId) return QStringLiteral("local");
    if (providerId == embeddingproviders::CloudId) return QStringLiteral("cloud");
    return QStringLiteral("none");
}

QString SettingsEmbeddingModel::providerFor(const QString& mode)
{
    if (mode == QLatin1String("local")) return embeddingproviders::LocalId;
    if (mode == QLatin1String("cloud")) return embeddingproviders::CloudId;
    return QString();
}

QString SettingsEmbeddingModel::effectiveModel(const AppSettings& s)
{
    const QString id = s.embeddingProviderId;
    if (!EmbeddingProviderRegistry::instance().has(id)) return QString();
    const QString model = s.embeddingSelected().model.trimmed();
    if (model.isEmpty() && id == embeddingproviders::CloudId) return QStringLiteral("default");
    return model;
}

void SettingsEmbeddingModel::normalize(AppSettings& s) const
{
    if (!m_card) return;
    const ProviderDescriptor d = EmbeddingProviderRegistry::instance().descriptor(s.embeddingProviderId);
    if (!d.id.isEmpty()) m_card->fillDefaults(s.embeddingConfigs[s.embeddingProviderId], d);
}

// ---- mód ---------------------------------------------------------------------------------

QString SettingsEmbeddingModel::mode() const
{
    if (m_vm) return modeOf(m_vm->draft().embeddingProviderId);
    if (m_demoState == QLatin1String("cloud")) return QStringLiteral("cloud");
    if (m_demoState.isEmpty() || m_demoState == QLatin1String("none")) return QStringLiteral("none");
    return QStringLiteral("local");
}

bool SettingsEmbeddingModel::cloudSelectable() const
{
    return m_vm && m_vm->cloudAvailability() == QLatin1String("live") && m_vm->cloud()->loggedIn();
}

void SettingsEmbeddingModel::setMode(const QString& mode)
{
    if (!m_vm || mode == this->mode()) return;
    if (mode != QLatin1String("none") && mode != QLatin1String("local") && mode != QLatin1String("cloud")) return;
    if (mode == QLatin1String("cloud") && !cloudSelectable()) return;
    const QString id = providerFor(mode);
    AppSettings& d = m_vm->draft();
    d.embeddingProviderId = id;
    if (!id.isEmpty()) {
        // A másik mód beállítása a térképben marad; az új az alapértelmezésekkel indul.
        ProviderConfig cfg = d.embeddingConfigs.value(id);
        m_card->fillDefaults(cfg, EmbeddingProviderRegistry::instance().descriptor(id));
        d.embeddingConfigs.insert(id, cfg);
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
    const QString availability = m_vm ? m_vm->cloudAvailability() : QStringLiteral("live");
    if (availability == QLatin1String("none")) return out;   // Cloud nélküli build
    QString tip;
    if (availability == QLatin1String("teaser"))
        tip = tr("A Tanara Cloud hamarosan érkezik.");
    else if (m_vm && !m_vm->cloud()->loggedIn())
        tip = tr("Ehhez jelentkezz be a Tanara Cloudba (fent, a Tanara Cloud kártyán).");
    const bool enabled = !m_vm || cloudSelectable() || mode() == QLatin1String("cloud");
    out << opt(QStringLiteral("cloud"), tr("Tanara Cloud"), enabled, enabled ? QString() : tip);
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

QString SettingsEmbeddingModel::cloudInfo() const
{
    QString text = tr("Az átiratok szövege a Tanara Cloudba kerül feldolgozásra, ugyanúgy, mint a "
                      "felhős összefoglalásnál. A hangfelvétel és a hanglenyomatok a gépen maradnak.");
    const int total = prepState().total;
    if (total > 0)
        text += QLatin1Char(' ')
              + tr("Az előkészítés egyszer kb. %1 kredit, utána megbeszélésenként ~%2.")
                    .arg(total * kCloudCreditsPerMeeting).arg(kCloudCreditsPerMeeting);
    return text;
}

QString SettingsEmbeddingModel::cloudHint() const
{
    if (mode() != QLatin1String("cloud") || !m_vm || m_vm->cloud()->loggedIn()) return QString();
    return tr("A Tanara Cloudhoz be kell jelentkezned (fent, a Tanara Cloud kártyán). Addig a "
              "javaslatok az alap szinten működnek.");
}

// ---- állapot-pirula ------------------------------------------------------------------------

QString SettingsEmbeddingModel::status() const
{
    const QString m = mode();
    if (m == QLatin1String("none")) return QStringLiteral("neutral");
    if (m == QLatin1String("local") && m_card && !m_card->testState().isEmpty()) return m_card->testState();
    if (m == QLatin1String("cloud") && m_vm && !m_vm->cloud()->loggedIn()) return QStringLiteral("neutral");
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
        // A kártya „Kapcsolódva · 31 ms”-e — a fejlécben elég az állapot.
        return m_card->testState() == QLatin1String("ok") ? tr("Kapcsolódva") : m_card->statusText();
    }
    if (m == QLatin1String("cloud") && m_vm && !m_vm->cloud()->loggedIn()) return tr("Bejelentkezés kell");
    const QString prep = prepStatus();
    if (prep == QLatin1String("error")) return tr("Nem érhető el");
    if (m == QLatin1String("local") && m_card && !m_card->configured()) return tr("Hiányos");
    // Az előkészítés már beszélt a végponttal → kapcsolódva; különben csak beállítva.
    const bool talked = (prep == QLatin1String("running") && prepState().prepared > 0) || prep == QLatin1String("done");
    return talked ? tr("Kapcsolódva") : tr("Beállítva");
}

// ---- KÖNYVTÁR ELŐKÉSZÍTÉSE ----------------------------------------------------------------

EmbeddingState SettingsEmbeddingModel::prepState() const
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
    // A kiválasztott beágyazás még nincs elmentve: az előkészítés a mentés után indul.
    if (m_vm && m_vm->base().embeddingProviderId.isEmpty()) return QStringLiteral("pending");
    const EmbeddingState s = prepState();
    switch (s.status) {
    case EmbeddingState::Running: return QStringLiteral("running");
    case EmbeddingState::Error:   return QStringLiteral("error");
    case EmbeddingState::Done:    return QStringLiteral("done");
    case EmbeddingState::Idle:    break;
    }
    if (s.total > 0 && s.prepared >= s.total && s.lastRun.isValid()) return QStringLiteral("done");
    return QStringLiteral("idle");
}

double SettingsEmbeddingModel::progress() const
{
    const EmbeddingState s = prepState();
    return s.total > 0 ? qBound(0.0, double(s.prepared) / double(s.total), 1.0) : 0.0;
}

QString SettingsEmbeddingModel::countText() const
{
    const EmbeddingState s = prepState();
    return tr("%1 / %2 megbeszélés").arg(s.prepared).arg(s.total);
}

QString SettingsEmbeddingModel::etaText() const
{
    const EmbeddingState s = prepState();
    if (s.status != EmbeddingState::Running || s.etaSec < 0) return QString();
    const int minutes = qMax(1, int(std::ceil(s.etaSec / 60.0)));
    return tr("kb. %n perc van hátra", nullptr, minutes);
}

QString SettingsEmbeddingModel::errorText() const
{
    const EmbeddingState s = prepState();
    if (s.status != EmbeddingState::Error) return QString();
    const QString why = s.error.trimmed().isEmpty() ? tr("ismeretlen hiba") : s.error.trimmed();
    return highVowelNumber(s.total) ? tr("Megállt %1 / %2-nél: %3").arg(s.prepared).arg(s.total).arg(why)
                                    : tr("Megállt %1 / %2-nál: %3").arg(s.prepared).arg(s.total).arg(why);
}

QString SettingsEmbeddingModel::doneText() const
{
    const EmbeddingState s = prepState();
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
    m_demo.status = EmbeddingState::Running;
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
    m_demo.status = EmbeddingState::Idle;
    m_demo.etaSec = -1;
    emit changed();
}

void SettingsEmbeddingModel::resumePreparation()
{
    if (controller()) {
        if (m_preparer) m_preparer->resume();
        return;
    }
    m_demo.status = EmbeddingState::Running;
    m_demo.error.clear();
    m_demo.etaSec = (m_demo.total - m_demo.prepared) * kSecondsPerMeeting;
    emit changed();
}

void SettingsEmbeddingModel::reprepare()
{
    if (AppController* c = controller()) {
        if (c->embeddings()) c->embeddings()->invalidateAll();
        if (m_preparer) m_preparer->start();
        return;
    }
    m_demo.prepared = 0;
    startPreparation();
}

// ---- modellváltás ------------------------------------------------------------------------

bool SettingsEmbeddingModel::restartPending() const
{
    if (!m_vm) return m_demoState == QLatin1String("modelChange");
    const QString before = effectiveModel(m_vm->base());
    const QString after = effectiveModel(m_vm->draft());
    return !before.isEmpty() && !after.isEmpty() && before != after;
}

int SettingsEmbeddingModel::estimateMinutes(int meetings) const
{
    const EmbeddingState s = prepState();
    double perMeeting = kSecondsPerMeeting;
    const int left = s.total - s.prepared;
    if (s.status == EmbeddingState::Running && s.etaSec > 0 && left > 0)
        perMeeting = double(s.etaSec) / left;              // a futó előkészítés mért üteme
    return qMax(1, int(std::ceil(meetings * perMeeting / 60.0)));
}

QString SettingsEmbeddingModel::restartWarning() const
{
    if (!restartPending()) return QString();
    const bool otherProvider = m_vm && m_vm->base().embeddingProviderId != m_vm->draft().embeddingProviderId;
    const QString lead = otherProvider ? tr("Másik beágyazásra váltasz.") : tr("Másik modellre váltasz.");
    const int total = prepState().total;
    if (total <= 0)
        return lead + QLatin1Char(' ')
             + tr("Mentés után a könyvtár előkészítése elölről indul. Addig a javaslatok az alap szinten működnek.");
    return lead + QLatin1Char(' ')
         + tr("Mentés után a könyvtár előkészítése elölről indul (%1, kb. %2). Addig a javaslatok az alap szinten működnek.")
               .arg(tr("%n megbeszélés", nullptr, total), tr("%n perc", nullptr, estimateMinutes(total)));
}

// ---- CÍMKEJAVASLATOK ----------------------------------------------------------------------

bool SettingsEmbeddingModel::tagSuggestions() const
{
    return m_vm ? m_vm->draft().tagSuggestions : true;
}

void SettingsEmbeddingModel::setTagSuggestions(bool on)
{
    if (!m_vm || m_vm->draft().tagSuggestions == on) return;
    m_vm->draft().tagSuggestions = on;
    m_vm->touch();
    emit changed();
}

bool SettingsEmbeddingModel::llmTagSuggestions() const
{
    return m_vm ? m_vm->draft().llmTagSuggestions : true;
}

void SettingsEmbeddingModel::setLlmTagSuggestions(bool on)
{
    if (!m_vm || m_vm->draft().llmTagSuggestions == on) return;
    m_vm->draft().llmTagSuggestions = on;
    m_vm->touch();
    emit changed();
}

int SettingsEmbeddingModel::rejectedCount() const
{
    if (AppController* c = controller())
        return c->tags() ? c->tags()->rejectedCount() : 0;
    return m_demoRejected;
}

void SettingsEmbeddingModel::clearRejected()
{
    if (AppController* c = controller()) {
        if (c->tags()) c->tags()->clearRejected();   // rejectedChanged → rejectedChanged
        return;
    }
    if (m_demoRejected == 0) return;
    m_demoRejected = 0;
    emit rejectedChanged();
}

// ---- demó --------------------------------------------------------------------------------

void SettingsEmbeddingModel::setDemoState(const QString& state)
{
    if (controller()) return;                 // valódi adat mellett a demó nem hat
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
        AppSettings& base = m_vm->m_base;
        AppSettings& draft = m_vm->m_draft;
        for (AppSettings* s : {&base, &draft}) {
            s->embeddingConfigs.clear();
            s->embeddingProviderId = local ? embeddingproviders::LocalId
                                   : cloud ? embeddingproviders::CloudId : QString();
            ProviderConfig cfg;
            cfg.type = embeddingproviders::LocalId;
            cfg.baseUrl = QStringLiteral("http://localhost:1234/v1");
            cfg.model = QStringLiteral("text-embedding-nomic-embed-text-v1.5");
            s->embeddingConfigs.insert(embeddingproviders::LocalId, cfg);
            if (cloud) {
                ProviderConfig c;
                c.type = embeddingproviders::CloudId;
                s->embeddingConfigs.insert(embeddingproviders::CloudId, c);
            }
            s->tagSuggestions = true;
            s->llmTagSuggestions = true;
        }
        // T15: a modellt átírták, még nincs elmentve.
        if (state == QLatin1String("modelChange"))
            draft.embeddingConfigs[embeddingproviders::LocalId].model = QStringLiteral("nomic-embed-text-v2-moe");
        if (m_card) m_card->reset();
    }

    m_demo = {};
    m_demo.provider = local ? embeddingproviders::LocalId : cloud ? embeddingproviders::CloudId : QString();
    m_demo.total = 40;
    if (state == QLatin1String("localRunning")) {
        m_demo.status = EmbeddingState::Running;
        m_demo.prepared = 23;
        m_demo.etaSec = 230;
    } else if (state == QLatin1String("localError") || state == QLatin1String("modelChange")) {
        m_demo.status = EmbeddingState::Error;
        m_demo.prepared = 31;
        m_demo.error = tr("a végpont nem válaszol. Fut a helyi szerver?");
    } else if (state == QLatin1String("localDone") || cloud) {
        m_demo.status = EmbeddingState::Done;
        m_demo.prepared = 40;
        m_demo.lastRun = QDateTime(QDate(2026, 10, 5), QTime(9, 12));
    } else if (local) {
        m_demo.prepared = 12;                  // félbehagyott előkészítés: „Előkészítés” gomb
    }
    m_demoRejected = 12;
    emit changed();
    emit rejectedChanged();
}

} // namespace tanara_qml
