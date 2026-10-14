#include "tanara/watcher/PlaybackRouting.h"

#include "tanara/domain/AudioDeviceNames.h"
#include "tanara/watcher/PipeWireGraph.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include <algorithm>

namespace tanara::watcher {

namespace {

bool sameRoutes(const QVector<PlaybackRoute>& a, const QVector<PlaybackRoute>& b)
{
    if (a.size() != b.size()) return false;
    for (int i = 0; i < a.size(); ++i)
        if (a[i].appName != b[i].appName || a[i].sinkName != b[i].sinkName
            || a[i].running != b[i].running)
            return false;
    return true;
}

} // namespace

QString AudioGraphSnapshot::appForOutput(const QString& captureDeviceName) const
{
    // "Monitor of X" / "Loopback of X" -> "X" (the output's description).
    const QString want = audionames::stripMonitorPrefix(captureDeviceName);
    if (want.isEmpty()) return {};
    QString fallback;
    for (const PlaybackRoute& r : routes) {
        if (r.sinkDescription.compare(want, Qt::CaseInsensitive) != 0) continue;
        if (r.running) return r.appName;
        if (fallback.isEmpty()) fallback = r.appName;
    }
    return fallback;
}

QString AudioGraphSnapshot::outputForApp(const QString& appNameOrBinary) const
{
    const QString key = appNameOrBinary.trimmed().toLower();
    if (key.isEmpty()) return {};
    QString fallback;
    for (const PlaybackRoute& r : routes) {
        if (!r.appName.toLower().contains(key) && !r.appBinary.contains(key)
            && !key.contains(r.appName.toLower()))
            continue;
        if (r.running) return r.sinkDescription;
        if (fallback.isEmpty()) fallback = r.sinkDescription;
    }
    return fallback;
}

namespace detail {

AudioGraphSnapshot parsePwDumpGraph(const QByteArray& json)
{
    AudioGraphSnapshot snap;
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isArray())
        return snap;

    struct Sink { QString name, description; };
    struct Stream { QString app, binary; bool running = false; };
    QHash<int, Sink> sinks;
    QHash<int, Stream> streams;
    QVector<QPair<int, int>> links;   // (source node, sink node)

    for (const QJsonValue& v : doc.array()) {
        const QJsonObject o = v.toObject();
        const QString type = o.value(QStringLiteral("type")).toString();
        const int id = o.value(QStringLiteral("id")).toInt(-1);
        const QJsonObject info = o.value(QStringLiteral("info")).toObject();
        if (type.endsWith(QStringLiteral("Node"))) {
            const QJsonObject props = info.value(QStringLiteral("props")).toObject();
            const QString cls = props.value(QStringLiteral("media.class")).toString();
            const QString nodeName = props.value(QStringLiteral("node.name")).toString();
            if (cls == QStringLiteral("Audio/Sink") || cls == QStringLiteral("Audio/Source")
                || cls == QStringLiteral("Audio/Duplex")) {
                snap.deviceKeys << cls + QLatin1Char(':') + nodeName;
                if (cls != QStringLiteral("Audio/Source")) {
                    QString desc = props.value(QStringLiteral("node.description")).toString();
                    if (desc.isEmpty()) desc = props.value(QStringLiteral("node.nick")).toString();
                    sinks.insert(id, Sink{nodeName, desc});
                }
            } else if (cls == QStringLiteral("Stream/Output/Audio")) {
                Stream s;
                s.binary = props.value(QStringLiteral("application.process.binary")).toString().toLower();
                const QString appProp = props.value(QStringLiteral("application.name")).toString();
                s.app = prettyAppName(s.binary, appProp.isEmpty()
                                                    ? props.value(QStringLiteral("node.name")).toString()
                                                    : appProp);
                s.running = info.value(QStringLiteral("state")).toString() == QStringLiteral("running");
                if (!s.app.isEmpty())
                    streams.insert(id, s);
            }
        } else if (type.endsWith(QStringLiteral("Link"))) {
            links.append({info.value(QStringLiteral("output-node-id")).toInt(-1),
                          info.value(QStringLiteral("input-node-id")).toInt(-1)});
        }
    }

    // A stream gets a separate link per channel, so there is one route per (app, output) pair.
    QStringList seen;
    for (const auto& l : links) {
        const auto st = streams.constFind(l.first);
        const auto sk = sinks.constFind(l.second);
        if (st == streams.constEnd() || sk == sinks.constEnd()) continue;
        const QString key = st->app + QLatin1Char('\n') + sk->name;
        const int at = seen.indexOf(key);
        if (at >= 0) {
            if (st->running) snap.routes[at].running = true;
            continue;
        }
        seen << key;
        snap.routes.append(PlaybackRoute{st->app, st->binary, sk->name, sk->description, st->running});
    }
    std::sort(snap.routes.begin(), snap.routes.end(), [](const PlaybackRoute& a, const PlaybackRoute& b) {
        if (a.sinkName != b.sinkName) return a.sinkName < b.sinkName;
        return a.appName < b.appName;
    });
    snap.deviceKeys.sort();
    return snap;
}

} // namespace detail

namespace {

// No source on this platform: available() false, empty snapshots.
class UnsupportedPlaybackRouting final : public IPlaybackRouting {
public:
    bool available() const override { return false; }
    AudioGraphSnapshot query(int) override { return {}; }
    bool queryAsync(Done) override { return false; }
    bool busy() const override { return false; }
    void cancel() override {}
};

} // namespace

std::unique_ptr<IPlaybackRouting> createUnsupportedPlaybackRouting()
{
    return std::make_unique<UnsupportedPlaybackRouting>();
}

AudioGraphSnapshot queryAudioGraph(int timeoutMs)
{
    return createPlatformPlaybackRouting()->query(timeoutMs);
}

struct PlaybackRouteMonitor::Impl {
    QTimer timer;
    std::unique_ptr<IPlaybackRouting> routing;
    AudioGraphSnapshot snap;
    bool first = true;
};

PlaybackRouteMonitor::PlaybackRouteMonitor(QObject* parent)
    : PlaybackRouteMonitor(createPlatformPlaybackRouting(), parent)
{
}

PlaybackRouteMonitor::PlaybackRouteMonitor(std::unique_ptr<IPlaybackRouting> routing, QObject* parent)
    : QObject(parent), d(std::make_unique<Impl>())
{
    d->routing = routing ? std::move(routing) : createUnsupportedPlaybackRouting();
    connect(&d->timer, &QTimer::timeout, this, &PlaybackRouteMonitor::poll);
}

PlaybackRouteMonitor::~PlaybackRouteMonitor() { stop(); }

bool PlaybackRouteMonitor::available() { return createPlatformPlaybackRouting()->available(); }

AudioGraphSnapshot PlaybackRouteMonitor::snapshot() const { return d->snap; }

void PlaybackRouteMonitor::start(int intervalMs)
{
    if (!d->routing->available()) return;
    d->timer.start(qMax(500, intervalMs));
    poll();
}

void PlaybackRouteMonitor::stop()
{
    d->timer.stop();
    d->routing->cancel();
}

void PlaybackRouteMonitor::poll()
{
    if (d->routing->busy()) return;   // the previous one is still running
    d->routing->queryAsync([this](const AudioGraphSnapshot& next) {
        const bool devs = next.deviceKeys != d->snap.deviceKeys;
        const bool routes = !sameRoutes(next.routes, d->snap.routes);
        const bool first = d->first;
        d->first = false;
        d->snap = next;
        if (routes) emit routesChanged();
        if (devs && !first) emit deviceSetChanged();
    });
}

} // namespace tanara::watcher
