#include "PathsCommands.h"

#include "tanara/app/LegacySettings.h"
#include "tanara/app/SettingsManager.h"
#include "tanara/foundation/paths/AppPaths.h"
#include "tanara/foundation/secrets/SecretStore.h"
#include "tanara/foundation/settings/SettingsStore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

namespace tanara::cli {

namespace {

QTextStream& out() { static QTextStream s(stdout); return s; }
QTextStream& err() { static QTextStream s(stderr); return s; }

QString native(const QString& p) { return QDir::toNativeSeparators(p); }

QString optionValue(const QStringList& args, const QString& name)
{
    const int i = args.indexOf(name);
    return i >= 0 ? args.value(i + 1) : QString();
}

// The settings currently stored in `file` (v2), or the defaults (no file / old / unreadable).
// Reads only: nothing is written.
app::AppSettings storedSettings(const QString& file, SettingsStore::LoadResult* result = nullptr)
{
    SettingsStore store(file);
    const SettingsStore::LoadResult r = store.load();
    if (result) *result = r;
    app::AppSettings s = app::SettingsManager::defaults();
    if (r == SettingsStore::LoadResult::Ok) {
        for (const QString& name : app::AppSettings::sectionNames())
            s.setSectionJson(name, store.section(name));
        app::SettingsManager::applyDefaults(s);
    }
    return s;
}

QJsonObject readJsonObject(const QString& file, QString* error)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("cannot read ") + native(file);
        return {};
    }
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        *error = QStringLiteral("not a JSON object: ") + native(file);
        return {};
    }
    return doc.object();
}

// Copies the target aside before it is replaced: <file>.bak-<timestamp>. Empty = nothing to back up.
bool backup(const QString& file, QString* backupPath)
{
    backupPath->clear();
    if (!QFile::exists(file)) return true;
    *backupPath = file + QStringLiteral(".bak-") + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    return QFile::copy(file, *backupPath);
}

bool writeSettings(const QString& file, const app::AppSettings& s)
{
    SettingsStore store(file);
    store.load();   // an old / unreadable file is set aside by save() (besides our .bak copy)
    for (const QString& name : app::AppSettings::sectionNames())
        store.setSection(name, s.sectionJson(name));
    return store.save();
}

void printSection(const QString& name, const QJsonObject& o)
{
    out() << "  \"" << name << "\": "
          << QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Indented)).trimmed().replace(
                 QLatin1Char('\n'), QStringLiteral("\n  "))
          << "\n";
}

int pathsShow()
{
    const paths::AppPaths base = paths::AppPaths::system();
    SettingsStore::LoadResult r{};
    const app::AppSettings s = storedSettings(base.settingsFile(), &r);
    const paths::AppPaths p = paths::AppPaths::system(s.paths);
    out() << "settings file: " << native(p.settingsFile());
    switch (r) {
    case SettingsStore::LoadResult::Missing:    out() << "  (not created yet: defaults)\n"; break;
    case SettingsStore::LoadResult::OldFormat:  out() << "  (OLD format: defaults; see `tanara-cli settings import-legacy`)\n"; break;
    case SettingsStore::LoadResult::Unreadable: out() << "  (unreadable: defaults)\n"; break;
    case SettingsStore::LoadResult::Ok:         out() << "\n"; break;
    }
    if (p.sandboxed()) out() << "TANARA_HOME:   " << native(paths::homeOverride()) << "  (overrides every folder)\n";
    out() << "\n";
    const QString head = QStringLiteral("%1%2%3\n").arg(QStringLiteral("ROLE"), -12).arg(QStringLiteral("SOURCE"), -13).arg(QStringLiteral("PATH"));
    out() << head;
    for (paths::Role role : {paths::Role::Config, paths::Role::Data, paths::Role::State, paths::Role::Logs,
                             paths::Role::Models, paths::Role::Recordings, paths::Role::Notes})
        out() << QStringLiteral("%1%2%3\n").arg(paths::roleName(role), -12)
                     .arg(paths::sourceName(p.source(role)), -13).arg(native(p.dir(role)));
    const QStringList roots = p.recordingRoots();
    for (int i = 1; i < roots.size(); ++i)
        out() << QStringLiteral("%1%2%3  (not used yet)\n").arg(QString(), -12).arg(QString(), -13).arg(native(roots.at(i)));
    out() << "\nsecrets:       backend setting \"" << s.secrets.backend << "\""
          << (secrets::keychainAllowed() ? QString() : QStringLiteral(" (keychain not available here: file)"))
          << "; file fallback " << native(p.dataFile(paths::files::Secrets)) << "\n";
    out().flush();
    return 0;
}

int pathsAdoptLegacy(const QStringList& args)
{
    const bool dryRun = args.contains(QStringLiteral("--dry-run"));
    if (!paths::homeOverride().isEmpty()) {
        err() << "TANARA_HOME is set: every folder is the sandbox, a \"paths\" section would be ignored.\n"
                 "Run adopt-legacy without TANARA_HOME.\n";
        return 2;
    }
    const QString legacyDir = paths::normalize(
        optionValue(args, QStringLiteral("--from")).isEmpty() ? QStringLiteral("~/.tanara")
                                                              : optionValue(args, QStringLiteral("--from")));
    const QString legacyFile = QDir(legacyDir).filePath(paths::files::Settings);
    QJsonObject flat;
    if (QFile::exists(legacyFile)) {
        QString error;
        flat = readJsonObject(legacyFile, &error);
        if (!error.isEmpty()) { err() << error << "\n"; return 1; }
        if (!app::legacy::isFlat(flat)) {
            err() << native(legacyFile) << " is already in the new format; nothing to adopt from it.\n";
            flat = QJsonObject();
        }
    } else if (!QDir(legacyDir).exists()) {
        err() << "No legacy folder: " << native(legacyDir) << "\n";
        return 1;
    }

    const paths::AppPaths platform = paths::AppPaths::system();
    const QString target = platform.settingsFile();
    if (QFileInfo(target).absoluteFilePath() == QFileInfo(legacyFile).absoluteFilePath()) {
        err() << "The legacy settings file is the target itself (" << native(target) << "); nothing to do.\n";
        return 2;
    }
    app::AppSettings s = storedSettings(target);
    const paths::PathSettings legacy = app::legacy::legacyPaths(flat, QDir::homePath());
    s.paths = legacy;
    const paths::AppPaths result = paths::AppPaths::system(s.paths);

    out() << (dryRun ? "DRY RUN - nothing is written.\n" : "") << "legacy folder:  " << native(legacyDir)
          << (flat.isEmpty() ? "  (no old settings.json: the default legacy folders)\n" : "\n")
          << "settings file:  " << native(target) << (QFile::exists(target) ? "  (exists: other sections kept)\n" : "  (new)\n")
          << "\nnew \"paths\" section:\n";
    printSection(QStringLiteral("paths"), s.paths.toJson());
    out() << "\nresulting folders:\n";
    for (paths::Role role : {paths::Role::Data, paths::Role::Models, paths::Role::Recordings, paths::Role::Notes}) {
        const QString d = result.dir(role);
        out() << QStringLiteral("  %1%2%3\n").arg(paths::roleName(role), -12).arg(native(d))
                     .arg(QDir(d).exists() ? QString() : QStringLiteral("  (does not exist!)"));
    }
    out() << "\nNothing is moved or deleted. Not adopted (they start fresh in the new places):\n"
          << "  state / logs / locks -> " << native(result.stateDir()) << "\n"
          << "  (state.json, ui-state.json, ui-transcript.json, recorder.ini, logs/ stay in " << native(legacyDir) << ")\n";
    if (QFile::exists(result.dataFile(paths::files::Secrets)))
        out() << "  secrets.json: found in the data folder; with the keychain backend the next start imports it\n"
                 "  and renames it to secrets.json.imported.\n";
    out() << "Next: `tanara-cli settings import-legacy` takes the other old settings over.\n";
    if (dryRun) { out().flush(); return 0; }

    QString bak;
    if (!backup(target, &bak)) { err() << "Backup failed: " << native(target) << "\n"; return 1; }
    if (!writeSettings(target, s)) { err() << "Writing failed: " << native(target) << "\n"; return 1; }
    if (!bak.isEmpty()) out() << "\nbackup:  " << native(bak) << "\n";
    out() << "written: " << native(target) << "\n";
    out().flush();
    return 0;
}

int settingsImportLegacy(const QStringList& args)
{
    const bool dryRun = args.contains(QStringLiteral("--dry-run"));
    const paths::AppPaths platform = paths::AppPaths::system();
    const QString target = platform.settingsFile();
    SettingsStore::LoadResult r{};
    const app::AppSettings current = storedSettings(target, &r);

    // Source: --from, otherwise the old file next to the data ("~/.tanara/settings.json" after
    // adopt-legacy), otherwise the target itself if it is still in the old format (sandbox).
    QString from = optionValue(args, QStringLiteral("--from"));
    if (from.isEmpty()) {
        const QString inData = paths::AppPaths::system(current.paths).dataFile(paths::files::Settings);
        const QString inLegacy = paths::normalize(QStringLiteral("~/.tanara/settings.json"));
        if (r == SettingsStore::LoadResult::OldFormat) from = target;
        else if (inData != target && QFile::exists(inData)) from = inData;
        else if (inLegacy != target && QFile::exists(inLegacy) && platform.sandboxed() == false) from = inLegacy;
    }
    if (from.isEmpty()) { err() << "No old settings.json found; give it with --from <file>.\n"; return 1; }
    from = paths::normalize(from);
    QString error;
    const QJsonObject flat = readJsonObject(from, &error);
    if (!error.isEmpty()) { err() << error << "\n"; return 1; }
    if (!app::legacy::isFlat(flat)) { err() << native(from) << " is not an old (flat) settings file.\n"; return 2; }

    app::AppSettings s = app::legacy::importFlat(flat, current);
    app::SettingsManager::applyDefaults(s);
    out() << (dryRun ? "DRY RUN - nothing is written.\n" : "") << "from:   " << native(from) << "\n"
          << "into:   " << native(target) << (QFile::exists(target) ? "\n" : "  (new)\n")
          << "\nsections after the import (\"paths\" and \"secrets\" are left as they are; API keys are not settings):\n";
    for (const QString& name : app::AppSettings::sectionNames())
        if (name != QLatin1String("paths") && name != QLatin1String("secrets") && s.sectionJson(name) != current.sectionJson(name))
            printSection(name, s.sectionJson(name));
    if (dryRun) { out().flush(); return 0; }

    QString bak;
    if (!backup(target, &bak)) { err() << "Backup failed: " << native(target) << "\n"; return 1; }
    if (!writeSettings(target, s)) { err() << "Writing failed: " << native(target) << "\n"; return 1; }
    if (!bak.isEmpty()) out() << "\nbackup:  " << native(bak) << "\n";
    out() << "written: " << native(target) << "\n";
    out().flush();
    return 0;
}

} // namespace

int runPathsCommand(const QStringList& args)
{
    const QString sub = args.value(2);
    if (sub == QLatin1String("show")) return pathsShow();
    if (sub == QLatin1String("adopt-legacy")) return pathsAdoptLegacy(args);
    err() << "Usage: paths show | paths adopt-legacy [--from <legacy dir, default ~/.tanara>] [--dry-run]\n";
    return 2;
}

int runSettingsCommand(const QStringList& args)
{
    if (args.value(2) == QLatin1String("import-legacy")) return settingsImportLegacy(args);
    err() << "Usage: settings import-legacy [--from <old settings.json>] [--dry-run]\n";
    return 2;
}

} // namespace tanara::cli
