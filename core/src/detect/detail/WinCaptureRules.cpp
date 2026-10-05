#include "tanara/detect/detail/WinCaptureRules.h"

namespace tanara::detail::win {

namespace {

// Windows-os képnév-aliasok: egy (felhasználói) lista-elemhez tartozó további képnevek.
struct Alias { const char* entry; const char* images[8]; };
const Alias kAliases[] = {
    {"teams", {"ms-teams", "msteams", "teams", nullptr}},
    {"webex", {"ciscocollabhost", "webexmta", "atmgr", "ciscowebexstart", nullptr}},
    // A Google Meet böngészőben fut → a böngésző fogja a mikrofont.
    {"meet",  {"chrome", "msedge", "firefox", "brave", "opera", "vivaldi", "chromium", nullptr}},
};

// Képnév-részlet → normalizált app-id + szép név. A sorrend számít (az első találat nyer).
struct Canonical { const char* image; const char* id; const char* name; };
const Canonical kCanonical[] = {
    {"teams", "teams", "Microsoft Teams"},      // ms-teams, msteams, teams
    {"zoom", "zoom", "Zoom"},
    {"ciscocollabhost", "webex", "Webex"}, {"webexmta", "webex", "Webex"},
    {"atmgr", "webex", "Webex"}, {"webex", "webex", "Webex"},
    {"slack", "slack", "Slack"}, {"discord", "discord", "Discord"},
    {"skype", "skype", "Skype"}, {"telegram", "telegram", "Telegram"},
    {"signal", "signal", "Signal"}, {"whatsapp", "whatsapp", "WhatsApp"},
    {"chromium", "chromium", "Chromium"}, {"chrome", "chrome", "Chrome"},
    {"msedge", "msedge", "Microsoft Edge"}, {"firefox", "firefox", "Firefox"},
    {"brave", "brave", "Brave"}, {"opera", "opera", "Opera"}, {"vivaldi", "vivaldi", "Vivaldi"},
};

} // namespace

QString imageBaseName(const QString& pathOrName)
{
    QString s = pathOrName.trimmed();
    const qsizetype cut = qMax(s.lastIndexOf(QLatin1Char('\\')), s.lastIndexOf(QLatin1Char('/')));
    if (cut >= 0)
        s = s.mid(cut + 1);
    s = s.toLower();
    if (s.endsWith(QLatin1String(".exe")))
        s.chop(4);
    return s;
}

bool isSelfImage(const QString& baseLower, const QString& selfBinary)
{
    const QString self = imageBaseName(selfBinary);
    if (self.isEmpty() || baseLower.isEmpty())
        return false;
    return baseLower == self || baseLower.startsWith(self + QLatin1Char('-'));
}

CallAppMatch matchCallApp(const QString& baseLower, const QStringList& knownApps)
{
    CallAppMatch m;
    if (baseLower.isEmpty())
        return m;
    for (const QString& app : knownApps) {
        const QString e = app.trimmed().toLower();
        if (e.isEmpty())
            continue;
        bool hit = baseLower.contains(e);
        for (const Alias& a : kAliases) {
            if (hit) break;
            if (e != QLatin1String(a.entry)) continue;
            for (const char* const* img = a.images; *img; ++img)
                if (baseLower.contains(QLatin1String(*img))) { hit = true; break; }
        }
        if (hit) { m.matched = true; break; }
    }
    if (!m.matched)
        return m;
    for (const Canonical& c : kCanonical) {
        if (baseLower.contains(QLatin1String(c.image))) {
            m.appId = QString::fromLatin1(c.id);
            m.appName = QString::fromLatin1(c.name);
            return m;
        }
    }
    m.appId = baseLower;     // ismeretlen (felhasználó által felvett) app: a képnév maga
    m.appName = baseLower;
    return m;
}

QString consentKeyToName(const QString& keyName, bool nonPackaged)
{
    if (nonPackaged) {
        // A NonPackaged kulcsnév a teljes út, '\' helyett '#'-tel.
        QString path = keyName;
        path.replace(QLatin1Char('#'), QLatin1Char('\\'));
        return imageBaseName(path);
    }
    // Csomagolt app: "<Név>_<publisher-hash>" → a név, kisbetűvel.
    const qsizetype us = keyName.lastIndexOf(QLatin1Char('_'));
    return (us > 0 ? keyName.left(us) : keyName).toLower();
}

bool consentInUse(quint64 lastUsedStart, quint64 lastUsedStop)
{
    if (lastUsedStart == 0)
        return false;
    return lastUsedStop == 0 || lastUsedStart > lastUsedStop;
}

} // namespace tanara::detail::win
