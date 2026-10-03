#include "tanara/audio/PlaybackRouting.h"

#include "tanara/audio/TrackCatalog.h"
#include "tanara/detect/detail/PwDumpParser.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>

namespace tanara {

namespace {

// „Monitor of X” / „Loopback of X” → „X” (a kimenet leírása).
QString stripMonitorPrefix(const QString& name)
{
    static const QRegularExpression prefix(
        QStringLiteral("^(monitor of|monitor:|loopback of|loopback:)\\s*"),
        QRegularExpression::CaseInsensitiveOption);
    QString n = name.trimmed();
    n.remove(prefix);
    return n;
}

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
    const QString want = stripMonitorPrefix(captureDeviceName);
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
    QVector<QPair<int, int>> links;   // (kimenő node, bemenő node)

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

    // Egy stream csatornánként külön linket kap → (app, kimenet) páronként egy útvonal.
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

static QString pwDumpBinary()
{
#if defined(Q_OS_LINUX)
    return QStandardPaths::findExecutable(QStringLiteral("pw-dump"));
#else
    return {};
#endif
}

AudioGraphSnapshot queryAudioGraph(int timeoutMs)
{
    const QString bin = pwDumpBinary();
    if (bin.isEmpty()) return {};
    QProcess proc;
    proc.start(bin, QStringList{});
    if (!proc.waitForStarted(500)) return {};
    if (!proc.waitForFinished(timeoutMs)) {
        proc.kill();
        proc.waitForFinished(200);
        return {};
    }
    return detail::parsePwDumpGraph(proc.readAllStandardOutput());
}

struct PlaybackRouteMonitor::Impl {
    QTimer timer;
    QProcess* proc = nullptr;
    AudioGraphSnapshot snap;
    bool first = true;
    QString bin;
};

PlaybackRouteMonitor::PlaybackRouteMonitor(QObject* parent)
    : QObject(parent), d(std::make_unique<Impl>())
{
    d->bin = pwDumpBinary();
    connect(&d->timer, &QTimer::timeout, this, &PlaybackRouteMonitor::poll);
}

PlaybackRouteMonitor::~PlaybackRouteMonitor() { stop(); }

bool PlaybackRouteMonitor::available() { return !pwDumpBinary().isEmpty(); }

AudioGraphSnapshot PlaybackRouteMonitor::snapshot() const { return d->snap; }

void PlaybackRouteMonitor::start(int intervalMs)
{
    if (d->bin.isEmpty()) return;
    d->timer.start(qMax(500, intervalMs));
    poll();
}

void PlaybackRouteMonitor::stop()
{
    d->timer.stop();
    if (d->proc) {
        d->proc->disconnect(this);
        d->proc->kill();
        d->proc->waitForFinished(300);
        delete d->proc;
        d->proc = nullptr;
    }
}

void PlaybackRouteMonitor::poll()
{
    if (d->proc || d->bin.isEmpty()) return;   // az előző még fut
    auto* proc = new QProcess(this);
    d->proc = proc;
    connect(proc, &QProcess::finished, this, [this, proc](int code, QProcess::ExitStatus st) {
        const QByteArray out = proc->readAllStandardOutput();
        proc->deleteLater();
        if (d->proc == proc) d->proc = nullptr;
        if (st != QProcess::NormalExit || code != 0) return;
        const AudioGraphSnapshot next = detail::parsePwDumpGraph(out);
        const bool devs = next.deviceKeys != d->snap.deviceKeys;
        const bool routes = !sameRoutes(next.routes, d->snap.routes);
        const bool first = d->first;
        d->first = false;
        d->snap = next;
        if (routes) emit routesChanged();
        if (devs && !first) emit deviceSetChanged();
    });
    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart) return;
        proc->deleteLater();
        if (d->proc == proc) d->proc = nullptr;
    });
    proc->start(d->bin, QStringList{});
}

} // namespace tanara
