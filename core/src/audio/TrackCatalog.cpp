#include "tanara/audio/TrackCatalog.h"
#include "tanara/audio/WaveformService.h"
#include "tanara/store/MeetingStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace tanara {

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

TrackRole classify(const Track& track, const QVector<Track>& all)
{
    if (track.kind == TrackKind::Mic)   return TrackRole::OwnMic;
    if (track.kind == TrackKind::Other) return TrackRole::Other;

    if (looksLikeCallDevice(track.deviceName))
        return TrackRole::CallAudio;
    // Ha VAN olyan loopback, amelyik név szerint a hívás eszköze, minden más rendszerhang.
    const Track* loudest = nullptr;
    int activeLoopbacks = 0;
    for (const Track& t : all) {
        if (t.kind != TrackKind::Loopback) continue;
        if (looksLikeCallDevice(t.deviceName)) return TrackRole::SystemAudio;
        if (!t.active) continue;
        ++activeLoopbacks;
        if (!loudest || t.peakLevel > loudest->peakLevel) loudest = &t;
    }
    // Név alapján nem dönthető el: a felvett hívás azon a monitoron szólt, amelyiken volt
    // hang — az egyetlen aktív, ill. több közül a leghangosabb.
    if (track.active && loudest && loudest->id == track.id
        && (activeLoopbacks == 1 || loudest->peakLevel > 0.0f))
        return TrackRole::CallAudio;
    return TrackRole::SystemAudio;
}

QStringList friendlyNames(const QVector<Track>& all)
{
    QVector<TrackRole> roles;
    QStringList names;
    roles.reserve(all.size());
    for (const Track& t : all) {
        const TrackRole r = classify(t, all);
        roles.append(r);
        switch (r) {
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
    // Azonos nevek megkülönböztetése: előbb a rövid eszköznévvel, aztán sorszámmal.
    for (int i = 0; i < names.size(); ++i) {
        QVector<int> same;
        for (int j = 0; j < names.size(); ++j)
            if (names.at(j) == names.at(i)) same.append(j);
        if (same.size() < 2) continue;
        const QString base = names.at(i);
        for (int j : same) {
            const QString s = shortDeviceName(all.at(j).deviceName);
            if (!s.isEmpty() && s != base)
                names[j] = QStringLiteral("%1 (%2)").arg(base, s);
        }
    }
    for (int i = 0; i < names.size(); ++i) {
        int n = 1;
        for (int j = i + 1; j < names.size(); ++j)
            if (names.at(j) == names.at(i))
                names[j] = QStringLiteral("%1 %2").arg(names.at(i)).arg(++n);
    }
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
    for (int i = 0; i < m.tracks.size(); ++i) {
        const Track& t = m.tracks.at(i);
        TrackView v;
        v.track = t;
        v.role = tracknames::classify(t, m.tracks);
        v.friendlyName = friendly.at(i);
        v.renamed = !t.customName.trimmed().isEmpty();
        v.displayName = v.renamed ? t.customName.trimmed() : v.friendlyName;
        v.rawDeviceName = t.deviceName;
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
    bool changed = false;
    for (int i = 0; i < m.tracks.size(); ++i) {
        Track& t = m.tracks[i];
        if (t.id != trackId) continue;
        QString n = name.simplified();
        if (n == friendly.at(i)) n.clear();   // a barátságos névre „átnevezés” = nincs egyedi név
        if (t.customName == n) return false;
        t.customName = n;
        changed = true;
        break;
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
