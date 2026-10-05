#include "tanara/detect/detail/PwDumpParser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace tanara::detail {

// A bináris-útból/névből ember-olvasható app-nevet és normalizált app-id-t képez.
// pl. "/usr/bin/zoom" → appId "zoom". Az ismert appokra szép nevet ad.
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
    // Ismeretlen: a props application.name-je, ha van, különben a bináris.
    return appNameProp.isEmpty() ? binaryLower : appNameProp;
}

namespace {

// Böngésző-binárisok: a webes hívások (Google Meet, Teams a böngészőben…) ezekben futnak.
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
    MeetingSignal sig;   // active=false alapból

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

        // Csak mikrofon-fogó streamek (egy app FOGJA a mikrofont), és csak ha AKTÍV.
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

        // Ön-kizárás: a saját felvevőnk (tanara) capture-je SOHA nem meeting.
        if (!selfLower.isEmpty()
            && (binaryLower.contains(selfLower) || appLower.contains(selfLower)))
            continue;
        // A Discord képernyőmegosztás/játék-hang NEM hívás.
        if (mediaLower.contains(QStringLiteral("game capture")))
            continue;

        // Illeszkedés: WebRTC-motor VAGY recStream/webrtc media.name VAGY ismert bináris.
        bool matched = appLower.contains(QStringLiteral("webrtc voiceengine"))
                       || mediaLower.contains(QStringLiteral("recstream"))
                       || mediaLower.contains(QStringLiteral("webrtc"));
        if (!matched) {
            for (const QString& app : knownApps) {
                const QString a = app.trimmed().toLower();
                if (a.isEmpty())
                    continue;
                if (binaryLower.contains(a) || appLower.contains(a)) { matched = true; break; }
                // A „meet” listaelem minden böngészőt jelent: a Google Meet (és a többi webes
                // hívás) a böngésző folyamatában fogja a mikrofont (Vivaldi: „vivaldi-bin”,
                // Chrome: „chrome”, Edge: „msedge”…), a Chromium-alapúak media.name-je csak
                // „RecordStream”. Ugyanez az alias él a Windows-detektorban is.
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
        return sig;   // az első aktív találat elég
    }
    return sig;
}

} // namespace tanara::detail
