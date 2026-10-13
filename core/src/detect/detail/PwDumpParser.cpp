#include "tanara/detect/detail/PwDumpParser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace tanara::detail {

// Builds a human-readable app name and a normalized app id from the binary path/name.
// e.g. "/usr/bin/zoom" -> appId "zoom". Gives a pretty name for known apps.
QString prettyAppName(const QString& binaryLower, const QString& appNameProp)
{
    struct Known { const char* key; const char* name; };
    static const Known table[] = {
        {"zoom", "Zoom"}, {"teams", "Microsoft Teams"}, {"webex", "Webex"},
        {"discord", "Discord"}, {"slack", "Slack"}, {"skype", "Skype"},
        {"telegram", "Telegram"}, {"chromium", "Chromium"}, {"chrome", "Chrome"},
        {"firefox", "Firefox"}, {"vivaldi", "Vivaldi"}, {"brave", "Brave"},
    };
    for (const Known& k : table)
        if (binaryLower.contains(QLatin1String(k.key)))
            return QString::fromLatin1(k.name);
    // Unknown: the props' application.name if present, otherwise the binary.
    return appNameProp.isEmpty() ? binaryLower : appNameProp;
}

namespace {

// Browser binaries: web calls (Google Meet, Teams in the browser...) run in these.
bool isBrowserBinary(const QString& binaryLower)
{
    static const char* browsers[] = {"chromium", "chrome", "firefox", "vivaldi", "brave",
                                     "opera", "msedge", "microsoft-edge"};
    for (const char* b : browsers)
        if (binaryLower.contains(QLatin1String(b)))
            return true;
    return false;
}

QString normalizedAppId(const QString& binaryLower)
{
    static const char* ids[] = {"zoom", "teams", "webex", "discord", "slack", "skype",
                                "telegram", "chromium", "chrome", "firefox", "vivaldi", "brave",
                                "opera", "msedge"};
    for (const char* id : ids)
        if (binaryLower.contains(QLatin1String(id)))
            return QString::fromLatin1(id);
    return binaryLower;
}

} // namespace

MeetingSignal parsePwDump(const QByteArray& json,
                          const QStringList& knownApps,
                          const QString& selfBinary)
{
    MeetingSignal sig;   // active=false by default

    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isArray())
        return sig;

    const QString selfLower = selfBinary.trimmed().toLower();

    for (const QJsonValue& v : doc.array()) {
        const QJsonObject o = v.toObject();
        if (!o.value(QStringLiteral("type")).toString().contains(QStringLiteral("Node")))
            continue;
        const QJsonObject info = o.value(QStringLiteral("info")).toObject();
        const QJsonObject props = info.value(QStringLiteral("props")).toObject();

        // Only microphone-holding streams (an app HOLDS the microphone), and only if ACTIVE.
        if (props.value(QStringLiteral("media.class")).toString() != QStringLiteral("Stream/Input/Audio"))
            continue;
        if (info.value(QStringLiteral("state")).toString() != QStringLiteral("running"))
            continue;

        const QString appNameProp = props.value(QStringLiteral("application.name")).toString();
        const QString binary      = props.value(QStringLiteral("application.process.binary")).toString();
        const QString mediaName   = props.value(QStringLiteral("media.name")).toString();
        const QString binaryLower = binary.toLower();
        const QString appLower    = appNameProp.toLower();
        const QString mediaLower  = mediaName.toLower();

        // Self-exclusion: our own recorder's (tanara) capture is NEVER a meeting.
        if (!selfLower.isEmpty()
            && (binaryLower.contains(selfLower) || appLower.contains(selfLower)))
            continue;
        // Discord screen share / game audio is NOT a call.
        if (mediaLower.contains(QStringLiteral("game capture")))
            continue;

        // Match: a WebRTC engine OR a recStream/webrtc media.name OR a known binary.
        bool matched = appLower.contains(QStringLiteral("webrtc voiceengine"))
                       || mediaLower.contains(QStringLiteral("recstream"))
                       || mediaLower.contains(QStringLiteral("webrtc"));
        if (!matched) {
            for (const QString& app : knownApps) {
                const QString a = app.trimmed().toLower();
                if (a.isEmpty())
                    continue;
                if (binaryLower.contains(a) || appLower.contains(a)) { matched = true; break; }
                // The "meet" list item means every browser: Google Meet (and other web calls) holds the microphone
                // in the browser's process (Vivaldi: "vivaldi-bin", Chrome: "chrome", Edge: "msedge"...), and for
                // Chromium-based ones media.name is just "RecordStream". The same alias lives in the Windows detector.
                if (a == QLatin1String("meet") && isBrowserBinary(binaryLower)) { matched = true; break; }
            }
        }
        if (!matched)
            continue;

        sig.active      = true;
        sig.appId       = normalizedAppId(binaryLower.isEmpty() ? appLower : binaryLower);
        sig.appName     = prettyAppName(binaryLower, appNameProp);
        sig.windowTitle = props.value(QStringLiteral("node.name")).toString();  // best-effort
        sig.sourceRef   = QStringLiteral("pw-node:%1")
                              .arg(o.value(QStringLiteral("id")).toInt());
        return sig;   // the first active match is enough
    }
    return sig;
}

} // namespace tanara::detail
