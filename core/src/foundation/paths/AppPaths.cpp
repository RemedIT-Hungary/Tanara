#include "tanara/foundation/paths/AppPaths.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QMutex>
#include <QStandardPaths>
#include <QSysInfo>

#include <optional>

namespace tanara::paths {

// ---- PathSettings --------------------------------------------------------------------------

QString PathSettings::primaryRecordingRoot() const
{
    for (const RecordingRoot& r : recordingRoots)
        if (r.primary && !r.path.trimmed().isEmpty()) return r.path.trimmed();
    for (const RecordingRoot& r : recordingRoots)
        if (!r.path.trimmed().isEmpty()) return r.path.trimmed();
    return {};
}

void PathSettings::setPrimaryRecordingRoot(const QString& path)
{
    QList<RecordingRoot> rest;
    for (const RecordingRoot& r : recordingRoots)
        if (!r.primary && !r.path.trimmed().isEmpty() && r.path.trimmed() != path.trimmed()) rest << r;
    recordingRoots.clear();
    if (!path.trimmed().isEmpty()) recordingRoots << RecordingRoot{path.trimmed(), true};
    recordingRoots << rest;
}

QJsonObject PathSettings::toJson() const
{
    QJsonArray roots;
    for (const RecordingRoot& r : recordingRoots)
        roots.append(QJsonObject{{QStringLiteral("path"), r.path}, {QStringLiteral("primary"), r.primary}});
    return QJsonObject{
        {QStringLiteral("dataDir"), dataDir},
        {QStringLiteral("modelsDir"), modelsDir},
        {QStringLiteral("notesDir"), notesDir},
        {QStringLiteral("recordingRoots"), roots},
    };
}

PathSettings PathSettings::fromJson(const QJsonObject& o)
{
    PathSettings p;
    p.dataDir   = o.value(QStringLiteral("dataDir")).toString().trimmed();
    p.modelsDir = o.value(QStringLiteral("modelsDir")).toString().trimmed();
    p.notesDir  = o.value(QStringLiteral("notesDir")).toString().trimmed();
    for (const QJsonValue& v : o.value(QStringLiteral("recordingRoots")).toArray()) {
        const QJsonObject r = v.toObject();
        const QString path = r.value(QStringLiteral("path")).toString().trimmed();
        if (!path.isEmpty())
            p.recordingRoots << RecordingRoot{path, r.value(QStringLiteral("primary")).toBool(false)};
    }
    return p;
}

// ---- helpers --------------------------------------------------------------------------------

QString expandHome(const QString& path)
{
    if (path == QLatin1String("~"))
        return QDir::homePath();
    if (path.startsWith(QLatin1String("~/")) || path.startsWith(QLatin1String("~\\")))
        return QDir(QDir::homePath()).filePath(path.mid(2));
    return path;
}

namespace {

QString expandWith(const QString& path, const QString& home)
{
    if (path == QLatin1String("~")) return home;
    if (path.startsWith(QLatin1String("~/")) || path.startsWith(QLatin1String("~\\")))
        return QDir(home).filePath(path.mid(2));
    return path;
}

QString normalizeWith(const QString& path, const QString& home, const QString& relativeTo)
{
    const QString t = path.trimmed();
    if (t.isEmpty()) return {};
    QString p = expandWith(t, home);
    if (QDir::isRelativePath(p))
        p = QDir(relativeTo.isEmpty() ? home : relativeTo).filePath(p);
    return QDir::cleanPath(p);
}

QString join(const QString& a, const QString& b) { return QDir::cleanPath(QDir(a).filePath(b)); }

} // namespace

QString normalize(const QString& path, const QString& relativeTo)
{
    return normalizeWith(path, QDir::homePath(), relativeTo);
}

QString homeOverride()
{
    const QString raw = qEnvironmentVariable("TANARA_HOME").trimmed();
    if (raw.isEmpty())
        return QString();
    // Relative path -> absolute, against the startup working directory.
    return QDir::cleanPath(QDir(expandHome(raw)).absolutePath());
}

Platform hostPlatform()
{
    // Run-time check (no platform macros outside platform/<os>/, see tools/check_modules.py).
    const QString os = QSysInfo::productType();
    if (os == QLatin1String("windows")) return Platform::Windows;
    if (os == QLatin1String("macos") || os == QLatin1String("osx")) return Platform::MacOS;
    return Platform::Linux;
}

QString roleName(Role r)
{
    switch (r) {
    case Role::Config:     return QStringLiteral("config");
    case Role::Data:       return QStringLiteral("data");
    case Role::State:      return QStringLiteral("state");
    case Role::Logs:       return QStringLiteral("logs");
    case Role::Models:     return QStringLiteral("models");
    case Role::Recordings: return QStringLiteral("recordings");
    case Role::Notes:      return QStringLiteral("notes");
    }
    return {};
}

QString sourceName(Source s)
{
    switch (s) {
    case Source::Default:    return QStringLiteral("default");
    case Source::Settings:   return QStringLiteral("settings");
    case Source::TanaraHome: return QStringLiteral("TANARA_HOME");
    }
    return {};
}

PlatformBase systemBase()
{
    PlatformBase b;
    b.platform = hostPlatform();
    b.home = QDir::homePath();
    b.documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (b.documents.isEmpty()) b.documents = join(b.home, QStringLiteral("Documents"));
    switch (b.platform) {
    case Platform::Windows: {
        // Roaming %APPDATA% and %LOCALAPPDATA% directly: QStandardPaths' generic locations both
        // point to Local, and AppDataLocation would add the organization name.
        auto env = [&b](const char* name, const QString& fallback) {
            const QString v = qEnvironmentVariable(name).trimmed();
            return v.isEmpty() ? join(b.home, fallback) : QDir::cleanPath(QDir::fromNativeSeparators(v));
        };
        b.configBase = env("APPDATA", QStringLiteral("AppData/Roaming"));
        b.dataBase = b.configBase;
        b.localBase = env("LOCALAPPDATA", QStringLiteral("AppData/Local"));
        b.stateBase = b.localBase;
        break;
    }
    case Platform::MacOS: {
        const QString support = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
        b.configBase = b.dataBase = b.stateBase = support;
        b.logBase = join(b.home, QStringLiteral("Library/Logs"));
        break;
    }
    case Platform::Linux:
        // Generic locations honor XDG_CONFIG_HOME / XDG_DATA_HOME / XDG_STATE_HOME (and the
        // Qt test mode).
        b.configBase = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
        b.dataBase = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
        b.stateBase = QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation);
        if (b.stateBase.isEmpty()) b.stateBase = join(b.home, QStringLiteral(".local/state"));
        break;
    }
    return b;
}

// ---- AppPaths -------------------------------------------------------------------------------

AppPaths AppPaths::resolve(const PlatformBase& base, const PathSettings& settings,
                           const QString& tanaraHome)
{
    AppPaths p;
    const QString H = tanaraHome.trimmed().isEmpty() ? QString() : QDir::cleanPath(tanaraHome.trimmed());
    if (!H.isEmpty()) {
        p.m_sandboxed = true;
        p.m_config = p.m_data = p.m_state = H;
        p.m_logs = join(H, QStringLiteral("logs"));
        p.m_models = join(H, QStringLiteral("models"));
        p.m_notes = join(H, QStringLiteral("notes"));
        // The recording roots of the "paths" section count only INSIDE the sandbox (a relative path is
        // taken relative to it), so a sandbox can try out several roots but never reaches real data.
        auto inSandbox = [&H](const QString& v) { return v == H || v.startsWith(H + QLatin1Char('/')); };
        const QString primary = normalizeWith(settings.primaryRecordingRoot(), base.home, H);
        p.m_roots = {inSandbox(primary) ? primary : join(H, QStringLiteral("recordings"))};
        for (const RecordingRoot& r : settings.recordingRoots) {
            const QString v = normalizeWith(r.path, base.home, H);
            if (!v.isEmpty() && inSandbox(v) && !p.m_roots.contains(v)) p.m_roots << v;
        }
        p.m_dataSrc = p.m_modelsSrc = p.m_notesSrc = p.m_rootsSrc = Source::TanaraHome;
        return p;
    }

    // Platform defaults.
    const QString docs = join(base.documents, QStringLiteral("Tanara"));
    switch (base.platform) {
    case Platform::Linux:
        p.m_config = join(base.configBase, QStringLiteral("tanara"));
        p.m_data = join(base.dataBase, QStringLiteral("tanara"));
        p.m_state = join(base.stateBase, QStringLiteral("tanara"));
        p.m_logs = join(p.m_state, QStringLiteral("logs"));
        p.m_models = join(p.m_data, QStringLiteral("models"));
        break;
    case Platform::Windows:
        p.m_config = join(base.configBase, QStringLiteral("Tanara"));
        p.m_data = join(base.dataBase, QStringLiteral("Tanara/data"));
        p.m_state = join(base.stateBase, QStringLiteral("Tanara"));
        p.m_logs = join(p.m_state, QStringLiteral("logs"));
        p.m_models = join(base.localBase, QStringLiteral("Tanara/models"));
        break;
    case Platform::MacOS:
        p.m_config = join(base.configBase, QStringLiteral("Tanara"));
        p.m_data = join(base.dataBase, QStringLiteral("Tanara/data"));
        p.m_state = join(base.stateBase, QStringLiteral("Tanara/state"));
        p.m_logs = join(base.logBase, QStringLiteral("Tanara"));
        p.m_models = join(base.dataBase, QStringLiteral("Tanara/models"));
        break;
    }
    p.m_notes = join(docs, QStringLiteral("notes"));
    const QString defaultRoot = join(docs, QStringLiteral("recordings"));

    // The "paths" settings section overrides data / models / notes / recordings.
    auto apply = [&](const QString& configured, QString& target, Source& src) {
        const QString v = normalizeWith(configured, base.home, base.home);
        if (v.isEmpty()) return;
        target = v;
        src = Source::Settings;
    };
    apply(settings.dataDir, p.m_data, p.m_dataSrc);
    apply(settings.modelsDir, p.m_models, p.m_modelsSrc);
    apply(settings.notesDir, p.m_notes, p.m_notesSrc);

    const QString primary = normalizeWith(settings.primaryRecordingRoot(), base.home, base.home);
    if (!primary.isEmpty()) {
        p.m_roots << primary;
        p.m_rootsSrc = Source::Settings;
    } else {
        p.m_roots << defaultRoot;
    }
    for (const RecordingRoot& r : settings.recordingRoots) {
        const QString v = normalizeWith(r.path, base.home, base.home);
        if (!v.isEmpty() && !p.m_roots.contains(v)) p.m_roots << v;
    }
    return p;
}

AppPaths AppPaths::system(const PathSettings& settings)
{
    return resolve(systemBase(), settings, homeOverride());
}

QString AppPaths::dir(Role r) const
{
    switch (r) {
    case Role::Config:     return m_config;
    case Role::Data:       return m_data;
    case Role::State:      return m_state;
    case Role::Logs:       return m_logs;
    case Role::Models:     return m_models;
    case Role::Recordings: return primaryRecordingRoot();
    case Role::Notes:      return m_notes;
    }
    return {};
}

Source AppPaths::source(Role r) const
{
    if (m_sandboxed) return Source::TanaraHome;
    switch (r) {
    case Role::Data:       return m_dataSrc;
    case Role::Models:     return m_modelsSrc;
    case Role::Notes:      return m_notesSrc;
    case Role::Recordings: return m_rootsSrc;
    default:               return Source::Default;
    }
}

QString AppPaths::configFile(const QString& name) const { return QDir(m_config).filePath(name); }
QString AppPaths::dataFile(const QString& name) const { return QDir(m_data).filePath(name); }
QString AppPaths::stateFile(const QString& name) const { return QDir(m_state).filePath(name); }
QString AppPaths::settingsFile() const { return configFile(files::Settings); }

void AppPaths::ensureDirs() const
{
    for (const QString& d : {m_config, m_data, m_state, m_notes, primaryRecordingRoot()})
        if (!d.isEmpty()) QDir().mkpath(d);
}

// ---- process-wide current -------------------------------------------------------------------

namespace {
QMutex g_mutex;
std::optional<AppPaths> g_current;
QString g_currentHome;   // the TANARA_HOME the published paths were resolved with
} // namespace

AppPaths current()
{
    const QString home = homeOverride();
    QMutexLocker lock(&g_mutex);
    // Nothing published yet, or TANARA_HOME changed since (tests switch sandboxes): resolve anew.
    if (!g_current || g_currentHome != home) return AppPaths::resolve(systemBase(), PathSettings(), home);
    return *g_current;
}

void setCurrent(const AppPaths& p)
{
    const QString home = homeOverride();
    QMutexLocker lock(&g_mutex);
    g_current = p;
    g_currentHome = home;
}

} // namespace tanara::paths
