#include "VoiceModelListModel.h"
#include "tanara/foundation/time/TimeFormat.h"


#include "AppContext.h"
#include "UiServices.h"
#include "SettingsViewModel.h"

#include "tanara/processing/voiceid/BackfillReport.h"
#include "tanara/processing/voiceid/IVoiceModelsFacade.h"
#include "tanara/processing/voiceid/VoiceModelDownloader.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QUrl>

#include <algorithm>

using namespace tanara;

namespace tanara_qml {

namespace {

// Recommended default: WeSpeaker alone (on four real meetings it separated speakers best);
// CAM++ is the fallback, ERes2NetV2 is weaker on Hungarian speech.
const QString kWespeaker = processing::VoiceModelRegistry::recommendedModelId();
const QString kCamplus = processing::VoiceModelRegistry::defaultModelId();
const QString kEres = QStringLiteral("eres2netv2");

// Display order: WeSpeaker, CAM++, then the rest.
QVector<processing::VoiceModelSpec> ordered(QVector<processing::VoiceModelSpec> specs)
{
    auto rank = [](const processing::VoiceModelSpec& s) {
        return s.id == kWespeaker ? 0 : s.id == kCamplus ? 1 : 2;
    };
    std::stable_sort(specs.begin(), specs.end(),
                     [&](const processing::VoiceModelSpec& a, const processing::VoiceModelSpec& b) { return rank(a) < rank(b); });
    return specs;
}

QString mb(qint64 bytes) { return QString::number(qRound64(bytes / 1e6)); }

// The file is present and (in the demo / during verification too) usable.
bool usable(const QString& state)
{
    return state == QLatin1String("downloaded") || state == QLatin1String("bundled")
        || state == QLatin1String("verifying");
}

// "12:03" / "1:02:03" (meeting time, ms).
QString clock(qint64 ms)
{
    return tanara::timefmt::clock(ms);
}

QString reasonText(processing::VoiceprintBackfillSkip::Reason reason)
{
    switch (reason) {
    case processing::VoiceprintBackfillSkip::Reason::MissingMeeting:
        return QCoreApplication::translate("VoiceModelListModel", "hiányzó felvétel (a megbeszélés már nincs meg)");
    case processing::VoiceprintBackfillSkip::Reason::MissingAudio:
        return QCoreApplication::translate("VoiceModelListModel", "hiányzó felvétel");
    case processing::VoiceprintBackfillSkip::Reason::Failed:
        return QCoreApplication::translate("VoiceModelListModel", "nem számolható (túl rövid hang)");
    }
    return QString();
}

QString shortName(const QString& modelId)
{
    const auto spec = processing::VoiceModelRegistry::spec(modelId);
    return spec ? spec->displayName.section(QLatin1Char(' '), 0, 0) : modelId;
}

QVariantMap skipEntry(const QString& person, const QString& sample, const QString& reason, const QString& models)
{
    return {{QStringLiteral("person"), person}, {QStringLiteral("sample"), sample},
            {QStringLiteral("reason"), reason}, {QStringLiteral("models"), models}};
}

// The core's skipped samples one item per sample (missing models merged), sorted by person;
// non-empty person -> only theirs.
QVariantList skipEntries(const processing::IVoiceModelsFacade* c, const QString& person = QString())
{
    QVariantList out;
    if (!c) return out;
    QStringList keys;
    QVector<processing::VoiceprintBackfillSkip> first;
    QHash<QString, QStringList> models;
    for (const processing::VoiceprintBackfillSkip& s : c->backfillSkips(person)) {
        if (!person.isEmpty() && s.person != person) continue;
        const QString key = s.person + QLatin1Char('|') + s.sourceMeetingId + QLatin1Char('|') + s.sampleRef;
        if (!models.contains(key)) { keys << key; first << s; }
        if (!models[key].contains(shortName(s.model))) models[key] << shortName(s.model);
    }
    QHash<QString, QString> titles;
    for (int i = 0; i < keys.size(); ++i) {
        const processing::VoiceprintBackfillSkip& s = first.at(i);
        QString title;
        if (s.reason == processing::VoiceprintBackfillSkip::Reason::MissingMeeting || s.sourceMeetingId.isEmpty()) {
            title = QCoreApplication::translate("VoiceModelListModel", "törölt megbeszélés");
        } else {
            if (!titles.contains(s.sourceMeetingId))
                titles.insert(s.sourceMeetingId, c->meetingTitle(s.sourceMeetingId));
            title = titles.value(s.sourceMeetingId);
        }
        // "<folder>/track.ogg#startMs-endMs" -> "12:03-12:06"
        QString range;
        const int hash = s.sampleRef.lastIndexOf(QLatin1Char('#'));
        if (hash >= 0) {
            const QStringList se = s.sampleRef.mid(hash + 1).split(QLatin1Char('-'));
            bool ok1 = false, ok2 = false;
            const qint64 a = se.value(0).toLongLong(&ok1), b = se.value(1).toLongLong(&ok2);
            if (ok1 && ok2) range = clock(a) + QStringLiteral("–") + clock(b);
        }
        if (title.isEmpty()) title = QFileInfo(s.sampleRef.left(hash >= 0 ? hash : s.sampleRef.size())).fileName();
        out << skipEntry(s.person, range.isEmpty() ? title : title + QStringLiteral(" · ") + range,
                         reasonText(s.reason), models.value(keys.at(i)).join(QStringLiteral(", ")));
    }
    return out;
}

} // namespace

VoiceModelListModel::VoiceModelListModel(QObject* parent) : QAbstractListModel(parent)
{
    m_downloader = new processing::VoiceModelDownloader(this);
    connect(m_downloader, &processing::VoiceModelDownloader::progress, this, &VoiceModelListModel::onProgress);
    connect(m_downloader, &processing::VoiceModelDownloader::finished, this, &VoiceModelListModel::onFinished);
    connect(m_downloader, &processing::VoiceModelDownloader::verified, this,
            [this](const QString& id, processing::VoiceModelDownloader::VerifyResult r, const QString&) { onVerified(id, int(r)); });
    rebuild();

    // Standalone instance (from QML, e.g. Getting started): the process's services, in the next
    // turn (by then QML has set demoState), unless an owner or a test gave a facade meanwhile.
    AppContext* ctx = AppContext::instance();
    connect(ctx, &AppContext::controllerChanged, this, &VoiceModelListModel::resolveServices);
    connect(ctx, &AppContext::demoChanged, this, &VoiceModelListModel::resolveServices);
    QMetaObject::invokeMethod(this, [this] {
        if (!m_facade) resolveServices();
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
            {NotRecommendedRole, "notRecommended"}, {CorruptRole, "corrupt"},
            {HasFileRole, "hasFile"},     {CanDeleteRole, "canDelete"}};
}

QVariant VoiceModelListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) return {};
    const Row& r = m_rows.at(index.row());
    const processing::VoiceModelSpec& s = r.spec;
    const bool def = s.id == kCamplus;
    switch (role) {
    case IdRole: return s.id;
    case NameRole: {
        // "CAM++ (3D-Speaker)": the parenthesized part shows in the source line anyway.
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
        // CC BY: the condition of use is attributing the authors (on the source page).
        return s.license.startsWith(QLatin1String("CC-BY")) ? tr("a szerzők megnevezésével") : QString();
    case SourceUrlRole: return s.sourceUrl;
    case SourceTextRole: {
        // Short form: "huggingface.co/Wespeaker/..." (the full address is in the link and the tooltip).
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
    case CanEnableRole: return usable(r.state);
    case RecommendedRole: return s.id == kWespeaker;
    case NotRecommendedRole: return s.id == kEres;
    case CorruptRole: return r.corrupt;
    case HasFileRole: return r.hasFile;
    case CanDeleteRole:
        return r.hasFile && r.state != QLatin1String("downloading") && r.state != QLatin1String("verifying");
    }
    return {};
}

// ---- facade / design state ----------------------------------------------------------------

bool VoiceModelListModel::live() const
{
    return m_dirsOverridden || (m_facade && m_facade->live());
}

void VoiceModelListModel::resolveServices()
{
    if (m_explicitFacade) return;
    processing::IVoiceModelsFacade* old = m_facade;
    m_facade = AppContext::instance()->services().voiceModels(m_demoState, this);
    if (old && old != m_facade && old->parent() == this) old->deleteLater();   // a fictional facade of ours
    attach();
}

void VoiceModelListModel::useServices(const UiServices& services, const QString& scenario)
{
    m_explicitFacade = true;
    const bool stateChanged = m_demoState != scenario;
    m_demoState = scenario;
    processing::IVoiceModelsFacade* old = m_facade;
    m_facade = services.voiceModels(scenario, this);
    if (old && old != m_facade && old->parent() == this) old->deleteLater();
    attach();
    if (stateChanged) emit demoStateChanged();
}

void VoiceModelListModel::setFacade(processing::IVoiceModelsFacade* facade)
{
    m_explicitFacade = true;
    if (m_facade == facade) return;
    processing::IVoiceModelsFacade* old = m_facade;
    m_facade = facade;
    if (old && old->parent() == this) old->deleteLater();
    attach();
}

void VoiceModelListModel::setDemoState(const QString& state)
{
    if (m_demoState == state && m_facade) return;
    m_demoState = state;
    // A fictional facade serves one design state: a new one for the new state.
    if (!m_explicitFacade && !(m_facade && m_facade->live())) resolveServices();
    emit demoStateChanged();
}

void VoiceModelListModel::attach()
{
    for (const QMetaObject::Connection& c : std::as_const(m_connections)) disconnect(c);
    m_connections.clear();
    processing::IVoiceModelsFacade* c = m_facade;
    if (!c) return;
    m_connections << connect(c, &processing::IVoiceModelsFacade::modelsChanged, this, [this] { refreshFiles(); });
    m_connections << connect(c, &processing::IVoiceModelsFacade::backfillStarted, this,
                             &VoiceModelListModel::onBackfillStarted);
    m_connections << connect(c, &processing::IVoiceModelsFacade::backfillProgress, this,
                             &VoiceModelListModel::onBackfillProgress);
    m_connections << connect(c, &processing::IVoiceModelsFacade::backfillFinished, this,
                             &VoiceModelListModel::onBackfillFinished);
    m_connections << connect(c, &processing::IVoiceModelsFacade::settingsChanged, this, [this] {
        if (!m_getDraft) notifyDraftChanged();   // standalone instance: the core's setting is the source
    });
    if (c->scenario()) {
        applyScenario();
        return;
    }
    m_bfState.clear();
    m_bfSeenRunning = false;
    m_bfSkips.clear();
    rebuild();
    refresh();
}

// The fictional facade's design state: the rows' and the backfill's state as given.
void VoiceModelListModel::applyScenario()
{
    const std::optional<processing::VoiceModelScenario> sc = m_facade ? m_facade->scenario() : std::nullopt;
    if (!sc) return;
    beginResetModel();
    m_rows.clear();
    const QVector<processing::VoiceModelSpec> specs = ordered(m_specOverride.isEmpty() ? processing::VoiceModelRegistry::builtin() : m_specOverride);
    for (const processing::VoiceModelSpec& s : specs) {
        Row r;
        r.spec = s;
        const processing::VoiceModelScenario::Row given = sc->rows.value(s.id);
        r.state = given.state;
        if (r.state == QLatin1String("downloading")) {
            r.total = s.sizeBytes;
            r.received = qint64(r.total * given.receivedFraction);
        }
        r.errorText = given.errorText;
        r.corrupt = given.corrupt;
        if (r.corrupt)
            r.errorText = tr("A fájl mérete eltér a várttól (%1 helyett %2) — töltsd le újra.")
                              .arg(SettingsViewModel::formatBytes(s.sizeBytes), SettingsViewModel::formatBytes(given.fileSize));
        r.hasFile = r.state == QLatin1String("downloaded") || r.state == QLatin1String("verifying") || r.corrupt;
        m_rows << r;
    }
    m_offline = sc->offline;
    endResetModel();

    m_bfSkips = skipEntries(m_facade);
    m_bfState.clear();   // the list may have changed too: signal in any case
    setBackfill(sc->backfillState, sc->backfillTotal, sc->backfillDone, sc->backfillSkipped);
    emit backfillChanged();
    emit summaryChanged();
}

void VoiceModelListModel::setSpecsForTest(const QVector<processing::VoiceModelSpec>& specs)
{
    m_specOverride = specs;
    if (live()) { rebuild(); refresh(); }
    else applyScenario();
}

void VoiceModelListModel::setDirsForTest(const QString& metaDir, const QString& appDir)
{
    m_metaOverride = metaDir;
    m_appOverride = appDir;
    m_dirsOverridden = true;
    refresh();
}

// ---- state ---------------------------------------------------------------------------

QString VoiceModelListModel::metaDir() const
{
    if (m_dirsOverridden) return m_metaOverride;
    return m_facade && m_facade->live() ? m_facade->modelsDir() : QString();
}

QString VoiceModelListModel::appDir() const
{
    if (m_dirsOverridden) return m_appOverride;
    return QCoreApplication::instance() ? QCoreApplication::applicationDirPath() : QString();
}

QStringList VoiceModelListModel::enabledIds() const
{
    if (m_getDraft) return m_getDraft();
    return m_facade ? m_facade->enabledModels() : QStringList();
}

void VoiceModelListModel::writeEnabled(const QStringList& ids)
{
    const QStringList norm = processing::VoiceModelRegistry::normalizeIds(ids);
    if (m_setDraft) m_setDraft(norm);
    else if (m_facade) m_facade->setEnabledModels(norm);
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

QString VoiceModelListModel::userPath(const processing::VoiceModelSpec& spec) const
{
    return processing::VoiceModelDownloader::targetPath(spec, metaDir());
}

void VoiceModelListModel::updateFileState(Row& r) const
{
    const QString meta = metaDir(), app = appDir();
    const QString user = processing::VoiceModelDownloader::targetPath(r.spec, meta);
    r.hasFile = !meta.isEmpty() && QFileInfo(user).isFile();
    if (m_downloader->isRunning(r.spec.id)) { r.state = QStringLiteral("downloading"); return; }
    const QString path = processing::VoiceModelRegistry::resolvePath(r.spec, meta, app);
    const QFileInfo fi(path);
    r.corrupt = false;
    if (fi.exists()) {
        r.errorText.clear();
        // Verification (size) is running: the pill shows "verifying...", the model is usable meanwhile.
        if (m_downloader->isVerifying(r.spec.id)) { r.state = QStringLiteral("verifying"); return; }
        // Windows: the base model is in <appDir>/models (the installer brings it); not downloadable.
        r.state = path != user ? QStringLiteral("bundled") : QStringLiteral("downloaded");
        if (r.state == QLatin1String("downloaded")
            && processing::VoiceModelDownloader::verifyFile(r.spec, path) == processing::VoiceModelDownloader::VerifyResult::SizeMismatch) {
            r.corrupt = true;
            r.state = QStringLiteral("error");
            r.errorText = tr("A fájl mérete eltér a várttól (%1 helyett %2) — töltsd le újra.")
                              .arg(SettingsViewModel::formatBytes(r.spec.sizeBytes),
                                   SettingsViewModel::formatBytes(fi.size()));
        }
        return;
    }
    // Missing: the latest download's error stays until the user retries.
    if (r.state == QLatin1String("error") && !r.errorText.isEmpty()) return;
    r.state = m_offline ? QStringLiteral("offline") : QStringLiteral("missing");
}

void VoiceModelListModel::rebuild()
{
    beginResetModel();
    QHash<QString, Row> old;
    for (const Row& r : std::as_const(m_rows)) old.insert(r.spec.id, r);
    m_rows.clear();
    const QVector<processing::VoiceModelSpec> specs = ordered(m_specOverride.isEmpty() ? processing::VoiceModelRegistry::builtin() : m_specOverride);
    for (const processing::VoiceModelSpec& s : specs) {
        Row r = old.value(s.id);
        r.spec = s;
        if (live()) updateFileState(r);
        m_rows << r;
    }
    endResetModel();
}

void VoiceModelListModel::refresh()
{
    if (!live()) return;   // fictional facade: the design state stays
    refreshFiles();
    verifyAllFiles();
    checkBackfill();
}

void VoiceModelListModel::verifyAllFiles()
{
    for (const Row& r : std::as_const(m_rows))
        if (r.state == QLatin1String("downloaded") || r.state == QLatin1String("bundled") || r.corrupt)
            verify(r.spec.id);
}

void VoiceModelListModel::verify(const QString& id)
{
    const int i = indexOf(id);
    if (i < 0) return;
    if (!live()) return;   // fictional facade
    Row& r = m_rows[i];
    const QString path = processing::VoiceModelRegistry::resolvePath(r.spec, metaDir(), appDir());
    if (!QFileInfo::exists(path)) return;
    if (!m_downloader->verify(r.spec, path)) return;   // already verifying / downloading
    updateFileState(r);
    emitRow(i);
    if (id == kWespeaker) emit summaryChanged();
}

void VoiceModelListModel::onVerified(const QString& id, int result)
{
    const int i = indexOf(id);
    if (i < 0) return;
    updateFileState(m_rows[i]);
    emitRow(i);
    emit summaryChanged();
    const auto r = processing::VoiceModelDownloader::VerifyResult(result);
    emit verifyFinished(id, r == processing::VoiceModelDownloader::VerifyResult::Ok || r == processing::VoiceModelDownloader::VerifyResult::Unverifiable);
}

QString VoiceModelListModel::deleteModel(const QString& id)
{
    const int i = indexOf(id);
    if (i < 0) return tr("Ismeretlen hangmodell.");
    Row& r = m_rows[i];
    if (r.state == QLatin1String("downloading") || r.state == QLatin1String("verifying"))
        return tr("Letöltés vagy ellenőrzés közben nem törölhető.");
    if (!r.hasFile) return tr("Ennek a modellnek nincs letöltött fájlja; a csomagban lévőt nem lehet törölni.");
    if (!live()) {
        // Fictional facade: only the state changes.
        r.state = QStringLiteral("missing");
        r.hasFile = false;
        r.corrupt = false;
        r.errorText.clear();
        emitRow(i);
        emit summaryChanged();
        return QString();
    }
    QString error;
    const bool ok = m_facade && m_facade->live() ? m_facade->deleteModelFile(id, &error)
                                                 : processing::VoiceModelDownloader::deleteModelFile(r.spec, metaDir(), &error);
    if (!ok) return tr("Nem sikerült törölni: %1").arg(error);
    r.errorText.clear();
    r.corrupt = false;
    refreshFiles();
    return QString();
}

void VoiceModelListModel::rescan()
{
    if (m_facade) m_facade->rescan();   // modelsChanged → refreshFiles
    refresh();
}

void VoiceModelListModel::refreshFiles()
{
    if (!live()) return;
    m_offline = !m_downloader->networkReachable();
    for (int i = 0; i < m_rows.size(); ++i) {
        updateFileState(m_rows[i]);
        emitRow(i);
    }
    if (m_rows.size() != (m_specOverride.isEmpty() ? processing::VoiceModelRegistry::builtin().size() : m_specOverride.size()))
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
        if (usable(r.state)) return true;
    return false;
}

int VoiceModelListModel::activeCount() const
{
    const QStringList on = enabledIds();
    int n = 0;
    for (const Row& r : m_rows)
        if (on.contains(r.spec.id) && usable(r.state)) ++n;
    return n;
}

bool VoiceModelListModel::fallbackActive() const
{
    // The same rule as VoiceModelRegistry::active()'s, per the draft.
    if (activeCount() > 0) return false;
    const int i = indexOf(kCamplus);
    return i >= 0 && usable(m_rows.at(i).state);
}

QString VoiceModelListModel::defaultState() const
{
    const int i = indexOf(kWespeaker);
    if (i < 0) return QString();
    // Verification (a moment) is not visible here: the Getting started row and the CTAs look at
    // whether the file exists; the card's pill shows "verifying...".
    const Row& r = m_rows.at(i);
    if (r.state == QLatin1String("verifying")) return r.hasFile ? QStringLiteral("downloaded") : QStringLiteral("bundled");
    return r.state;
}

QString VoiceModelListModel::defaultSizeText() const
{
    const auto s = processing::VoiceModelRegistry::spec(kWespeaker);
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

// ---- actions -------------------------------------------------------------------------

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
    if (r.state == QLatin1String("bundled") || r.state == QLatin1String("verifying") || m_downloader->isRunning(id)) return;
    if (!live()) {
        // Fictional facade: only the state changes.
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
    m_downloader->start(r.spec, processing::VoiceModelDownloader::targetPath(r.spec, metaDir()), overwrite);
    emit summaryChanged();
}

void VoiceModelListModel::cancel(const QString& id)
{
    if (m_downloader->isRunning(id)) {
        m_downloader->cancel(id);
        return;
    }
    const int i = indexOf(id);   // demo
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
    if (on && !usable(m_rows.at(i).state)) return;
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
    // We refresh per megabyte (the bar is still smooth; we do not flood QML).
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
        // Verification after the download (size): the pill shows "verifying...", then "downloaded" / error.
        verify(id);
        // It downloaded it to use it: we enable it in the draft (takes effect on Save).
        QStringList ids = enabledIds();
        if (!ids.contains(id)) {
            ids << id;
            writeEnabled(ids);
        } else if (m_facade && m_facade->live() && m_facade->enabledModels().contains(id)) {
            // It was already enabled (e.g. the base model): the core re-reads the file only on a
            // setting change; the facade recomputes the used models (and backfills).
            m_facade->reloadModels();
        }
    } else {
        switch (m_downloader->lastFailure(id)) {
        case processing::VoiceModelDownloader::Failure::Cancelled:
            r.state = QStringLiteral("missing");
            break;
        case processing::VoiceModelDownloader::Failure::Offline:
            m_offline = true;
            r.state = QStringLiteral("offline");
            break;
        case processing::VoiceModelDownloader::Failure::Exists:
            updateFileState(r);   // arrived from elsewhere (CLI) meanwhile
            break;
        case processing::VoiceModelDownloader::Failure::SizeMismatch:
            r.state = QStringLiteral("error");
            r.errorText = tr("A letöltött fájl mérete nem egyezik a várttal, ezért nem mentettem el.");
            break;
        case processing::VoiceModelDownloader::Failure::Write:
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

// ---- voiceprint backfill ------------------------------------------------------------

void VoiceModelListModel::setBackfill(const QString& state, int total, int done, int skipped)
{
    if (state == m_bfState && total == m_bfTotal && done == m_bfDone && skipped == m_bfSkipped) return;
    m_bfState = state;
    m_bfTotal = total;
    m_bfDone = done;
    m_bfSkipped = skipped;
    emit backfillChanged();
}

void VoiceModelListModel::loadSkips()
{
    m_bfSkips = skipEntries(m_facade);
}

// The initial state (new instance, e.g. the window reopened): whether it runs, whether a skipped sample remains.
void VoiceModelListModel::checkBackfill()
{
    if (!m_facade || !m_facade->live()) return;
    if (m_facade->backfillRunning()) {
        // k / N arrives with the next progress signal; until then indeterminate.
        m_bfSeenRunning = true;
        if (m_bfState != QLatin1String("running")) setBackfill(QStringLiteral("running"), 0, 0, 0);
        return;
    }
    if (m_bfState == QLatin1String("running") || m_bfState == QLatin1String("done")) return;
    loadSkips();
    m_bfState.clear();   // the list may have changed too
    if (!m_bfSkips.isEmpty()) setBackfill(QStringLiteral("partial"), m_bfTotal, m_bfDone, int(m_bfSkips.size()));
    else setBackfill(QString(), 0, 0, 0);
}

void VoiceModelListModel::onBackfillStarted(int total)
{
    m_bfSeenRunning = true;
    m_bfSkips.clear();
    setBackfill(QStringLiteral("running"), total, 0, 0);
}

void VoiceModelListModel::onBackfillProgress(int done, int total)
{
    m_bfSeenRunning = true;
    setBackfill(QStringLiteral("running"), total, qBound(0, done, total), 0);
}

void VoiceModelListModel::onBackfillFinished(int /*added*/, int /*skipped*/)
{
    if (!m_facade || !m_facade->live()) return;
    loadSkips();
    m_bfState.clear();   // the list changed too: signal in any case
    const int skipped = int(m_bfSkips.size());
    if (skipped > 0) setBackfill(QStringLiteral("partial"), m_bfTotal, m_bfTotal, skipped);
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
        return tr("%n minta nem volt pótolható az új modellel.", nullptr, m_bfSkipped);
    return QString();
}

// ---- BackfillSkipNote (People > details) --------------------------------------------

BackfillSkipNote::BackfillSkipNote(QObject* parent) : QObject(parent)
{
    // From QML: the process's services in the next turn (by then QML has set demoState).
    AppContext* ctx = AppContext::instance();
    connect(ctx, &AppContext::controllerChanged, this, &BackfillSkipNote::resolveServices);
    connect(ctx, &AppContext::demoChanged, this, &BackfillSkipNote::resolveServices);
    QMetaObject::invokeMethod(this, [this] {
        if (!m_facade) resolveServices();
    }, Qt::QueuedConnection);
}

void BackfillSkipNote::resolveServices()
{
    if (m_explicitFacade) return;
    processing::IVoiceModelsFacade* old = m_facade;
    m_facade = AppContext::instance()->services().voiceModels(m_demoState, this);
    if (old && old != m_facade && old->parent() == this) old->deleteLater();
    for (const QMetaObject::Connection& conn : std::as_const(m_connections)) disconnect(conn);
    m_connections.clear();
    if (m_facade) {
        m_connections << connect(m_facade, &processing::IVoiceModelsFacade::backfillStarted, this, [this] { reload(); });
        m_connections << connect(m_facade, &processing::IVoiceModelsFacade::backfillFinished, this, [this] { reload(); });
    }
    reload();
}

void BackfillSkipNote::setFacade(processing::IVoiceModelsFacade* facade)
{
    m_explicitFacade = true;
    for (const QMetaObject::Connection& conn : std::as_const(m_connections)) disconnect(conn);
    m_connections.clear();
    m_facade = facade;
    if (facade) {
        m_connections << connect(facade, &processing::IVoiceModelsFacade::backfillStarted, this, [this] { reload(); });
        m_connections << connect(facade, &processing::IVoiceModelsFacade::backfillFinished, this, [this] { reload(); });
    }
    reload();
}

void BackfillSkipNote::setPerson(const QString& p)
{
    if (m_person == p) return;
    m_person = p;
    emit personChanged();
    reload();
}

void BackfillSkipNote::setDemoState(const QString& s)
{
    if (m_demoState == s) return;
    m_demoState = s;
    // A fictional facade serves one design state: a new one for the new state.
    if (!m_explicitFacade && !(m_facade && m_facade->live())) resolveServices();
    else reload();
}

void BackfillSkipNote::reload()
{
    m_entries = m_person.isEmpty() || !m_facade ? QVariantList() : skipEntries(m_facade, m_person);
    emit changed();
}

QString BackfillSkipNote::text() const
{
    if (m_entries.isEmpty()) return QString();
    // A single reason -> the reason goes into the sentence; mixed -> generic.
    QString reason = m_entries.first().toMap().value(QStringLiteral("reason")).toString();
    for (const QVariant& e : m_entries)
        if (e.toMap().value(QStringLiteral("reason")).toString() != reason) { reason.clear(); break; }
    const QString missing = reasonText(processing::VoiceprintBackfillSkip::Reason::MissingAudio);
    if (reason.startsWith(missing)) reason = missing;   // the deleted meeting counts as a missing recording
    return reason.isEmpty() ? tr("%n minta nem pótolható az új modellel", nullptr, int(m_entries.size()))
                            : tr("%n minta nem pótolható: %1", nullptr, int(m_entries.size())).arg(reason);
}

QString BackfillSkipNote::detail() const
{
    QStringList lines;
    for (const QVariant& e : m_entries) {
        const QVariantMap m = e.toMap();
        lines << QStringLiteral("%1 — %2 (%3)").arg(m.value(QStringLiteral("sample")).toString(),
                                                   m.value(QStringLiteral("reason")).toString(),
                                                   m.value(QStringLiteral("models")).toString());
    }
    return lines.join(QLatin1Char('\n'));
}

} // namespace tanara_qml
