// IAutostart on Windows: the HKCU Run key.
#include "tanara/watcher/detect/Autostart.h"
#include "tanara/watcher/platform/windows/RunKey.h"

#include <QDir>

#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>
#include <string>

namespace tanara::watcher {

namespace autostart {

namespace {
const wchar_t* wc(const QString& s) { return reinterpret_cast<const wchar_t*>(s.utf16()); }
} // namespace

bool writeRunValue(const QString& subKey, const QString& valueName, const QString& watcherExecutable)
{
    if (subKey.isEmpty() || valueName.isEmpty() || watcherExecutable.isEmpty()) return false;
    // A quoted path with native separators: needed because of "Program Files"-style paths with spaces.
    const QString cmd = QLatin1Char('"') + QDir::toNativeSeparators(watcherExecutable) + QLatin1Char('"');
    HKEY k = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, wc(subKey), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr)
        != ERROR_SUCCESS)
        return false;
    const DWORD bytes = DWORD((cmd.size() + 1) * sizeof(wchar_t));
    const LONG rc = RegSetValueExW(k, wc(valueName), 0, REG_SZ, reinterpret_cast<const BYTE*>(wc(cmd)), bytes);
    RegCloseKey(k);
    return rc == ERROR_SUCCESS;
}

QString readRunValue(const QString& subKey, const QString& valueName)
{
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
            out = QString::fromWCharArray(buf.c_str());   // up to the terminating NUL
    }
    RegCloseKey(k);
    return out;
}

bool removeRunValue(const QString& subKey, const QString& valueName)
{
    HKEY k = nullptr;
    const LONG open = RegOpenKeyExW(HKEY_CURRENT_USER, wc(subKey), 0, KEY_SET_VALUE, &k);
    if (open == ERROR_FILE_NOT_FOUND) return true;
    if (open != ERROR_SUCCESS) return false;
    const LONG rc = RegDeleteValueW(k, wc(valueName));
    RegCloseKey(k);
    return rc == ERROR_SUCCESS || rc == ERROR_FILE_NOT_FOUND;
}

} // namespace autostart

namespace {

class RunKeyAutostart final : public IAutostart {
public:
    QString entryPath() const override
    {
        return QStringLiteral("HKEY_CURRENT_USER\\%1\\%2")
            .arg(QLatin1String(autostart::kRunSubKey), QLatin1String(autostart::kRunValueName));
    }
    bool isEnabled() const override
    {
        return !autostart::readRunValue(QLatin1String(autostart::kRunSubKey),
                                        QLatin1String(autostart::kRunValueName)).isEmpty();
    }
    bool enable(const QString& watcherExecutable) override
    {
        return autostart::writeRunValue(QLatin1String(autostart::kRunSubKey),
                                        QLatin1String(autostart::kRunValueName), watcherExecutable);
    }
    bool disable() override
    {
        return autostart::removeRunValue(QLatin1String(autostart::kRunSubKey),
                                         QLatin1String(autostart::kRunValueName));
    }
};

} // namespace

std::unique_ptr<IAutostart> createPlatformAutostart()
{
    return std::make_unique<RunKeyAutostart>();
}

} // namespace tanara::watcher
