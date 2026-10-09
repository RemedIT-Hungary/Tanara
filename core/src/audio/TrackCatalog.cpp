#include "tanara/audio/TrackCatalog.h"
#include "tanara/audio/WaveformService.h"
#include "tanara/store/MeetingStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QReadWriteLock>
#include <QRegularExpression>

#include <algorithm>
#include <numeric>

namespace tanara {

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
    // A headsetek a hívásokhoz külön (mono, kétirányú) profilt adnak; a hívás-appok ezt
    // használják. PipeWire/PulseAudio: „… - Communication”, „… Chat”, HSP/HFP „Hands-Free”,
    // „Headset Head Unit”; WASAPI: „… Hands-Free AG Audio”, „Kommunikációs …”.
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
    QString n = deviceName.trimmed();
    static const QRegularExpression monitorPrefix(
        QStringLiteral("^(monitor of|monitor:|loopback of|loopback:)\\s*"),
        QRegularExpression::CaseInsensitiveOption);
    n.remove(monitorPrefix);
    // „Gyártó Típus - Profil” → „Gyártó Típus”.
    const int dash = n.indexOf(QStringLiteral(" - "));
    if (dash > 0) n = n.left(dash);
    // WASAPI: „Hangszórók (Realtek Audio)” → a zárójeles rész az eszköz.
    static const QRegularExpression paren(QStringLiteral("^[^()]*\\(([^()]+)\\)\\s*$"));
    const QRegularExpressionMatch pm = paren.match(n);
    if (pm.hasMatch()) n = pm.captured(1);
    return n.trimmed();
}

QVector<SegmentInfo> segments(const QVector<Track>& all)
{
    const int n = int(all.size());
    QVector<SegmentInfo> out(n);
    for (int i = 0; i < n; ++i) out[i].leader = i;
    QVector<bool> done(n, false);
    for (int i = 0; i < n; ++i) {
        if (done[i] || all.at(i).deviceName.isEmpty()) continue;
        QVector<int> group;
        for (int j = i; j < n; ++j)
            if (!done[j] && all.at(j).deviceName == all.at(i).deviceName) group.append(j);
        for (int j : group) done[j] = true;
        if (group.size() < 2) continue;
        // Csak ha mind különböző időben indult (lásd a fejlécet).
        QVector<qint64> offs;
        for (int j : group) offs.append(all.at(j).startOffsetMs);
        std::sort(offs.begin(), offs.end());
        if (std::adjacent_find(offs.cbegin(), offs.cend()) != offs.cend()) continue;
        std::stable_sort(group.begin(), group.end(), [&all](int x, int y) {
            return all.at(x).startOffsetMs < all.at(y).startOffsetMs;
        });
        for (int k = 0; k < group.size(); ++k)
            out[group[k]] = SegmentInfo{group.first(), k + 1, int(group.size())};
    }
    return out;
}

namespace {

// A logikai sáv (egy eszköz összes szakasza) összesített állapota a szerep-döntéshez.
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
    // Logikai loopback-sávok (szakaszok összevonva).
    QVector<Logical> logical;
    for (int i = 0; i < all.size(); ++i) {
        const Track& t = all.at(i);
        if (t.kind != TrackKind::Loopback) continue;
        // Ha VAN olyan loopback, amelyik név szerint a hívás eszköze, minden más rendszerhang.
        if (looksLikeCallDevice(t.deviceName)) return TrackRole::SystemAudio;
        const int lead = seg.at(i).leader;
        auto it = std::find_if(logical.begin(), logical.end(),
                               [lead](const Logical& l) { return l.leader == lead; });
        if (it == logical.end()) { logical.append(Logical{lead, false, 0.0f}); it = logical.end() - 1; }
        if (t.active) { it->active = true; it->peak = std::max(it->peak, t.peakLevel); }
    }
    // Név alapján nem dönthető el: a felvett hívás azon a monitoron szólt, amelyiken volt
    // hang — az egyetlen aktív, ill. több közül a leghangosabb.
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
    // Nincs a listában: önmagában, a többiek mellé téve.
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
        // A felhasználó által átnevezett eszköz sávja az ő nevét kapja (a szerep-név helyett).
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
    // Azonos nevek megkülönböztetése — csak a logikai sávok (szakasz-vezetők) között: előbb a
    // rövid eszköznévvel, aztán sorszámmal. A további szakaszok a vezetőjük nevét kapják.
    auto isLeader = [&seg](int i) { return seg.at(i).leader == i; };
    for (int i = 0; i < names.size(); ++i) {
        if (!isLeader(i)) continue;
        QVector<int> same;
        for (int j = 0; j < names.size(); ++j)
            if (isLeader(j) && names.at(j) == names.at(i)) same.append(j);
        if (same.size() < 2) continue;
        const QString base = names.at(i);
        for (int j : same) {
            if (own.contains(all.at(j).deviceName)) continue;   // a saját név már egyedi szándék
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

TrackCatalog::TrackCatalog(MeetingStore* store, QObject* parent)
    : QObject(parent), m_store(store)
{
    qRegisterMetaType<tanara::TrackView>();
}

QVector<TrackView> TrackCatalog::tracks(const Meeting& m)
{
    QVector<TrackView> out;
    out.reserve(m.tracks.size());
    const QStringList friendly = tracknames::friendlyNames(m.tracks);
    const QVector<tracknames::SegmentInfo> seg = tracknames::segments(m.tracks);
    // Sorrend: logikai sávonként (a vezető első előfordulása szerint — a vezető a legkorábbi
    // szakasz, ezért a csoport első tagjának indexét vesszük), azon belül szakasz szerint.
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
    if (n == friendly.at(target)) n.clear();   // a barátságos névre „átnevezés” = nincs egyedi név
    // Az eszköz minden szakasza ugyanazt a nevet kapja (egy logikai sáv).
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

    // Csak hangfájl lehet sáv (a meeting.json, átirat, hullámforma-cache stb. nem). Kiterjesztés
    // nélküli KÜLSŐ fájlt a régi viselkedés szerint ogg-nak veszünk.
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
        // Már a meeting mappájában van → csak ráhivatkozunk. Másik sáv fájlját nem vesszük át,
        // és a LEKEVERÉST sem: sávként később (sáv-törléssel) a keverék is törlődne.
        rel = src.fileName();
        const Qt::CaseSensitivity cs =
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
            Qt::CaseInsensitive;
#else
            Qt::CaseSensitive;
#endif
        const QString mixName = m.mixdownFile.isEmpty() ? QStringLiteral("mixdown.mp3") : m.mixdownFile;
        if (rel.compare(mixName, cs) == 0 || rel.startsWith(QStringLiteral("mixdown."), cs))
            return fail(tr("A lekeverés fájlja nem választható sávnak."));
        for (const Track& t : std::as_const(m.tracks))
            if (t.id != trackId && t.file.compare(rel, cs) == 0)
                return fail(tr("Ez a fájl már egy másik sávhoz tartozik."));
    } else {
        // Bemásoljuk a meeting mappájába (az eredeti érintetlen marad). Az eredeti sáv-
        // fájlnevet tartjuk meg, ha a kiterjesztés egyezik és a hely szabad; különben a
        // forrás kiterjesztésével, ütközésnél sorszámozva.
        QString base = QFileInfo(track->file).completeBaseName();
        if (base.isEmpty()) base = QStringLiteral("track_") + track->id;
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
        m.mixdownDirty = true;   // a keverék a hiányzó sáv nélkül készülhetett
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
        // A lekeverés fájlját sáv-törlés SOSEM törli (régi meeting.json-ban sávként
        // szerepelhet — lásd relocateTrack), és olyat sem, amire megmaradó sáv hivatkozik.
        bool shared = !m.mixdownFile.isEmpty() && t.file == m.mixdownFile;
        for (const Track& o : std::as_const(m.tracks))
            if (o.active && o.file == t.file) shared = true;
        if (!t.file.isEmpty() && !shared) {
            const QString path = QDir(m.folder).filePath(t.file);
            QFile::remove(path);                  // a hangfájl FIZIKAI törlése (explicit kérés)
            WaveformService::removeCache(path);
        }
        ++removed;
    }
    if (removed == 0) return 0;
    m.tracks = kept;
    // Eldobott sáv nem része a keveréknek → a mixdown érvényes marad (nincs mixdownDirty).
    m_store->saveMeeting(m);
    emit tracksChanged(meetingId);
    return removed;
}

} // namespace tanara
