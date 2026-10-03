#include "tanara/import/AudioImporter.h"
#include "tanara/store/JsonSerialization.h"
#include "tanara/store/MeetingStore.h"
#include "tanara/Logging.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTimer>
#include <QUuid>

namespace tanara {

namespace {

QString trImport(const char* text)
{
    return QCoreApplication::translate("AudioImporter", text);
}

// Fájlnév-rész a sávhoz: ékezet nélküli, kisbetűs, kötőjeles ASCII (mint a felvett sávoké).
QString slugify(const QString& in)
{
    QString s;
    for (const QChar c : in.normalized(QString::NormalizationForm_KD))
        if (c.category() != QChar::Mark_NonSpacing) s.append(c);
    static const QRegularExpression nonWord(QStringLiteral("[^A-Za-z0-9]+"));
    s.replace(nonWord, QStringLiteral("-"));
    static const QRegularExpression edges(QStringLiteral("^-+|-+$"));
    s.replace(edges, QString());
    s = s.toLower().left(48);
    return s.isEmpty() ? QStringLiteral("sav") : s;
}

// A sáv neve úgy, hogy a TrackCatalog rövidítése (tracknames::shortDeviceName — az
// eszköznevek „ - Profil” és „(…)” részét vágja) ne csonkítsa: a fájlnév egészben látsszon.
QString trackNameFromFile(const QString& path)
{
    QString n = QFileInfo(path).completeBaseName().simplified();
    if (n.isEmpty()) n = QFileInfo(path).fileName();
    n.replace(QStringLiteral(" - "), QStringLiteral(" – "));
    static const QRegularExpression paren(QStringLiteral("^[^()]*\\(([^()]+)\\)\\s*$"));
    if (paren.match(n).hasMatch()) {
        n.replace(QLatin1Char('('), QLatin1Char('['));
        n.replace(QLatin1Char(')'), QLatin1Char(']'));
    }
    static const QRegularExpression monitorPrefix(
        QStringLiteral("^(monitor of|monitor:|loopback of|loopback:)\\s*"),
        QRegularExpression::CaseInsensitiveOption);
    if (monitorPrefix.match(n).hasMatch()) n.prepend(QStringLiteral("„")).append(QStringLiteral("”"));
    return n;
}

// A fájlba ágyazott készítési idő a címkékből (MP4/MOV/MKV: ISO creation_time; BWF WAV:
// date + creation_time külön). Érvénytelen, ha nincs vagy nyilvánvalóan hibás (beállítatlan
// óra: 1904 / 1970, vagy jövőbeli).
QDateTime embeddedDate(const QJsonObject& tags)
{
    QString creation, date;
    for (auto it = tags.constBegin(); it != tags.constEnd(); ++it) {
        const QString k = it.key().toLower();
        if (k == QLatin1String("creation_time")) creation = it.value().toString().trimmed();
        else if (k == QLatin1String("date"))     date = it.value().toString().trimmed();
    }
    QDateTime dt;
    if (!creation.isEmpty()) {
        dt = QDateTime::fromString(creation, Qt::ISODateWithMs);
        if (!dt.isValid()) dt = QDateTime::fromString(creation, Qt::ISODate);
        if (!dt.isValid() || !creation.contains(QLatin1Char('-'))) {
            // BWF: a dátum és az idő külön címkében (helyi idő).
            const QDate d = QDate::fromString(date.left(10), Qt::ISODate);
            const QTime t = QTime::fromString(creation.left(8), QStringLiteral("HH:mm:ss"));
            dt = (d.isValid() && t.isValid()) ? QDateTime(d, t) : QDateTime();
        }
    }
    if (!dt.isValid()) return {};
    dt = dt.toLocalTime();
    if (dt.date().year() < 1990 || dt > QDateTime::currentDateTime().addDays(1)) return {};
    return dt;
}

ImportFileInfo baseInfo(const QString& path)
{
    ImportFileInfo info;
    const QFileInfo fi(path);
    info.path = fi.absoluteFilePath();
    info.sizeBytes = fi.isFile() ? fi.size() : 0;
    info.modifiedAt = fi.lastModified();
    if (!fi.isFile())
        info.error = trImport("A fájl nem található: %1").arg(fi.fileName());
    return info;
}

QStringList probeArgs(const QString& path)
{
    return {QStringLiteral("-v"), QStringLiteral("error"),
            QStringLiteral("-print_format"), QStringLiteral("json"),
            QStringLiteral("-show_format"), QStringLiteral("-show_streams"), path};
}

// Az ffprobe JSON-kimenetéből az adatok (az ELSŐ hangfolyam számít).
void parseProbe(ImportFileInfo& info, bool exitedOk, const QByteArray& json)
{
    const QString name = QFileInfo(info.path).fileName();
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    const QJsonArray streams = root.value(QStringLiteral("streams")).toArray();
    if (!exitedOk || streams.isEmpty()) {
        info.error = trImport("Nem hang- vagy videófájl (nem olvasható be): %1").arg(name);
        return;
    }
    QJsonObject audio;
    for (const QJsonValue& v : streams) {
        const QJsonObject s = v.toObject();
        const QString type = s.value(QStringLiteral("codec_type")).toString();
        if (type == QLatin1String("audio") && audio.isEmpty()) audio = s;
        // A borítókép (attached_pic) nem videó.
        if (type == QLatin1String("video")
            && s.value(QStringLiteral("disposition")).toObject()
                   .value(QStringLiteral("attached_pic")).toInt() == 0)
            info.hasVideo = true;
    }
    if (audio.isEmpty() || audio.value(QStringLiteral("channels")).toInt() <= 0) {
        info.error = trImport("Ebben a fájlban nincs hang: %1").arg(name);
        return;
    }
    const QJsonObject format = root.value(QStringLiteral("format")).toObject();
    info.channels   = audio.value(QStringLiteral("channels")).toInt();
    info.sampleRate = audio.value(QStringLiteral("sample_rate")).toString().toInt();
    info.codec      = audio.value(QStringLiteral("codec_name")).toString();
    double secs = format.value(QStringLiteral("duration")).toString().toDouble();
    if (secs <= 0.0) secs = audio.value(QStringLiteral("duration")).toString().toDouble();
    info.durationMs = secs > 0.0 ? qint64(secs * 1000.0 + 0.5) : 0;
    info.createdAt = embeddedDate(format.value(QStringLiteral("tags")).toObject());
    if (!info.createdAt.isValid())
        info.createdAt = embeddedDate(audio.value(QStringLiteral("tags")).toObject());
    info.ok = true;
}

QString ffmpegMissing()
{
    return trImport("Az ffmpeg nem található a gépen — telepítsd, és próbáld újra.");
}

} // namespace

// ---- szabad függvények ---------------------------------------------------------------

namespace audioimport {

ImportFileInfo probe(const QString& path, int timeoutMs)
{
    ImportFileInfo info = baseInfo(path);
    if (!info.error.isEmpty()) return info;
    QProcess p;
    p.start(QStringLiteral("ffprobe"), probeArgs(info.path));
    if (!p.waitForStarted(5000)) {
        info.error = ffmpegMissing();
        return info;
    }
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(2000);
        info.error = trImport("Nem hang- vagy videófájl (nem olvasható be): %1")
                         .arg(QFileInfo(info.path).fileName());
        return info;
    }
    parseProbe(info, p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0,
               p.readAllStandardOutput());
    return info;
}

bool splitByDefault(const ImportFileInfo& info)
{
    return info.ok && info.channels > 2 && !info.hasVideo;
}

QString channelName(int channel, int channelCount)
{
    if (channelCount == 2)
        return channel == 0 ? trImport("Bal csatorna") : trImport("Jobb csatorna");
    return trImport("%1. csatorna").arg(channel + 1);
}

QVector<ImportPlannedTrack> planTracks(const QVector<ImportSource>& sources,
                                       const QVector<ImportFileInfo>& files)
{
    QVector<ImportPlannedTrack> out;
    for (int i = 0; i < sources.size(); ++i) {
        const ImportFileInfo info = files.value(i);
        const QString base = trackNameFromFile(sources.at(i).path);
        if (sources.at(i).splitChannels && info.channels >= 2) {
            for (int c = 0; c < info.channels; ++c) {
                ImportPlannedTrack t;
                t.sourceIndex = i;
                t.channel = c;
                t.channels = 1;
                const QString ch = channelName(c, info.channels);
                // Egyetlen fájlnál elég a csatorna neve; többnél a fájlé is kell elé.
                t.name = sources.size() > 1 ? QStringLiteral("%1 – %2").arg(base, ch) : ch;
                out.append(t);
            }
        } else {
            ImportPlannedTrack t;
            t.sourceIndex = i;
            t.channels = info.channels >= 2 ? 2 : 1;   // kettőnél több csatorna → sztereóba keverve
            t.name = base;
            out.append(t);
        }
    }
    return out;
}

QString defaultTitle(const QStringList& paths)
{
    if (paths.isEmpty()) return trImport("Importált felvétel");
    QStringList names;
    for (const QString& p : paths) {
        const QString n = QFileInfo(p).completeBaseName().simplified();
        names << (n.isEmpty() ? QFileInfo(p).fileName() : n);
    }
    QString common = names.first();
    for (const QString& n : names) {
        int k = 0;
        while (k < common.size() && k < n.size() && common.at(k) == n.at(k)) ++k;
        common.truncate(k);
    }
    // A közös rész végéről a csonka elválasztók le („interjú_”, „Kovács - ”, „felvétel (”).
    static const QRegularExpression tail(QStringLiteral("[\\s\\-–—_.,(\\[]+$"));
    common.remove(tail);
    if (names.size() > 1 && common.size() >= 3) return common;
    return names.first();
}

QDateTime defaultStart(const QVector<ImportFileInfo>& files)
{
    QDateTime best;
    for (const ImportFileInfo& f : files) {
        const QDateTime d = f.bestDate();
        if (d.isValid() && (!best.isValid() || d < best)) best = d;
    }
    return best.isValid() ? best : QDateTime::currentDateTime();
}

} // namespace audioimport

// ---- AudioImporter -------------------------------------------------------------------

struct AudioImporter::Impl {
    AudioImporter* q = nullptr;
    MeetingStore*  store = nullptr;
    int            kbps = 64;
    QString        userName;

    // A futó importálás. A lezárás (siker / hiba / megszakítás) EGY helyen történik: close().
    struct Run {
        QString id;
        ImportRequest req;
        QString title;
        QVector<ImportFileInfo> files;
        QVector<ImportPlannedTrack> plan;
        QStringList trackFiles;          // a tervvel párhuzamos (relatív fájlnevek)
        QStringList trackIds;
        QString stagingDir;
        int probeIndex = 0;
        int encodeIndex = -1;
        QVector<qint64> encodedMs;       // forrásonként az ffmpeg által kiírt hossz
        QPointer<QProcess> proc;
        QByteArray outBuf;
        bool cancelled = false;
        int lastPercent = -2;
    };
    std::unique_ptr<Run> run;

    void probeNext();
    void beginEncode();
    void encodeNext();
    void onProgressOutput();
    void finalize();
    enum class End { Done, Failed, Cancelled };
    void close(End end, const QString& message = {}, const QString& detail = {},
               const Meeting& meeting = {});
    void sweepStaleStaging() const;
    QStringList encodeArgs(int sourceIndex) const;
};

AudioImporter::AudioImporter(MeetingStore* store, QObject* parent)
    : QObject(parent), d(std::make_unique<Impl>())
{
    d->q = this;
    d->store = store;
    qRegisterMetaType<tanara::ImportFileInfo>();
    qRegisterMetaType<tanara::ImportRequest>();
}

AudioImporter::~AudioImporter()
{
    // Kilépés importálás közben: ne maradjon futó ffmpeg és félkész mappa.
    if (d->run) {
        if (QProcess* p = d->run->proc) {
            p->disconnect(this);
            p->kill();
            p->waitForFinished(3000);
        }
        if (!d->run->stagingDir.isEmpty())
            QDir(d->run->stagingDir).removeRecursively();
    }
}

void AudioImporter::setOpusBitrateKbps(int kbps) { d->kbps = kbps > 0 ? kbps : 64; }
void AudioImporter::setUserSpeakerName(const QString& name) { d->userName = name.trimmed(); }
bool AudioImporter::busy() const { return d->run != nullptr; }
QString AudioImporter::currentId() const { return d->run ? d->run->id : QString(); }

void AudioImporter::probeAsync(const QString& path)
{
    ImportFileInfo info = baseInfo(path);
    if (!info.error.isEmpty()) {
        QTimer::singleShot(0, this, [this, info] { emit probed(info); });
        return;
    }
    auto* p = new QProcess(this);
    auto done = std::make_shared<bool>(false);
    connect(p, &QProcess::finished, this, [this, p, info, done](int code, QProcess::ExitStatus st) mutable {
        if (*done) return;
        *done = true;
        parseProbe(info, st == QProcess::NormalExit && code == 0, p->readAllStandardOutput());
        p->deleteLater();
        emit probed(info);
    });
    connect(p, &QProcess::errorOccurred, this, [this, p, info, done](QProcess::ProcessError e) mutable {
        if (e != QProcess::FailedToStart || *done) return;
        *done = true;
        info.error = ffmpegMissing();
        p->deleteLater();
        emit probed(info);
    });
    p->start(QStringLiteral("ffprobe"), probeArgs(info.path));
}

QString AudioImporter::start(const ImportRequest& request)
{
    if (d->run || !d->store) return {};
    d->run = std::make_unique<Impl::Run>();
    Impl::Run& r = *d->run;
    r.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    r.req = request;
    for (ImportSource& s : r.req.sources)
        s.path = QFileInfo(s.path).absoluteFilePath();
    QStringList paths;
    for (const ImportSource& s : r.req.sources) paths << s.path;
    r.title = request.title.trimmed().isEmpty() ? audioimport::defaultTitle(paths)
                                                : request.title.trimmed();
    const QString id = r.id;
    emit started(id, r.title);
    // A munka a következő eseménykörben indul: a hívó addigra megkapta az azonosítót.
    QTimer::singleShot(0, this, [this, id] {
        if (!d->run || d->run->id != id) return;
        if (d->run->req.sources.isEmpty()) {
            d->close(Impl::End::Failed, tr("Nincs importálható fájl."));
            return;
        }
        d->sweepStaleStaging();
        d->probeNext();
    });
    return id;
}

void AudioImporter::cancel()
{
    if (!d->run || d->run->cancelled) return;
    d->run->cancelled = true;
    if (QProcess* p = d->run->proc) {
        if (p->state() != QProcess::NotRunning) {
            p->kill();   // a finished-ág zár le
            return;
        }
    }
    d->close(Impl::End::Cancelled);
}

// Egy nappal korábbi, összeomlásból itt maradt félkész import-mappák törlése.
void AudioImporter::Impl::sweepStaleStaging() const
{
    const QDir root(store->audioDir());
    const QFileInfoList dirs = root.entryInfoList({QStringLiteral(".import-*")},
                                                  QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot);
    for (const QFileInfo& fi : dirs)
        if (fi.lastModified().secsTo(QDateTime::currentDateTime()) > 24 * 3600)
            QDir(fi.absoluteFilePath()).removeRecursively();
}

void AudioImporter::Impl::probeNext()
{
    Run& r = *run;
    if (r.cancelled) { close(End::Cancelled); return; }
    if (r.probeIndex >= r.req.sources.size()) { beginEncode(); return; }

    ImportFileInfo info = baseInfo(r.req.sources.at(r.probeIndex).path);
    if (!info.error.isEmpty()) { close(End::Failed, info.error); return; }

    auto* p = new QProcess(q);
    r.proc = p;
    const QString id = r.id;
    auto done = std::make_shared<bool>(false);
    QObject::connect(p, &QProcess::finished, q,
                     [this, p, id, info, done](int code, QProcess::ExitStatus st) mutable {
        if (*done) return;
        *done = true;
        p->deleteLater();
        if (!run || run->id != id) return;
        if (run->cancelled) { close(End::Cancelled); return; }
        parseProbe(info, st == QProcess::NormalExit && code == 0, p->readAllStandardOutput());
        if (!info.ok) { close(End::Failed, info.error); return; }
        run->files.append(info);
        ++run->probeIndex;
        probeNext();
    });
    QObject::connect(p, &QProcess::errorOccurred, q, [this, p, id, done](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart || *done) return;
        *done = true;
        p->deleteLater();
        if (run && run->id == id) close(End::Failed, ffmpegMissing());
    });
    p->start(QStringLiteral("ffprobe"), probeArgs(info.path));
}

void AudioImporter::Impl::beginEncode()
{
    Run& r = *run;
    r.plan = audioimport::planTracks(r.req.sources, r.files);
    if (r.plan.isEmpty()) { close(End::Failed, AudioImporter::tr("Nincs importálható fájl.")); return; }

    r.stagingDir = QDir(store->audioDir()).filePath(QStringLiteral(".import-") + r.id);
    if (!QDir().mkpath(r.stagingDir)) {
        const QString dir = r.stagingDir;
        r.stagingDir.clear();
        close(End::Failed, AudioImporter::tr("Nem hozható létre mappa a felvételek helyén."), dir);
        return;
    }
    // Sáv-azonosítók / fájlnevek: a névből képzett, a meetingen belül egyedi slug.
    QStringList used;
    for (const ImportPlannedTrack& t : r.plan) {
        const QString base = slugify(t.name);
        QString slug = base;
        int n = 2;
        while (used.contains(slug)) slug = base + QLatin1Char('-') + QString::number(n++);
        used << slug;
        r.trackIds << slug;
        r.trackFiles << QStringLiteral("track_") + slug + QStringLiteral(".ogg");
    }
    r.encodedMs.fill(0, r.req.sources.size());
    r.encodeIndex = -1;
    encodeNext();
}

// Egy forrás ffmpeg-parancsa: az első hangfolyam → Opus/ogg, 48 kHz. Bontásnál egyetlen
// ffmpeg több kimenettel (csatornánként egy mono sáv) — a forrást egyszer olvassuk végig.
QStringList AudioImporter::Impl::encodeArgs(int sourceIndex) const
{
    const Run& r = *run;
    const QString bitrate = QString::number(kbps) + QLatin1Char('k');
    QStringList args{QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                     QStringLiteral("-nostdin"), QStringLiteral("-y"),
                     QStringLiteral("-progress"), QStringLiteral("pipe:1"), QStringLiteral("-nostats"),
                     QStringLiteral("-i"), r.req.sources.at(sourceIndex).path};
    QVector<int> mine;
    for (int i = 0; i < r.plan.size(); ++i)
        if (r.plan.at(i).sourceIndex == sourceIndex) mine.append(i);
    const auto output = [&](int planIndex) {
        args << QStringLiteral("-map_metadata") << QStringLiteral("-1")
             << QStringLiteral("-ar") << QStringLiteral("48000")
             << QStringLiteral("-c:a") << QStringLiteral("libopus")
             << QStringLiteral("-b:a") << bitrate
             << QDir(r.stagingDir).filePath(r.trackFiles.at(planIndex));
    };
    if (mine.size() == 1 && r.plan.at(mine.first()).channel < 0) {
        args << QStringLiteral("-map") << QStringLiteral("0:a:0")
             << QStringLiteral("-ac") << QString::number(r.plan.at(mine.first()).channels);
        output(mine.first());
        return args;
    }
    QString graph = QStringLiteral("[0:a:0]asplit=%1").arg(mine.size());
    for (int k = 0; k < mine.size(); ++k) graph += QStringLiteral("[s%1]").arg(k);
    for (int k = 0; k < mine.size(); ++k)
        graph += QStringLiteral(";[s%1]pan=mono|c0=c%2[o%1]").arg(k).arg(r.plan.at(mine.at(k)).channel);
    args << QStringLiteral("-filter_complex") << graph;
    for (int k = 0; k < mine.size(); ++k) {
        args << QStringLiteral("-map") << QStringLiteral("[o%1]").arg(k);
        output(mine.at(k));
    }
    return args;
}

void AudioImporter::Impl::encodeNext()
{
    Run& r = *run;
    if (r.cancelled) { close(End::Cancelled); return; }
    ++r.encodeIndex;
    if (r.encodeIndex >= r.req.sources.size()) { finalize(); return; }

    auto* p = new QProcess(q);
    r.proc = p;
    r.outBuf.clear();
    const QString id = r.id;
    const int index = r.encodeIndex;
    auto done = std::make_shared<bool>(false);
    QObject::connect(p, &QProcess::readyReadStandardOutput, q, [this, p, id] {
        if (!run || run->id != id || run->proc != p) return;
        run->outBuf += p->readAllStandardOutput();
        onProgressOutput();
    });
    QObject::connect(p, &QProcess::finished, q,
                     [this, p, id, index, done](int code, QProcess::ExitStatus st) {
        if (*done) return;
        *done = true;
        p->deleteLater();
        if (!run || run->id != id) return;
        if (run->cancelled) { close(End::Cancelled); return; }
        const QString name = QFileInfo(run->req.sources.at(index).path).fileName();
        if (st != QProcess::NormalExit || code != 0) {
            close(End::Failed, AudioImporter::tr("Nem sikerült beolvasni: %1").arg(name),
                  QString::fromUtf8(p->readAllStandardError()).simplified().left(300));
            return;
        }
        run->outBuf += p->readAllStandardOutput();
        onProgressOutput();
        // Minden sávnak meg kell lennie (üres / hiányzó kimenet = hiba).
        for (int i = 0; i < run->plan.size(); ++i) {
            if (run->plan.at(i).sourceIndex != index) continue;
            if (QFileInfo(QDir(run->stagingDir).filePath(run->trackFiles.at(i))).size() <= 0) {
                close(End::Failed, AudioImporter::tr("Nem sikerült beolvasni: %1").arg(name));
                return;
            }
        }
        if (run->encodedMs.at(index) <= 0)
            run->encodedMs[index] = run->files.at(index).durationMs;
        encodeNext();
    });
    QObject::connect(p, &QProcess::errorOccurred, q, [this, p, id, done](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart || *done) return;
        *done = true;
        p->deleteLater();
        if (run && run->id == id) close(End::Failed, ffmpegMissing());
    });
    emit q->progress(r.id, qMax(r.lastPercent, -1), r.encodeIndex, int(r.req.sources.size()));
    p->start(QStringLiteral("ffmpeg"), encodeArgs(index));
}

// Az ffmpeg `-progress` kimenete: kulcs=érték sorok; out_time_us = a már kiírt hang hossza.
void AudioImporter::Impl::onProgressOutput()
{
    Run& r = *run;
    qint64 outMs = -1;
    int nl;
    while ((nl = r.outBuf.indexOf('\n')) >= 0) {
        const QByteArray line = r.outBuf.left(nl).trimmed();
        r.outBuf.remove(0, nl + 1);
        if (line.startsWith("out_time_us=") || line.startsWith("out_time_ms=")) {
            bool ok = false;
            const qint64 us = line.mid(12).toLongLong(&ok);   // (az out_time_ms is µs)
            if (ok && us >= 0) outMs = us / 1000;
        }
    }
    if (outMs < 0) return;
    r.encodedMs[r.encodeIndex] = qMax(r.encodedMs.at(r.encodeIndex), outMs);

    // Teljes százalék a források (ismert) hosszával súlyozva; ismeretlen hossznál nincs.
    qint64 total = 0, doneMs = 0;
    for (int i = 0; i < r.files.size(); ++i) {
        const qint64 dur = r.files.at(i).durationMs;
        if (dur <= 0) { total = 0; break; }
        total += dur;
        if (i < r.encodeIndex) doneMs += dur;
        else if (i == r.encodeIndex) doneMs += qMin(dur, r.encodedMs.at(i));
    }
    const int pct = total > 0 ? int(qBound(qint64(0), doneMs * 100 / total, qint64(99))) : -1;
    if (pct == r.lastPercent) return;
    r.lastPercent = pct;
    emit q->progress(r.id, pct, r.encodeIndex, int(r.req.sources.size()));
}

void AudioImporter::Impl::finalize()
{
    Run& r = *run;
    Meeting m;
    m.id = r.id;
    m.title = r.title;
    m.startedAt = r.req.startedAt.isValid() ? r.req.startedAt : audioimport::defaultStart(r.files);
    for (qint64 ms : r.encodedMs) m.durationMs = qMax(m.durationMs, ms);

    for (int i = 0; i < r.plan.size(); ++i) {
        const ImportPlannedTrack& p = r.plan.at(i);
        Track t;
        t.id = r.trackIds.at(i);
        t.deviceName = p.name;
        t.file = r.trackFiles.at(i);
        t.sampleRate = 48000;
        t.channels = p.channels;
        t.active = true;
        if (i == r.req.ownTrack) {
            // A felhasználó saját mikrofonja: úgy viselkedik, mint egy felvett mic-sáv.
            t.kind = TrackKind::Mic;
            t.fixedSpeaker = true;
            t.speakerLabel = userName.isEmpty() ? QStringLiteral("Mikrofon 1") : userName;
        } else {
            t.kind = TrackKind::Other;
            t.fixedSpeaker = false;
        }
        m.tracks.append(t);
    }

    // Végleges mappa: ugyanaz a névminta, mint a felvételnél (dátum_idő_cím-slug).
    const QDir root(store->audioDir());
    const QString base = m.startedAt.toString(QStringLiteral("yyyy-MM-dd_HHmm"))
                         + QLatin1Char('_') + slugify(m.title);
    QString name = base;
    if (root.exists(name)) name = base + QLatin1Char('_') + r.id.left(8);
    m.folder = root.filePath(name);

    // A meeting.json még a rejtett mappában elkészül, így az átnevezés után a mappa már
    // teljes (ha a folyamat épp ott állna le, az index-újraépítés rendesen megtalálja).
    const QByteArray json = QJsonDocument(toJson(m)).toJson(QJsonDocument::Indented);
    QSaveFile f(QDir(r.stagingDir).filePath(QStringLiteral("meeting.json")));
    const bool written = f.open(QIODevice::WriteOnly) && f.write(json) == json.size() && f.commit();
    if (!written || !QDir().rename(r.stagingDir, m.folder)) {
        close(End::Failed, AudioImporter::tr("Nem sikerült elmenteni az importált felvételt."),
              m.folder);
        return;
    }
    r.stagingDir.clear();
    store->saveMeeting(m);   // index + meetingUpdated (a könyvtár ebből veszi fel)
    close(End::Done, {}, {}, m);
    qCInfo(lcStore).noquote() << "Importálva:" << name << "sávok:" << m.tracks.size()
                              << "hossz(ms):" << m.durationMs;
}

void AudioImporter::Impl::close(End end, const QString& message, const QString& detail,
                                const Meeting& meeting)
{
    // Előbb a futás elengedése (a jel kezelője már indíthat újat), aztán a jel.
    const std::unique_ptr<Run> r = std::move(run);
    if (!r) return;
    if (!r->stagingDir.isEmpty())
        QDir(r->stagingDir).removeRecursively();   // félkész mappa nem marad
    switch (end) {
    case End::Done:
        emit q->progress(r->id, 100, int(r->req.sources.size()) - 1, int(r->req.sources.size()));
        emit q->finished(r->id, meeting);
        break;
    case End::Failed:
        qCWarning(lcStore).noquote() << "Importálás sikertelen:" << message << detail;
        emit q->failed(r->id, message, detail);
        break;
    case End::Cancelled:
        emit q->cancelled(r->id);
        break;
    }
}

} // namespace tanara
