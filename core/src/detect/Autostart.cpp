#include "tanara/detect/Autostart.h"
#include "tanara/Paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>

#if defined(Q_OS_WIN)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <string>
#endif

namespace tanara::autostart {

QString watcherEntryPath()
{
#if defined(Q_OS_LINUX)
    return QDir(QDir::homePath()).filePath(QStringLiteral(".config/autostart/tanara-watcher.desktop"));
#elif defined(Q_OS_WIN)
    return QStringLiteral("HKEY_CURRENT_USER\\%1\\%2")
        .arg(QLatin1String(kRunSubKey), QLatin1String(kRunValueName));
#else
    return QString();
#endif
}

bool isWatcherEnabled()
{
#if defined(Q_OS_WIN)
    return !readRunValue(QLatin1String(kRunSubKey), QLatin1String(kRunValueName)).isEmpty();
#else
    const QString path = watcherEntryPath();
    return !path.isEmpty() && QFile::exists(path);
#endif
}

bool managed()
{
    if (!paths::homeOverride().isEmpty()) return false;      // homokozó: a valódi bejegyzés tabu
    if (QStandardPaths::isTestModeEnabled()) return false;
    return !watcherEntryPath().isEmpty();
}

QString findWatcherExecutable(const QString& applicationDir)
{
#if defined(Q_OS_WIN)
    const QString exe = QStringLiteral("tanara-watcher.exe");
#else
    const QString exe = QStringLiteral("tanara-watcher");
#endif
    for (const QString& cand : { QDir(applicationDir).filePath(exe),
                                 QDir(applicationDir).filePath(QStringLiteral("../watcher/") + exe) }) {
        if (QFileInfo(cand).isFile())
            return QFileInfo(cand).absoluteFilePath();
    }
    return QString();
}

bool writeEntry(const QString& entryPath, const QString& watcherExecutable)
{
    if (entryPath.isEmpty() || watcherExecutable.isEmpty()) return false;
    QDir().mkpath(QFileInfo(entryPath).absolutePath());
    QFile f(entryPath);
    // .desktop-fájl: LF sorvég kell (Text mód nélkül — Windowson az CRLF-et írna).
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    QTextStream(&f)
        << "[Desktop Entry]\n"
        << "Type=Application\n"
        << "Name=Tanara Watcher\n"
        << "Comment=Meeting-figyelő a rendszertálcán\n"
        << "Exec=" << watcherExecutable << "\n"
        << "Terminal=false\n"
        << "X-GNOME-Autostart-enabled=true\n";
    return true;
}

bool applyWatcher(bool on, const QString& watcherExecutable)
{
    if (!managed()) return false;
#if defined(Q_OS_WIN)
    const QString key = QLatin1String(kRunSubKey);
    const QString name = QLatin1String(kRunValueName);
    if (!on)
        return removeRunValue(key, name);
    return writeRunValue(key, name, watcherExecutable);
#else
    const QString path = watcherEntryPath();
    if (!on) {
        QFile::remove(path);
        return true;
    }
    return writeEntry(path, watcherExecutable);
#endif
}

// ---- Windows Run-kulcs -------------------------------------------------------------------

#if defined(Q_OS_WIN)
namespace {
const wchar_t* wc(const QString& s) { return reinterpret_cast<const wchar_t*>(s.utf16()); }
} // namespace
#endif

bool writeRunValue(const QString& subKey, const QString& valueName, const QString& watcherExecutable)
{
#if defined(Q_OS_WIN)
    if (subKey.isEmpty() || valueName.isEmpty() || watcherExecutable.isEmpty()) return false;
    // Idézőjeles, natív elválasztós út: a "Program Files"-féle szóközös utak miatt kell.
    const QString cmd = QLatin1Char('"') + QDir::toNativeSeparators(watcherExecutable) + QLatin1Char('"');
    HKEY k = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, wc(subKey), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr)
        != ERROR_SUCCESS)
        return false;
    const DWORD bytes = DWORD((cmd.size() + 1) * sizeof(wchar_t));
    const LONG rc = RegSetValueExW(k, wc(valueName), 0, REG_SZ, reinterpret_cast<const BYTE*>(wc(cmd)), bytes);
    RegCloseKey(k);
    return rc == ERROR_SUCCESS;
#else
    Q_UNUSED(subKey); Q_UNUSED(valueName); Q_UNUSED(watcherExecutable);
    return false;
#endif
}

QString readRunValue(const QString& subKey, const QString& valueName)
{
#if defined(Q_OS_WIN)
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, wc(subKey), 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS)
        return QString();
    DWORD type = 0, bytes = 0;
    QString out;
    if (RegQueryValueExW(k, wc(valueName), nullptr, &type, nullptr, &bytes) == ERROR_SUCCESS
        && (type == REG_SZ || type == REG_EXPAND_SZ) && bytes > 0) {
        std::wstring buf(bytes / sizeof(wchar_t) + 1, L'\0');
        if (RegQueryValueExW(k, wc(valueName), nullptr, &type, reinterpret_cast<BYTE*>(buf.data()), &bytes)
            == ERROR_SUCCESS)
            out = QString::fromWCharArray(buf.c_str());   // a záró NUL-ig
    }
    RegCloseKey(k);
    return out;
#else
    Q_UNUSED(subKey); Q_UNUSED(valueName);
    return QString();
#endif
}

bool removeRunValue(const QString& subKey, const QString& valueName)
{
#if defined(Q_OS_WIN)
    HKEY k = nullptr;
    const LONG open = RegOpenKeyExW(HKEY_CURRENT_USER, wc(subKey), 0, KEY_SET_VALUE, &k);
    if (open == ERROR_FILE_NOT_FOUND) return true;
    if (open != ERROR_SUCCESS) return false;
    const LONG rc = RegDeleteValueW(k, wc(valueName));
    RegCloseKey(k);
    return rc == ERROR_SUCCESS || rc == ERROR_FILE_NOT_FOUND;
#else
    Q_UNUSED(subKey); Q_UNUSED(valueName);
    return false;
#endif
}

} // namespace tanara::autostart
