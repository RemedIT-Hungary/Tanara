#include "tanara/library/import/AudioImporter.h"
#include "tanara/library/MeetingLayout.h"
#include "tanara/library/store/detail/MeetingJson.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/foundation/log/Logging.h"
#include "tanara/foundation/text/Text.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTimer>
#include <QUuid>

namespace tanara::library {

namespace {

QString trImport(const char* text)
{
    return QCoreApplication::translate("AudioImporter", text);
}

// File-name part for the track: accent-free, lowercase, hyphenated ASCII (like the recorded tracks').
QString slugify(const QString& in)
{
    return text::asciiSlug(in, 48, QStringLiteral("sav"));
}

// The track's name so that TrackCatalog's shortening (tracknames::shortDeviceName, which cuts the
// " - Profile" and "(...)" parts of device names) does not truncate it: the file name shows whole.
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

// The creation time embedded in the file, from the tags (MP4/MOV/MKV: ISO creation_time; BWF WAV: date +
// creation_time separately). Invalid if absent or obviously wrong (an unset clock: 1904 / 1970, or in the future).
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
            // BWF: the date and the time in separate tags (local time).
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

// The data from the probe (the FIRST audio stream counts).
void parseProbe(ImportFileInfo& info, const media::MediaResult& r)
{
    const QString name = QFileInfo(info.path).fileName();
    if (!r.ok() || !r.info || r.info->streams.isEmpty()) {
        info.error = trImport("Nem hang- vagy videófájl (nem olvasható be): %1").arg(name);
        return;
    }
    const media::MediaInfo& mi = *r.info;
    info.hasVideo = mi.hasVideo();   // cover art (attached_pic) is not video
    const media::MediaStream* audio = mi.firstAudio();
    if (!audio || audio->channels <= 0) {
        info.error = trImport("Ebben a fájlban nincs hang: %1").arg(name);
        return;
    }
    info.channels   = audio->channels;
    info.sampleRate = audio->sampleRate;
    info.codec      = audio->codecName;
    double secs = mi.durationSec;
    if (secs <= 0.0) secs = audio->durationSec;
    info.durationMs = secs > 0.0 ? qint64(secs * 1000.0 + 0.5) : 0;
    info.createdAt = embeddedDate(mi.formatTags);
    if (!info.createdAt.isValid())
        info.createdAt = embeddedDate(audio->tags);
    info.ok = true;
}

QString ffmpegMissing()
{
    return trImport("Az ffmpeg nem található a gépen — telepítsd, és próbáld újra.");
}

} // namespace

// ---- free functions ---------------------------------------------------------------

namespace audioimport {

ImportFileInfo probe(const media::MediaTool& media, const QString& path, int timeoutMs)
{
    ImportFileInfo info = baseInfo(path);
    if (!info.error.isEmpty()) return info;
    const media::MediaResult r = media.probe(info.path, timeoutMs);
    if (!r.started) {
        info.error = ffmpegMissing();
        return info;
    }
    parseProbe(info, r);   // a timeout counts as unreadable
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
                // For a single file the channel's name is enough; for several the file's is needed in front.
                t.name = sources.size() > 1 ? QStringLiteral("%1 – %2").arg(base, ch) : ch;
                out.append(t);
            }
        } else {
            ImportPlannedTrack t;
            t.sourceIndex = i;
            t.channels = info.channels >= 2 ? 2 : 1;   // more than two channels -> mixed down to stereo
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
    // Cut the truncated separators off the end of the common part ("interview_", "Smith - ", "recording (").
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
    MeetingRepository*  store = nullptr;
    media::MediaToolPtr tool;
    int            kbps = 64;
    SelfPerson     self;          // the own microphone's speaker

    // The running import. The closing (success / error / cancel) happens in ONE place: close().
    struct Run {
        QString id;
        ImportRequest req;
        QString title;
        QVector<ImportFileInfo> files;
        QVector<ImportPlannedTrack> plan;
        QStringList trackFiles;          // parallel to the plan (relative file names)
        QStringList trackIds;
        QString stagingDir;
        int probeIndex = 0;
        int encodeIndex = -1;
        QVector<qint64> encodedMs;       // per source, the length written by ffmpeg
        QPointer<media::MediaJob> proc;
        bool cancelled = false;
        int lastPercent = -2;
    };
    std::unique_ptr<Run> run;

    void probeNext();
    void beginEncode();
    void encodeNext();
    void onProgress(qint64 outMs);
    void finalize();
    enum class End { Done, Failed, Cancelled };
    void close(End end, const QString& message = {}, const QString& detail = {},
               const Meeting& meeting = {});
    void sweepStaleStaging() const;
    media::TranscodeSpec transcodeSpec(int sourceIndex) const;
};

AudioImporter::AudioImporter(MeetingRepository* store, media::MediaToolPtr media, QObject* parent)
    : QObject(parent), d(std::make_unique<Impl>())
{
    d->q = this;
    d->store = store;
    d->tool = std::move(media);
    qRegisterMetaType<tanara::library::ImportFileInfo>();
    qRegisterMetaType<tanara::library::ImportRequest>();
}

AudioImporter::~AudioImporter()
{
    // Exit during an import: no ffmpeg and no half-made folder may be left.
    if (d->run) {
        if (media::MediaJob* p = d->run->proc) {
            p->disconnect(this);
            p->abort(3000);
        }
        if (!d->run->stagingDir.isEmpty())
            QDir(d->run->stagingDir).removeRecursively();
    }
}

void AudioImporter::setOpusBitrateKbps(int kbps) { d->kbps = kbps > 0 ? kbps : 64; }
void AudioImporter::setSelfPerson(const SelfPerson& self) { d->self = SelfPerson{self.id, self.name.trimmed()}; }
bool AudioImporter::busy() const { return d->run != nullptr; }
QString AudioImporter::currentId() const { return d->run ? d->run->id : QString(); }

void AudioImporter::probeAsync(const QString& path)
{
    ImportFileInfo info = baseInfo(path);
    if (!info.error.isEmpty()) {
        QTimer::singleShot(0, this, [this, info] { emit probed(info); });
        return;
    }
    media::MediaJob* p = d->tool->startProbe(info.path, this);
    connect(p, &media::MediaJob::finished, this, [this, p, info](const media::MediaResult& r) mutable {
        p->deleteLater();
        if (!r.started) info.error = ffmpegMissing();
        else parseProbe(info, r);
        emit probed(info);
    });
    p->start();
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
    // The work starts in the next event-loop turn: the caller has received the id by then.
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
    if (media::MediaJob* p = d->run->proc) {
        if (p->isRunning()) {
            p->cancel();   // the finished branch closes
            return;
        }
    }
    d->close(Impl::End::Cancelled);
}

// Deleting half-made import folders left from a crash a day earlier.
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

    media::MediaJob* p = tool->startProbe(info.path, q);
    r.proc = p;
    const QString id = r.id;
    QObject::connect(p, &media::MediaJob::finished, q, [this, p, id, info](const media::MediaResult& res) mutable {
        p->deleteLater();
        if (!run || run->id != id) return;
        if (!res.started) { close(End::Failed, ffmpegMissing()); return; }
        if (run->cancelled) { close(End::Cancelled); return; }
        parseProbe(info, res);
        if (!info.ok) { close(End::Failed, info.error); return; }
        run->files.append(info);
        ++run->probeIndex;
        probeNext();
    });
    p->start();
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
    // Track ids / file names: a slug made from the name, unique within the meeting.
    QStringList used;
    for (const ImportPlannedTrack& t : r.plan) {
        const QString base = slugify(t.name);
        QString slug = base;
        int n = 2;
        while (used.contains(slug)) slug = base + QLatin1Char('-') + QString::number(n++);
        used << slug;
        r.trackIds << slug;
        r.trackFiles << MeetingLayout::trackFilePrefix() + slug + QStringLiteral(".ogg");
    }
    r.encodedMs.fill(0, r.req.sources.size());
    r.encodeIndex = -1;
    encodeNext();
}

// One source's transcode: the first audio stream -> Opus/ogg, 48 kHz. For splitting, a single
// ffmpeg with several outputs (one mono track per channel): we read the source through once
// (the command line: media::ffmpegargs::transcode).
media::TranscodeSpec AudioImporter::Impl::transcodeSpec(int sourceIndex) const
{
    const Run& r = *run;
    media::TranscodeSpec spec;
    spec.inputPath = r.req.sources.at(sourceIndex).path;
    spec.bitrateKbps = kbps;
    spec.sampleRate = 48000;
    for (int i = 0; i < r.plan.size(); ++i) {
        const ImportPlannedTrack& t = r.plan.at(i);
        if (t.sourceIndex != sourceIndex) continue;
        spec.outputs.append({QDir(r.stagingDir).filePath(r.trackFiles.at(i)), t.channel, t.channels});
    }
    return spec;
}

void AudioImporter::Impl::encodeNext()
{
    Run& r = *run;
    if (r.cancelled) { close(End::Cancelled); return; }
    ++r.encodeIndex;
    if (r.encodeIndex >= r.req.sources.size()) { finalize(); return; }

    media::MediaJob* p = tool->startTranscode(transcodeSpec(r.encodeIndex), q);
    r.proc = p;
    const QString id = r.id;
    const int index = r.encodeIndex;
    QObject::connect(p, &media::MediaJob::progress, q, [this, p, id](qint64 outMs) {
        if (!run || run->id != id || run->proc != p) return;
        onProgress(outMs);
    });
    QObject::connect(p, &media::MediaJob::finished, q, [this, p, id, index](const media::MediaResult& res) {
        p->deleteLater();
        if (!run || run->id != id) return;
        if (!res.started) { close(End::Failed, ffmpegMissing()); return; }
        if (run->cancelled) { close(End::Cancelled); return; }
        const QString name = QFileInfo(run->req.sources.at(index).path).fileName();
        if (!res.normalExit || res.exitCode != 0) {
            close(End::Failed, AudioImporter::tr("Nem sikerült beolvasni: %1").arg(name),
                  QString::fromUtf8(res.stdErr).simplified().left(300));
            return;
        }
        // Every track must exist (an empty / missing output = an error).
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
    emit q->progress(r.id, qMax(r.lastPercent, -1), r.encodeIndex, int(r.req.sources.size()));
    p->start();
}

// The transcode's progress (ffmpeg `-progress`, parsed by the media tool): the length already written.
void AudioImporter::Impl::onProgress(qint64 outMs)
{
    Run& r = *run;
    if (outMs < 0) return;
    r.encodedMs[r.encodeIndex] = qMax(r.encodedMs.at(r.encodeIndex), outMs);

    // Total percentage weighted by the sources' (known) length; none if a length is unknown.
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
            // The user's own microphone: it behaves like a recorded mic track.
            t.kind = TrackKind::Mic;
            t.fixedSpeaker = true;
            t.speakerLabel = self.name.isEmpty() ? QStringLiteral("Mikrofon 1") : self.name;
            // The track's fixed speaker IS the user: the label maps to the own person.
            if (!self.name.isEmpty() && !self.id.isNull()) m.speakerMap.insert(t.speakerLabel, self.id);
        } else {
            t.kind = TrackKind::Other;
            t.fixedSpeaker = false;
        }
        m.tracks.append(t);
    }

    // Final folder: the same name pattern as for a recording (date_time_title-slug).
    const QDir root(store->audioDir());
    const QString base = m.startedAt.toString(QStringLiteral("yyyy-MM-dd_HHmm"))
                         + QLatin1Char('_') + slugify(m.title);
    QString name = base;
    if (root.exists(name)) name = base + QLatin1Char('_') + r.id.left(8);
    m.folder = root.filePath(name);

    // meeting.json is made while still in the hidden folder, so after the rename the folder is already
    // complete (if the process stopped right there, the index rebuild finds it properly).
    const QByteArray json = QJsonDocument(toJson(m)).toJson(QJsonDocument::Indented);
    QSaveFile f(MeetingLayout::path(r.stagingDir, MeetingLayout::meetingJson()));
    const bool written = f.open(QIODevice::WriteOnly) && f.write(json) == json.size() && f.commit();
    if (!written || !QDir().rename(r.stagingDir, m.folder)) {
        close(End::Failed, AudioImporter::tr("Nem sikerült elmenteni az importált felvételt."),
              m.folder);
        return;
    }
    r.stagingDir.clear();
    store->saveMeeting(m);   // index + meetingUpdated (the library picks it up from this)
    close(End::Done, {}, {}, m);
    qCInfo(lcStore).noquote() << "Imported:" << name << "tracks:" << m.tracks.size()
                              << "duration(ms):" << m.durationMs;
}

void AudioImporter::Impl::close(End end, const QString& message, const QString& detail,
                                const Meeting& meeting)
{
    // First release the run (the signal's handler may already start a new one), then the signal.
    const std::unique_ptr<Run> r = std::move(run);
    if (!r) return;
    if (!r->stagingDir.isEmpty())
        QDir(r->stagingDir).removeRecursively();   // no half-made folder stays
    switch (end) {
    case End::Done:
        emit q->progress(r->id, 100, int(r->req.sources.size()) - 1, int(r->req.sources.size()));
        emit q->finished(r->id, meeting);
        break;
    case End::Failed:
        qCWarning(lcStore).noquote() << "Import failed:" << message << detail;
        emit q->failed(r->id, message, detail);
        break;
    case End::Cancelled:
        emit q->cancelled(r->id);
        break;
    }
}

} // namespace tanara::library