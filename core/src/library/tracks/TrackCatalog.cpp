#include "tanara/library/tracks/TrackCatalog.h"
#include "tanara/domain/AudioDeviceNames.h"
#include "tanara/foundation/platform/Platform.h"
#include "tanara/library/MeetingLayout.h"
#include "tanara/library/tracks/WaveformService.h"
#include "tanara/library/store/MeetingRepository.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QReadWriteLock>
#include <QRegularExpression>

#include <algorithm>
#include <numeric>

namespace tanara::library {

namespace devicenames {

namespace {
QReadWriteLock g_lock;
QMap<QString, QString>& table()
{
    static QMap<QString, QString> t;
    return t;
}
} // namespace

void setOverrides(const QMap<QString, QString>& rawToFriendly)
{
    QMap<QString, QString> clean;
    for (auto it = rawToFriendly.constBegin(); it != rawToFriendly.constEnd(); ++it) {
        const QString name = it.value().simplified();
        if (!it.key().isEmpty() && !name.isEmpty()) clean.insert(it.key(), name);
    }
    QWriteLocker lock(&g_lock);
    table() = clean;
}

QMap<QString, QString> overrides()
{
    QReadLocker lock(&g_lock);
    return table();
}

bool hasOverride(const QString& rawDeviceName)
{
    QReadLocker lock(&g_lock);
    return table().contains(rawDeviceName);
}

QString displayName(const QString& rawDeviceName, const QMap<QString, QString>& overrides)
{
    const QString own = overrides.value(rawDeviceName).simplified();
    if (!own.isEmpty()) return own;
    const QString s = tracknames::shortDeviceName(rawDeviceName);
    return s.isEmpty() ? rawDeviceName : s;
}

QString displayName(const QString& rawDeviceName)
{
    return displayName(rawDeviceName, overrides());
}

} // namespace devicenames

namespace tracknames {

bool looksLikeCallDevice(const QString& deviceName)
{
    // Headsets give a separate (mono, two-way) profile for calls; call apps use it. PipeWire/PulseAudio:
    // "... - Communication", "... Chat", HSP/HFP "Hands-Free", "Headset Head Unit"; WASAPI:
    // "... Hands-Free AG Audio" and the localized "Communication ..." names.
    static const QStringList keys{
        QStringLiteral("communication"), QStringLiteral("kommunik"), QStringLiteral("chat"),
        QStringLiteral("hands-free"), QStringLiteral("handsfree"), QStringLiteral("hfp"),
        QStringLiteral("hsp"), QStringLiteral("head unit"), QStringLiteral("headset"),
        QStringLiteral("voip")};
    const QString n = deviceName.toLower();
    for (const QString& k : keys)
        if (n.contains(k)) return true;
    return false;
}

QString shortDeviceName(const QString& deviceName)
{
    QString n = audionames::stripMonitorPrefix(deviceName);
    // "Vendor Model - Profile" -> "Vendor Model".
    const int dash = n.indexOf(QStringLiteral(" - "));
    if (dash > 0) n = n.left(dash);
    // WASAPI: "Speakers (Realtek Audio)" -> the parenthesized part is the device.
    static const QRegularExpression paren(QStringLiteral("^[^()]*\\(([^()]+)\\)\\s*$"));
    const QRegularExpressionMatch pm = paren.match(n);
    if (pm.hasMatch()) n = pm.captured(1);
    return n.trimmed();
}

QVector<SegmentInfo> segments(const QVector<Track>& all)
{
    const int n = int(all.size());
    // Union-find over the tracks: the spans of one device (by name, see the header) and the
    // segment chains (continuesTrackId) end up in one logical track.
    QVector<int> parent(n);
    for (int i = 0; i < n; ++i) parent[i] = i;
    const auto find = [&parent](int x) {
        while (parent[x] != x) x = parent[x] = parent[parent[x]];
        return x;
    };
    const auto unite = [&](int a, int b) {
        a = find(a);
        b = find(b);
        if (a != b) parent[qMax(a, b)] = qMin(a, b);
    };

    QVector<bool> done(n, false);
    for (int i = 0; i < n; ++i) {
        if (done[i] || all.at(i).deviceName.isEmpty()) continue;
        QVector<int> group;
        for (int j = i; j < n; ++j)
            if (!done[j] && all.at(j).deviceName == all.at(i).deviceName) group.append(j);
        for (int j : group) done[j] = true;
        if (group.size() < 2) continue;
        // Only if all started at different times (see the header).
        QVector<qint64> offs;
        for (int j : group) offs.append(all.at(j).startOffsetMs);
        std::sort(offs.begin(), offs.end());
        if (std::adjacent_find(offs.cbegin(), offs.cend()) != offs.cend()) continue;
        for (int j : group) unite(group.first(), j);
    }
    // A re-opened segment (after a sleep / a lost device) names the one it continues: one logical
    // track even if the device came back under another name.
    for (int i = 0; i < n; ++i) {
        const QString& prev = all.at(i).continuesTrackId;
        if (prev.isEmpty()) continue;
        for (int j = 0; j < n; ++j)
            if (j != i && all.at(j).id == prev) { unite(i, j); break; }
    }

    QVector<SegmentInfo> out(n);
    for (int i = 0; i < n; ++i) out[i].leader = i;
    QHash<int, QVector<int>> groups;
    for (int i = 0; i < n; ++i) groups[find(i)].append(i);
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        QVector<int>& group = it.value();
        if (group.size() < 2) continue;
        std::stable_sort(group.begin(), group.end(), [&all](int x, int y) {
            return all.at(x).startOffsetMs < all.at(y).startOffsetMs;
        });
        for (int k = 0; k < group.size(); ++k)
            out[group[k]] = SegmentInfo{group.first(), k + 1, int(group.size())};
    }
    return out;
}

std::optional<RecordingGap> gapBefore(const Meeting& meeting, int trackIndex)
{
    if (trackIndex < 0 || trackIndex >= meeting.tracks.size() || meeting.gaps.isEmpty()) return std::nullopt;
    const QVector<SegmentInfo> seg = segments(meeting.tracks);
    const SegmentInfo& me = seg.at(trackIndex);
    if (me.segment < 2) return std::nullopt;
    const Track& t = meeting.tracks.at(trackIndex);
    // The previous segment of the same logical track: the gap lies between its start and ours.
    qint64 prevStart = -1;
    for (int j = 0; j < seg.size(); ++j)
        if (seg.at(j).leader == me.leader && seg.at(j).segment == me.segment - 1)
            prevStart = meeting.tracks.at(j).startOffsetMs;
    // A lost device names the segment it resumed; a sleep is meeting-wide.
    for (const RecordingGap& g : meeting.gaps)
        if (!g.trackId.isEmpty() && g.trackId == t.id) return g;
    std::optional<RecordingGap> best;
    for (const RecordingGap& g : meeting.gaps) {
        if (!g.trackId.isEmpty()) continue;
        if (g.toMs > t.startOffsetMs || g.fromMs < prevStart) continue;
        if (!best || g.toMs > best->toMs) best = g;
    }
    return best;
}

namespace {

// The logical track's (all spans of a device) aggregate state for the role decision.
struct Logical {
    int leader = -1;
    bool active = false;
    float peak = 0.0f;
};

TrackRole classifyAt(int idx, const QVector<Track>& all, const QVector<SegmentInfo>& seg)
{
    const Track& track = all.at(idx);
    if (track.kind == TrackKind::Mic)   return TrackRole::OwnMic;
    if (track.kind == TrackKind::Other) return TrackRole::Other;

    if (looksLikeCallDevice(track.deviceName))
        return TrackRole::CallAudio;
    // Logical loopback tracks (spans merged).
    QVector<Logical> logical;
    for (int i = 0; i < all.size(); ++i) {
        const Track& t = all.at(i);
        if (t.kind != TrackKind::Loopback) continue;
        // If there IS a loopback whose name says it is the call's device, every other one is system audio.
        if (looksLikeCallDevice(t.deviceName)) return TrackRole::SystemAudio;
        const int lead = seg.at(i).leader;
        auto it = std::find_if(logical.begin(), logical.end(),
                               [lead](const Logical& l) { return l.leader == lead; });
        if (it == logical.end()) { logical.append(Logical{lead, false, 0.0f}); it = logical.end() - 1; }
        if (t.active) { it->active = true; it->peak = std::max(it->peak, t.peakLevel); }
    }
    // Cannot be decided by name: the recorded call played on the monitor that had sound: the only
    // active one, or the loudest of several.
    const Logical* loudest = nullptr;
    int activeLoopbacks = 0;
    for (const Logical& l : std::as_const(logical)) {
        if (!l.active) continue;
        ++activeLoopbacks;
        if (!loudest || l.peak > loudest->peak) loudest = &l;
    }
    const int myLead = seg.at(idx).leader;
    bool myActive = false;
    for (const Logical& l : std::as_const(logical)) if (l.leader == myLead) myActive = l.active;
    if (myActive && loudest && loudest->leader == myLead
        && (activeLoopbacks == 1 || loudest->peak > 0.0f))
        return TrackRole::CallAudio;
    return TrackRole::SystemAudio;
}

} // namespace

TrackRole classify(const Track& track, const QVector<Track>& all)
{
    for (int i = 0; i < all.size(); ++i)
        if (all.at(i).id == track.id && all.at(i).file == track.file)
            return classifyAt(i, all, segments(all));
    // Not in the list: alone, placed next to the others.
    QVector<Track> withIt = all;
    withIt.append(track);
    return classifyAt(int(withIt.size()) - 1, withIt, segments(withIt));
}

QStringList friendlyNames(const QVector<Track>& all)
{
    const QVector<SegmentInfo> seg = segments(all);
    QStringList names;
    const QMap<QString, QString> own = devicenames::overrides();
    for (int i = 0; i < all.size(); ++i) {
        const Track& t = all.at(i);
        // A track of a device the user renamed gets their name (instead of the role name).
        if (const QString o = own.value(t.deviceName); !o.isEmpty()) {
            names << o;
            continue;
        }
        switch (classifyAt(i, all, seg)) {
        case TrackRole::OwnMic:
            names << QCoreApplication::translate("TrackCatalog", "Saját mikrofon"); break;
        case TrackRole::CallAudio:
            names << QCoreApplication::translate("TrackCatalog", "Hívás hangja"); break;
        case TrackRole::SystemAudio:
            names << QCoreApplication::translate("TrackCatalog", "Rendszerhang"); break;
        case TrackRole::Other: {
            const QString s = shortDeviceName(t.deviceName);
            names << (s.isEmpty() ? QCoreApplication::translate("TrackCatalog", "Hangsáv") : s);
            break;
        }
        }
    }
    // Telling identical names apart, only between logical tracks (span leaders): first with the short
    // device name, then with a number. Further spans get their leader's name.
    auto isLeader = [&seg](int i) { return seg.at(i).leader == i; };
    for (int i = 0; i < names.size(); ++i) {
        if (!isLeader(i)) continue;
        QVector<int> same;
        for (int j = 0; j < names.size(); ++j)
            if (isLeader(j) && names.at(j) == names.at(i)) same.append(j);
        if (same.size() < 2) continue;
        const QString base = names.at(i);
        for (int j : same) {
            if (own.contains(all.at(j).deviceName)) continue;   // the user's own name is already a deliberate unique choice
            const QString s = shortDeviceName(all.at(j).deviceName);
            if (!s.isEmpty() && s != base)
                names[j] = QStringLiteral("%1 (%2)").arg(base, s);
        }
    }
    for (int i = 0; i < names.size(); ++i) {
        if (!isLeader(i)) continue;
        int n = 1;
        for (int j = i + 1; j < names.size(); ++j)
            if (isLeader(j) && names.at(j) == names.at(i))
                names[j] = QStringLiteral("%1 %2").arg(names.at(i)).arg(++n);
    }
    for (int i = 0; i < names.size(); ++i)
        if (!isLeader(i)) names[i] = names.at(seg.at(i).leader);
    return names;
}

} // namespace tracknames

TrackCatalog::TrackCatalog(MeetingRepository* store, QObject* parent)
    : QObject(parent), m_store(store)
{
    qRegisterMetaType<tanara::library::TrackView>();
}

QVector<TrackView> TrackCatalog::tracks(const Meeting& m)
{
    QVector<TrackView> out;
    out.reserve(m.tracks.size());
    const QStringList friendly = tracknames::friendlyNames(m.tracks);
    const QVector<tracknames::SegmentInfo> seg = tracknames::segments(m.tracks);
    // Order: per logical track (by the leader's first occurrence; the leader is the earliest span, so we
    // take the group's first member's index), within it by span.
    QVector<int> order(m.tracks.size());
    std::iota(order.begin(), order.end(), 0);
    QVector<int> groupPos(m.tracks.size(), -1);
    for (int i = 0; i < m.tracks.size(); ++i) {
        int& p = groupPos[seg.at(i).leader];
        if (p < 0) p = i;
    }
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        const int ga = groupPos.at(seg.at(a).leader), gb = groupPos.at(seg.at(b).leader);
        if (ga != gb) return ga < gb;
        return seg.at(a).segment < seg.at(b).segment;
    });
    for (int i : std::as_const(order)) {
        const Track& t = m.tracks.at(i);
        TrackView v;
        v.track = t;
        v.role = tracknames::classifyAt(i, m.tracks, seg);
        v.friendlyName = friendly.at(i);
        v.renamed = !t.customName.trimmed().isEmpty();
        v.displayName = v.renamed ? t.customName.trimmed() : v.friendlyName;
        v.rawDeviceName = t.deviceName;
        v.segment = seg.at(i).segment;
        v.segmentCount = seg.at(i).count;
        v.gapBefore = tracknames::gapBefore(m, i);
        v.absolutePath = QDir(m.folder).filePath(t.file);
        const QFileInfo fi(v.absolutePath);
        v.fileMissing = t.file.isEmpty() || !fi.isFile();
        v.fileSize = v.fileMissing ? 0 : fi.size();
        if (!v.fileMissing) {
            const TrackPeaks cached = WaveformService::loadCached(v.absolutePath);
            if (cached.isValid()) v.durationMs = cached.durationMs;
        }
        out.append(v);
    }
    return out;
}

QVector<TrackView> TrackCatalog::tracks(const QString& meetingId) const
{
    if (!m_store) return {};
    const Meeting m = m_store->load(meetingId);
    if (m.id.isEmpty()) return {};
    return tracks(m);
}

int TrackCatalog::droppedTrackCount(const QString& meetingId) const
{
    if (!m_store) return 0;
    const Meeting m = m_store->load(meetingId);
    int n = 0;
    for (const Track& t : m.tracks) if (!t.active) ++n;
    return n;
}

bool TrackCatalog::renameTrack(const QString& meetingId, const QString& trackId, const QString& name)
{
    if (!m_store) return false;
    Meeting m = m_store->load(meetingId);
    if (m.id.isEmpty()) return false;
    const QStringList friendly = tracknames::friendlyNames(m.tracks);
    const QVector<tracknames::SegmentInfo> seg = tracknames::segments(m.tracks);
    int target = -1;
    for (int i = 0; i < m.tracks.size(); ++i)
        if (m.tracks.at(i).id == trackId) { target = i; break; }
    if (target < 0) return false;
    QString n = name.simplified();
    if (n == friendly.at(target)) n.clear();   // "renaming" to the friendly name = no custom name
    // Every span of the device gets the same name (one logical track).
    bool changed = false;
    for (int i = 0; i < m.tracks.size(); ++i) {
        if (seg.at(i).leader != seg.at(target).leader) continue;
        Track& t = m.tracks[i];
        if (t.customName == n) continue;
        t.customName = n;
        changed = true;
    }
    if (!changed) return false;
    m_store->saveMeeting(m);
    emit tracksChanged(meetingId);
    return true;
}

bool TrackCatalog::relocateTrack(const QString& meetingId, const QString& trackId,
                                 const QString& newFilePath, QString* error)
{
    auto fail = [error](const QString& msg) { if (error) *error = msg; return false; };
    if (!m_store) return fail(tr("Nincs meeting-tár."));
    Meeting m = m_store->load(meetingId);
    if (m.id.isEmpty()) return fail(tr("Ismeretlen meeting: %1").arg(meetingId));
    Track* track = nullptr;
    for (Track& t : m.tracks) if (t.id == trackId) { track = &t; break; }
    if (!track) return fail(tr("Ismeretlen sáv: %1").arg(trackId));

    const QFileInfo src(newFilePath);
    if (!src.isFile() || !src.isReadable())
        return fail(tr("A kiválasztott fájl nem olvasható: %1").arg(newFilePath));
    if (src.size() == 0)
        return fail(tr("A kiválasztott fájl üres: %1").arg(newFilePath));

    // Only an audio file can be a track (not meeting.json, the transcript, the waveform cache etc.). An
    // EXTERNAL file without an extension is taken as ogg, as in the old behavior.
    static const QStringList kAudioExt{
        QStringLiteral("ogg"), QStringLiteral("oga"), QStringLiteral("opus"), QStringLiteral("wav"),
        QStringLiteral("mp3"), QStringLiteral("flac"), QStringLiteral("m4a"), QStringLiteral("aac"),
        QStringLiteral("mp4"), QStringLiteral("wma"), QStringLiteral("webm"), QStringLiteral("mka"),
        QStringLiteral("aif"), QStringLiteral("aiff"), QStringLiteral("amr"), QStringLiteral("3gp")};
    const QString srcExt = src.suffix().toLower();
    const bool inFolder = src.absoluteDir() == QDir(QDir(m.folder).absolutePath());
    if (inFolder ? !kAudioExt.contains(srcExt) : (!srcExt.isEmpty() && !kAudioExt.contains(srcExt)))
        return fail(tr("A kiválasztott fájl nem hangfájl: %1").arg(src.fileName()));

    const QDir folder(m.folder);
    QString rel;
    if (inFolder) {
        // Already in the meeting's folder: we only reference it. Another track's file is not taken over,
        // nor is the MIXDOWN: as a track it would later be deleted with a track delete, and the mix with it.
        rel = src.fileName();
        const Qt::CaseSensitivity cs =
            paths::caseSensitiveFileSystem() ? Qt::CaseSensitive : Qt::CaseInsensitive;
        const QString mixName = MeetingLayout::mixdownOf(m);
        if (rel.compare(mixName, cs) == 0 || rel.startsWith(QStringLiteral("mixdown."), cs))
            return fail(tr("A lekeverés fájlja nem választható sávnak."));
        for (const Track& t : std::as_const(m.tracks))
            if (t.id != trackId && t.file.compare(rel, cs) == 0)
                return fail(tr("Ez a fájl már egy másik sávhoz tartozik."));
    } else {
        // We copy it into the meeting's folder (the original stays untouched). We keep the original
        // track file name if the extension matches and the spot is free; otherwise with the source's
        // extension, numbered on collision.
        QString base = QFileInfo(track->file).completeBaseName();
        if (base.isEmpty()) base = MeetingLayout::trackFilePrefix() + track->id;
        const QString ext = src.suffix().isEmpty() ? QStringLiteral("ogg") : src.suffix().toLower();
        rel = QStringLiteral("%1.%2").arg(base, ext);
        for (int n = 2; folder.exists(rel); ++n)
            rel = QStringLiteral("%1-%2.%3").arg(base).arg(n).arg(ext);
        if (!QFile::copy(src.absoluteFilePath(), folder.filePath(rel)))
            return fail(tr("Nem sikerült a fájlt a megbeszélés mappájába másolni."));
    }

    if (!track->file.isEmpty() && track->file != rel)
        WaveformService::removeCache(folder.filePath(track->file));
    track->file = rel;
    if (track->active)
        m.mixdownDirty = true;   // the mix may have been made without the missing track
    m_store->saveMeeting(m);
    emit tracksChanged(meetingId);
    return true;
}

int TrackCatalog::deleteDroppedTracks(const QString& meetingId)
{
    if (!m_store) return 0;
    Meeting m = m_store->load(meetingId);
    if (m.id.isEmpty()) return 0;
    QVector<Track> kept;
    int removed = 0;
    for (const Track& t : std::as_const(m.tracks)) {
        if (t.active) { kept.append(t); continue; }
        // Track deletion NEVER deletes the mixdown file (in an old meeting.json it may be listed as a track;
        // see relocateTrack), nor one that a remaining track references.
        bool shared = !m.mixdownFile.isEmpty() && t.file == m.mixdownFile;
        for (const Track& o : std::as_const(m.tracks))
            if (o.active && o.file == t.file) shared = true;
        if (!t.file.isEmpty() && !shared) {
            const QString path = QDir(m.folder).filePath(t.file);
            QFile::remove(path);                  // PHYSICAL deletion of the audio file (an explicit request)
            WaveformService::removeCache(path);
        }
        ++removed;
    }
    if (removed == 0) return 0;
    m.tracks = kept;
    // A dropped track is not part of the mix, so the mixdown stays valid (no mixdownDirty).
    m_store->saveMeeting(m);
    emit tracksChanged(meetingId);
    return removed;
}

} // namespace tanara::library