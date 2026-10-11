#include "VoiceModelListModel.h"

#include "AppContext.h"
#include "SettingsViewModel.h"

#include "tanara/AppController.h"
#include "tanara/Paths.h"
#include "tanara/SettingsManager.h"
#include "tanara/store/VoiceprintStore.h"
#include "tanara/voiceid/VoiceModelDownloader.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QUrl>

#include <algorithm>

using namespace tanara;

namespace tanara_qml {

namespace {

// Ajánlott alapállapot: egyedül a WeSpeaker (négy valódi megbeszélésen ez választotta szét
// legjobban a beszélőket); a CAM++ tartalék, az ERes2NetV2 magyar beszéden gyengébb.
const QString kWespeaker = VoiceModelRegistry::recommendedModelId();
const QString kCamplus = VoiceModelRegistry::defaultModelId();
const QString kEres = QStringLiteral("eres2netv2");

// Megjelenítési sorrend: WeSpeaker, CAM++, utána a többi.
QVector<VoiceModelSpec> ordered(QVector<VoiceModelSpec> specs)
{
    auto rank = [](const VoiceModelSpec& s) {
        return s.id == kWespeaker ? 0 : s.id == kCamplus ? 1 : 2;
    };
    std::stable_sort(specs.begin(), specs.end(),
                     [&](const VoiceModelSpec& a, const VoiceModelSpec& b) { return rank(a) < rank(b); });
    return specs;
}

QString mb(qint64 bytes) { return QString::number(qRound64(bytes / 1e6)); }

} // namespace

VoiceModelListModel::VoiceModelListModel(QObject* parent) : QAbstractListModel(parent)
{
    m_downloader = new VoiceModelDownloader(this);
    connect(m_downloader, &VoiceModelDownloader::progress, this, &VoiceModelListModel::onProgress);
    connect(m_downloader, &VoiceModelDownloader::finished, this, &VoiceModelListModel::onFinished);
    m_bfTimer.setInterval(1000);
    connect(&m_bfTimer, &QTimer::timeout, this, &VoiceModelListModel::pollBackfill);
    rebuild();

    // Önálló példány (QML-ből, pl. Első lépések): az App.controller a következő körben kötődik,
    // ha addig a gazda nem adott sajátot és nem kért demót.
    QMetaObject::invokeMethod(this, [this] {
        if (m_controllerSet) return;
        if (AppController* c = AppContext::instance()->controller()) setController(c);
    }, Qt::QueuedConnection);
}

VoiceModelListModel::~VoiceModelListModel() = default;

// ---- QAbstractListModel ----------------------------------------------------------------

int VoiceModelListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QHash<int, QByteArray> VoiceModelListModel::roleNames() const
{
    return {{IdRole, "modelId"},          {NameRole, "name"},
            {BlurbRole, "blurb"},         {LicenseRole, "license"},
            {LicenseNoteRole, "licenseNote"}, {SourceUrlRole, "sourceUrl"},
            {SourceTextRole, "sourceText"}, {SizeTextRole, "sizeText"},
            {DimTextRole, "dimText"},     {StateRole, "state"},
            {ProgressRole, "progress"},   {ProgressTextRole, "progressText"},
            {ErrorTextRole, "errorText"}, {EnabledRole, "enabled"},
            {CanEnableRole, "canEnable"}, {RecommendedRole, "recommended"},
            {NotRecommendedRole, "notRecommended"}};
}

QVariant VoiceModelListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) return {};
    const Row& r = m_rows.at(index.row());
    const VoiceModelSpec& s = r.spec;
    const bool def = s.id == kCamplus;
    switch (role) {
    case IdRole: return s.id;
    case NameRole: {
        // „CAM++ (3D-Speaker)” → a zárójeles rész a forrás-sorban úgyis látszik.
        const int p = s.displayName.indexOf(QLatin1String(" ("));
        return p > 0 ? s.displayName.left(p) : s.displayName;
    }
    case BlurbRole:
        if (def) return r.state == QLatin1String("bundled") ? tr("A csomagban · tartalék.")
                                                           : tr("Tartalék: ezt használja, amíg a WeSpeaker nincs letöltve.");
        if (s.id == kWespeaker) return tr("Ajánlott alap: négy valódi megbeszélésen ez választotta szét legjobban a beszélőket.");
        if (s.id == kEres) return tr("Rövid sorokra; magyar beszéden nem ajánlott.");
        return QString();
    case LicenseRole: return s.license;
    case LicenseNoteRole:
        // CC BY: a használat feltétele a szerzők megnevezése (a forrás-lapon).
        return s.license.startsWith(QLatin1String("CC-BY")) ? tr("a szerzők megnevezésével") : QString();
    case SourceUrlRole: return s.sourceUrl;
    case SourceTextRole: {
        // Rövid alak: „huggingface.co/Wespeaker/…” (a teljes cím a linkben és a tooltipben).
        const QUrl u(s.sourceUrl);
        QString host = u.host();
        if (host.startsWith(QLatin1String("www."))) host = host.mid(4);
        const QStringList parts = u.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
        if (parts.isEmpty()) return host;
        return host + QLatin1Char('/') + parts.first() + (parts.size() > 1 ? QStringLiteral("/…") : QString());
    }
    case SizeTextRole: return s.sizeBytes > 0 ? SettingsViewModel::formatBytes(s.sizeBytes) : QString();
    case DimTextRole: return s.dim > 0 ? tr("%1 dim.").arg(s.dim) : QString();
    case StateRole: return r.state;
    case ProgressRole:
        return r.state == QLatin1String("downloading") && r.total > 0 ? double(r.received) / double(r.total) : -1.0;
    case ProgressTextRole:
        return r.total > 0 ? QStringLiteral("%1 / %2 MB").arg(mb(r.received), mb(r.total))
                           : QStringLiteral("%1 MB").arg(mb(r.received));
    case ErrorTextRole: return r.errorText;
    case EnabledRole: return enabledIds().contains(s.id);
    case CanEnableRole:
        return r.state == QLatin1String("downloaded") || r.state == QLatin1String("bundled");
    case RecommendedRole: return s.id == kWespeaker;
    case NotRecommendedRole: return s.id == kEres;
    }
    return {};
}

// ---- controller / demó -------------------------------------------------------------------

QObject* VoiceModelListModel::controllerObject() const { return m_controller; }

void VoiceModelListModel::setControllerObject(QObject* controller)
{
    setController(qobject_cast<AppController*>(controller));
}

void VoiceModelListModel::setController(AppController* controller)
{
    if (m_controllerSet && m_controller == controller) return;
    m_controller = controller;
    m_controllerSet = true;
    attach();
    emit controllerChanged();
}

void VoiceModelListModel::setDemoState(const QString& state)
{
    if (m_controller) return;
    m_controllerSet = true;
    loadDemo(state);
    emit demoStateChanged();
}

void VoiceModelListModel::attach()
{
    for (const QMetaObject::Connection& c : std::as_const(m_connections)) disconnect(c);
    m_connections.clear();
    m_bfTimer.stop();
    AppController* c = m_controller;
    if (!c) {
        loadDemo(m_demoState);
        return;
    }
    m_connections << connect(c, &AppController::voiceModelsChanged, this, [this] {
        refreshFiles();
        // A core a jel UTÁN indítja a pótlást (ha bővült a lista): a következő körben nézzük.
        QTimer::singleShot(0, this, [this] { checkBackfill(); });
    });
    m_connections << connect(c, &AppController::voiceprintBackfillFinished, this,
                             &VoiceModelListModel::onBackfillFinished);
    m_connections << connect(c->settings(), &SettingsManager::settingsChanged, this, [this] {
        if (!m_getDraft) notifyDraftChanged();   // önálló példány: a core beállítása a forrás
    });
    m_bfState.clear();
    m_bfSeenRunning = false;
    refresh();
}

void VoiceModelListModel::loadDemo(const QString& state)
{
    m_demoState = state;
    m_controller.clear();
    beginResetModel();
    m_rows.clear();
    const QVector<VoiceModelSpec> specs = ordered(m_specOverride.isEmpty() ? VoiceModelRegistry::builtin() : m_specOverride);
    for (const VoiceModelSpec& s : specs) {
        Row r;
        r.spec = s;
        r.state = QStringLiteral("downloaded");
        m_rows << r;
    }
    auto row = [this](const QString& id) -> Row* {
        const int i = indexOf(id);
        return i >= 0 ? &m_rows[i] : nullptr;
    };
    m_offline = false;
    m_demoEnabled = {kWespeaker};
    if (Row* e = row(kEres)) e->state = QStringLiteral("missing");
    if (state == QLatin1String("B08download")) {
        if (Row* w = row(kWespeaker)) {
            w->state = QStringLiteral("downloading");
            w->total = w->spec.sizeBytes;
            w->received = qint64(w->total * 0.46);
        }
        m_demoEnabled = {kWespeaker};
    } else if (state == QLatin1String("B08none")) {
        for (Row& r : m_rows) r.state = QStringLiteral("missing");
        m_demoEnabled = {kWespeaker};
    } else if (state == QLatin1String("B08error")) {
        if (Row* w = row(kWespeaker)) {
            w->state = QStringLiteral("error");
            w->errorText = tr("A letöltés megszakadt: a kapcsolat 12 MB után megszűnt. Semmi nem került a gépre.");
        }
        m_demoEnabled = {kWespeaker};
    } else if (state == QLatin1String("B08offline")) {
        m_offline = true;
        if (Row* w = row(kWespeaker)) w->state = QStringLiteral("offline");
        if (Row* e = row(kEres)) e->state = QStringLiteral("offline");
        m_demoEnabled = {kWespeaker};
    } else if (state == QLatin1String("B08bundled")) {
        if (Row* c = row(kCamplus)) c->state = QStringLiteral("bundled");
    } else if (state == QLatin1String("B08fallback")) {
        // A WeSpeaker nincs letöltve, a csomagolt CAM++ fut tartalékként.
        if (Row* w = row(kWespeaker)) w->state = QStringLiteral("missing");
        if (Row* c = row(kCamplus)) c->state = QStringLiteral("bundled");
    }
    endResetModel();

    if (state == QLatin1String("B08backfill")) setBackfill(QStringLiteral("running"), 23, 14, 0);
    else if (state == QLatin1String("B08partial")) setBackfill(QStringLiteral("partial"), 23, 20, 3);
    else setBackfill(QString(), 0, 0, 0);
    emit summaryChanged();
}

void VoiceModelListModel::setSpecsForTest(const QVector<VoiceModelSpec>& specs)
{
    m_specOverride = specs;
    if (m_controller || m_dirsOverridden) { rebuild(); refresh(); }
    else loadDemo(m_demoState);
}

void VoiceModelListModel::setDirsForTest(const QString& metaDir, const QString& appDir)
{
    m_metaOverride = metaDir;
    m_appOverride = appDir;
    m_dirsOverridden = true;
    refresh();
}

// ---- állapot ---------------------------------------------------------------------------

QString VoiceModelListModel::metaDir() const
{
    if (m_dirsOverridden) return m_metaOverride;
    if (!m_controller) return QString();
    return paths::resolveMetadataDir(m_controller->settings()->settings().metadataDir);
}

QString VoiceModelListModel::appDir() const
{
    if (m_dirsOverridden) return m_appOverride;
    return QCoreApplication::instance() ? QCoreApplication::applicationDirPath() : QString();
}

QStringList VoiceModelListModel::enabledIds() const
{
    if (m_getDraft) return m_getDraft();
    if (m_controller) return m_controller->settings()->enabledVoiceModels();
    return m_demoEnabled;
}

void VoiceModelListModel::writeEnabled(const QStringList& ids)
{
    const QStringList norm = VoiceModelRegistry::normalizeIds(ids);
    if (m_setDraft) m_setDraft(norm);
    else if (m_controller) m_controller->settings()->setEnabledVoiceModels(norm);
    else m_demoEnabled = norm;
    notifyDraftChanged();
}

void VoiceModelListModel::setDraftAccess(std::function<QStringList()> get,
                                         std::function<void(const QStringList&)> set)
{
    m_getDraft = std::move(get);
    m_setDraft = std::move(set);
    notifyDraftChanged();
}

void VoiceModelListModel::notifyDraftChanged()
{
    if (!m_rows.isEmpty()) emit dataChanged(index(0), index(m_rows.size() - 1), {EnabledRole});
    emit summaryChanged();
}

void VoiceModelListModel::updateFileState(Row& r) const
{
    if (m_downloader->isRunning(r.spec.id)) { r.state = QStringLiteral("downloading"); return; }
    const QString meta = metaDir(), app = appDir();
    const QString user = VoiceModelDownloader::targetPath(r.spec, meta);
    const QString path = VoiceModelRegistry::resolvePath(r.spec, meta, app);
    const QFileInfo fi(path);
    r.corrupt = false;
    if (fi.exists()) {
        // Windows: az alapmodell az <appDir>/models-ban (a telepítő hozza) — nem letölthető.
        r.state = path != user ? QStringLiteral("bundled") : QStringLiteral("downloaded");
        if (r.spec.sizeBytes > 0 && fi.size() != r.spec.sizeBytes && r.state == QLatin1String("downloaded")) {
            r.corrupt = true;
            r.state = QStringLiteral("error");
            r.errorText = tr("A fájl mérete eltér a várttól (%1 helyett %2) — töltsd le újra.")
                              .arg(SettingsViewModel::formatBytes(r.spec.sizeBytes),
                                   SettingsViewModel::formatBytes(fi.size()));
        }
        return;
    }
    // Hiány: a legutóbbi letöltés hibája megmarad, amíg újra nem próbálja.
    if (r.state == QLatin1String("error") && !r.errorText.isEmpty()) return;
    r.state = m_offline ? QStringLiteral("offline") : QStringLiteral("missing");
}

void VoiceModelListModel::rebuild()
{
    beginResetModel();
    QHash<QString, Row> old;
    for (const Row& r : std::as_const(m_rows)) old.insert(r.spec.id, r);
    m_rows.clear();
    const QVector<VoiceModelSpec> specs = ordered(m_specOverride.isEmpty() ? VoiceModelRegistry::builtin() : m_specOverride);
    for (const VoiceModelSpec& s : specs) {
        Row r = old.value(s.id);
        r.spec = s;
        if (m_controller || m_dirsOverridden) updateFileState(r);
        m_rows << r;
    }
    endResetModel();
}

void VoiceModelListModel::refresh()
{
    if (!m_controller && !m_dirsOverridden) return;   // demó: a kitalált állapot marad
    refreshFiles();
    checkBackfill();
}

void VoiceModelListModel::refreshFiles()
{
    if (!m_controller && !m_dirsOverridden) return;
    m_offline = !m_downloader->networkReachable();
    for (int i = 0; i < m_rows.size(); ++i) {
        updateFileState(m_rows[i]);
        emitRow(i);
    }
    if (m_rows.size() != (m_specOverride.isEmpty() ? VoiceModelRegistry::builtin().size() : m_specOverride.size()))
        rebuild();
    emit summaryChanged();
}

int VoiceModelListModel::indexOf(const QString& id) const
{
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows.at(i).spec.id == id) return i;
    return -1;
}

void VoiceModelListModel::emitRow(int i)
{
    if (i >= 0 && i < m_rows.size()) emit dataChanged(index(i), index(i));
}

bool VoiceModelListModel::anyAvailable() const
{
    for (const Row& r : m_rows)
        if (r.state == QLatin1String("downloaded") || r.state == QLatin1String("bundled")) return true;
    return false;
}

int VoiceModelListModel::activeCount() const
{
    const QStringList on = enabledIds();
    int n = 0;
    for (const Row& r : m_rows)
        if (on.contains(r.spec.id)
            && (r.state == QLatin1String("downloaded") || r.state == QLatin1String("bundled")))
            ++n;
    return n;
}

bool VoiceModelListModel::fallbackActive() const
{
    // Ugyanaz a szabály, mint a VoiceModelRegistry::active()-é, a piszkozat szerint.
    if (activeCount() > 0) return false;
    const int i = indexOf(kCamplus);
    return i >= 0 && (m_rows.at(i).state == QLatin1String("downloaded") || m_rows.at(i).state == QLatin1String("bundled"));
}

QString VoiceModelListModel::defaultState() const
{
    const int i = indexOf(kWespeaker);
    return i >= 0 ? m_rows.at(i).state : QString();
}

QString VoiceModelListModel::defaultSizeText() const
{
    const auto s = VoiceModelRegistry::spec(kWespeaker);
    return s ? SettingsViewModel::formatBytes(s->sizeBytes) : QString();
}

QString VoiceModelListModel::defaultProgressText() const
{
    const int i = indexOf(kWespeaker);
    return i >= 0 ? data(index(i), ProgressTextRole).toString() : QString();
}

double VoiceModelListModel::defaultProgress() const
{
    const int i = indexOf(kWespeaker);
    return i >= 0 ? data(index(i), ProgressRole).toDouble() : -1.0;
}

QString VoiceModelListModel::defaultErrorText() const
{
    const int i = indexOf(kWespeaker);
    return i >= 0 ? m_rows.at(i).errorText : QString();
}

// ---- műveletek -------------------------------------------------------------------------

void VoiceModelListModel::download(const QString& id) { startDownload(id, false); }

void VoiceModelListModel::retry(const QString& id)
{
    const int i = indexOf(id);
    startDownload(id, i >= 0 && m_rows.at(i).corrupt);
}

void VoiceModelListModel::startDownload(const QString& id, bool overwrite)
{
    const int i = indexOf(id);
    if (i < 0) return;
    Row& r = m_rows[i];
    if (r.state == QLatin1String("bundled") || m_downloader->isRunning(id)) return;
    if (!m_controller && !m_dirsOverridden) {
        // Demó: csak az állapot változik.
        r.state = QStringLiteral("downloading");
        r.total = r.spec.sizeBytes;
        r.received = 0;
        emitRow(i);
        emit summaryChanged();
        return;
    }
    r.errorText.clear();
    r.received = 0;
    r.total = r.spec.sizeBytes;
    r.state = QStringLiteral("downloading");
    emitRow(i);
    m_downloader->start(r.spec, VoiceModelDownloader::targetPath(r.spec, metaDir()), overwrite);
    emit summaryChanged();
}

void VoiceModelListModel::cancel(const QString& id)
{
    if (m_downloader->isRunning(id)) {
        m_downloader->cancel(id);
        return;
    }
    const int i = indexOf(id);   // demó
    if (i >= 0 && m_rows.at(i).state == QLatin1String("downloading")) {
        m_rows[i].state = QStringLiteral("missing");
        emitRow(i);
        emit summaryChanged();
    }
}

void VoiceModelListModel::setEnabled(const QString& id, bool on)
{
    const int i = indexOf(id);
    if (i < 0) return;
    const QString st = m_rows.at(i).state;
    if (on && st != QLatin1String("downloaded") && st != QLatin1String("bundled")) return;
    QStringList ids = enabledIds();
    if (on == ids.contains(id)) return;
    if (on) ids << id;
    else ids.removeAll(id);
    writeEnabled(ids);
}

void VoiceModelListModel::openSource(const QString& id)
{
    const int i = indexOf(id);
    if (i >= 0 && !m_rows.at(i).spec.sourceUrl.isEmpty()) emit openUrlRequested(m_rows.at(i).spec.sourceUrl);
}

void VoiceModelListModel::onProgress(const QString& id, qint64 received, qint64 total)
{
    const int i = indexOf(id);
    if (i < 0) return;
    Row& r = m_rows[i];
    // Megabájtonként frissítünk (a sáv így is folyamatos, a QML-t nem árasztjuk el).
    const bool step = received / 1000000 != r.received / 1000000 || total != r.total || received == 0;
    r.received = received;
    r.total = total;
    if (step) {
        emitRow(i);
        if (id == kWespeaker) emit summaryChanged();
    }
}

void VoiceModelListModel::onFinished(const QString& id, bool ok, const QString& error)
{
    const int i = indexOf(id);
    if (i < 0) return;
    Row& r = m_rows[i];
    r.errorText.clear();
    if (ok) {
        updateFileState(r);
        // Letöltötte, hogy használja: a piszkozatban bekapcsoljuk (Mentéssel lép életbe).
        QStringList ids = enabledIds();
        if (!ids.contains(id)) {
            ids << id;
            writeEnabled(ids);
        } else if (m_controller && m_controller->settings()->enabledVoiceModels().contains(id)) {
            // Már be volt kapcsolva (pl. az alapmodell): a core a fájlt csak beállítás-változáskor
            // nézi újra — ugyanazt visszaírva újraszámolja a használt modelleket (és pótol).
            m_controller->settings()->setSettings(m_controller->settings()->settings());
        }
    } else {
        switch (m_downloader->lastFailure(id)) {
        case VoiceModelDownloader::Failure::Cancelled:
            r.state = QStringLiteral("missing");
            break;
        case VoiceModelDownloader::Failure::Offline:
            m_offline = true;
            r.state = QStringLiteral("offline");
            break;
        case VoiceModelDownloader::Failure::Exists:
            updateFileState(r);   // közben máshonnan (CLI) megjött
            break;
        case VoiceModelDownloader::Failure::SizeMismatch:
            r.state = QStringLiteral("error");
            r.errorText = tr("A letöltött fájl mérete nem egyezik a várttal, ezért nem mentettem el.");
            break;
        case VoiceModelDownloader::Failure::Write:
            r.state = QStringLiteral("error");
            r.errorText = tr("Nem tudtam menteni a modellt a belső adatok mappájába.");
            break;
        default:
            r.state = QStringLiteral("error");
            r.errorText = tr("A letöltés nem sikerült: %1").arg(error);
            break;
        }
    }
    emitRow(i);
    emit summaryChanged();
    emit downloadFinished(id, ok);
}

// ---- lenyomat-pótlás -------------------------------------------------------------------

int VoiceModelListModel::countMissingSamples() const
{
    if (!m_controller || !m_controller->voiceprints()) return 0;
    VoiceprintStore* store = m_controller->voiceprints();
    store->refresh();
    QSet<QString> missing;   // személy|testvér-kulcs: egy minta egy tétel, akárhány modell hiányzik
    const QStringList models = m_controller->activeVoiceModelIds();
    for (const QString& person : store->people())
        for (const QString& model : models)
            for (const Voiceprint& vp : store->samplesMissingModel(person, model))
                missing.insert(person + QLatin1Char('|') + VoiceprintStore::siblingKey(vp));
    return missing.size();
}

void VoiceModelListModel::setBackfill(const QString& state, int total, int done, int skipped)
{
    if (state == m_bfState && total == m_bfTotal && done == m_bfDone && skipped == m_bfSkipped) return;
    m_bfState = state;
    m_bfTotal = total;
    m_bfDone = done;
    m_bfSkipped = skipped;
    emit backfillChanged();
}

void VoiceModelListModel::checkBackfill()
{
    if (!m_controller) return;
    if (m_controller->voiceprintBackfillRunning()) {
        if (m_bfState != QLatin1String("running")) {
            m_bfSeenRunning = true;
            m_bfAdded = 0;
            setBackfill(QStringLiteral("running"), countMissingSamples(), 0, 0);
        }
        if (!m_bfTimer.isActive()) m_bfTimer.start();
        return;
    }
    if (m_bfState == QLatin1String("running")) return;   // a befejező jel zárja le
    // Nem fut: ha maradt pótolhatatlan minta, az látszik (hiányzó felvétel).
    const int left = countMissingSamples();
    if (left > 0) setBackfill(QStringLiteral("partial"), m_bfTotal, m_bfDone, left);
    else if (m_bfState == QLatin1String("partial")) setBackfill(QString(), 0, 0, 0);
}

void VoiceModelListModel::pollBackfill()
{
    if (!m_controller) { m_bfTimer.stop(); return; }
    if (!m_controller->voiceprintBackfillRunning()) {
        // A befejező jel elmaradt (vagy még úton van): a következő körben lezárjuk.
        m_bfTimer.stop();
        QMetaObject::invokeMethod(this, [this] {
            if (m_bfState == QLatin1String("running") && m_controller && !m_controller->voiceprintBackfillRunning())
                onBackfillFinished(0);
        }, Qt::QueuedConnection);
        return;
    }
    const int left = countMissingSamples();
    setBackfill(m_bfState, m_bfTotal, qBound(0, m_bfTotal - left, m_bfTotal), 0);
}

void VoiceModelListModel::onBackfillFinished(int added)
{
    m_bfAdded += added;
    if (!m_controller) return;
    if (m_controller->voiceprintBackfillRunning()) return;   // újrafut (közben bővült a lista)
    m_bfTimer.stop();
    const int left = countMissingSamples();
    if (left > 0) setBackfill(QStringLiteral("partial"), m_bfTotal, qMax(0, m_bfTotal - left), left);
    else if (m_bfSeenRunning) setBackfill(QStringLiteral("done"), m_bfTotal, m_bfTotal, 0);
    else setBackfill(QString(), 0, 0, 0);
}

double VoiceModelListModel::backfillProgress() const
{
    if (m_bfState != QLatin1String("running") || m_bfTotal <= 0) return -1.0;
    return double(m_bfDone) / double(m_bfTotal);
}

QString VoiceModelListModel::backfillText() const
{
    if (m_bfState == QLatin1String("running"))
        return m_bfTotal > 0
            ? tr("A meglévő %n minta újraszámolása az új modellel… (%1 / %2)", nullptr, m_bfTotal)
                  .arg(m_bfDone).arg(m_bfTotal)
            : tr("A meglévő minták újraszámolása az új modellel… folyamatban");
    if (m_bfState == QLatin1String("done"))
        return tr("Kész: a meglévő minták az új modellel is megvannak.");
    if (m_bfState == QLatin1String("partial"))
        return tr("%n minta nem volt pótolható (hiányzó felvétel).", nullptr, m_bfSkipped);
    return QString();
}

} // namespace tanara_qml
