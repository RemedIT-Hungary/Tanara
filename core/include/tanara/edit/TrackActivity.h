#pragma once
//
// Sávonkénti hang-aktivitás (energia-idősor) a sáv-oldal ellenőrzéshez (SideAnalysis.h).
//
// Logikai sávonként (egy eszköz szakaszai együtt, tracknames::segments) egy rögzített
// keret-hosszú (alapból 50 ms) dB-idősor MEGBESZÉLÉS-időben: keretenként az RMS dB a sáv saját
// zajpadlója felett. A sávfájlokat egyszer dekódoljuk 16 kHz mono s16-ra (egy ffmpeg-futás
// fájlonként, folyamatos feldolgozással — a PCM nem marad a memóriában). A szakaszok közti
// lyukak (és a sáv kezdete előtti / vége utáni rész) NaN: ott a sávnak nincs adata.
//
// A dekóder cserélhető (PcmDecoder) — a unit-tesztek hamis dekóderrel számolnak.
// A cache a meeting-mappában: tracks.activity.bin (a sávfájlok lenyomatához kötve).
//
#include "tanara/Types.h"

#include <QString>
#include <QVector>

#include <functional>
#include <optional>

namespace tanara {

struct TrackActivity {
    QString trackId;                // a logikai sáv első szakaszának id-ja
    TrackKind kind = TrackKind::Other;
    int frameMs = 50;
    qint64 originMs = 0;            // az első keret kezdete MEGBESZÉLÉS-időben
    QVector<float> dbAboveFloor;    // keretenként >= 0; NaN = nincs adat (lyuk)
    float floorDb = 0.0f;           // becsült zajpadló (a keret-dB-k 10. percentilise), dBFS

    // Hány keret hordoz adatot (nem NaN).
    int coveredFrames() const;
    qint64 coveredMs() const { return qint64(coveredFrames()) * frameMs; }
    qint64 endMs() const { return originMs + qint64(dbAboveFloor.size()) * frameMs; }
};

struct MeetingActivity {
    QString fingerprint;            // activityFingerprint(): ha változik, újraszámolunk
    QVector<TrackActivity> tracks;

    bool isEmpty() const { return tracks.isEmpty(); }
    static QString filePath(const QString& meetingFolder);   // tracks.activity.bin
    // Üres (fingerprint-tel kitöltött, sáv nélküli) eredmény, ha nincs fájl / más a lenyomat /
    // ismeretlen a formátum.
    static MeetingActivity load(const QString& meetingFolder, const QString& fingerprint);
    bool save(const QString& meetingFolder) const;
};

namespace trackactivity {

inline constexpr int kSampleRate = 16000;
inline constexpr int kDefaultFrameMs = 50;
// Ennél halkabb keretet (digitális csend) ide vágunk, hogy a log ne menjen -inf-be.
inline constexpr float kMinDb = -90.0f;
// A zajpadló a keret-dB-k ennyiedik percentilise.
inline constexpr double kFloorPercentile = 0.10;

// A dekódolt PCM folyamatosan érkezik (16 kHz mono s16): ezt hívja a dekóder darabonként.
using PcmSink = std::function<void(const qint16* samples, qsizetype count)>;
// Egy fájl dekódolása a sinkbe. false = sikertelen (*error kitöltve, ha nem null).
using PcmDecoder = std::function<bool(const QString& absPath, const PcmSink& sink, QString* error)>;

// A valódi dekóder: ffmpeg → s16le 16 kHz mono, QProcess, folyamatos olvasás.
PcmDecoder ffmpegDecoder(const QString& ffmpegPath = QStringLiteral("ffmpeg"));

// Keret-RMS → dBFS (kMinDb-re vágva). A tiszta, darabolás-független számítás (a tesztekhez is).
QVector<float> frameDb(const QVector<qint16>& pcm, int frameMs);

// A zajpadló: a NEM NaN értékek kFloorPercentile percentilise; üres → kMinDb.
float estimateFloor(const QVector<float>& db);

// A sávfájlok lenyomata: aktív sávonként fájlnév + méret + mtime + eltolás, és a frameMs.
QString activityFingerprint(const Meeting& m, const QString& folder, int frameMs = kDefaultFrameMs);

// Egy időablak energiája egy sávon: a lefedett (nem NaN) keretek LINEÁRIS teljesítményének
// átlaga dB-ben a zajpadló felett (10·log10(mean(10^(d/10)))). A keret akkor számít, ha
// átfed az ablakkal. Nincs lefedett keret → nullopt.
std::optional<float> windowEnergyDb(const TrackActivity& t, qint64 startMs, qint64 endMs);

} // namespace trackactivity

// A meeting aktív sávjainak aktivitása. Logikai sávonként egy TrackActivity; a nem aktív
// szakasz kimarad, a hiányzó / nem dekódolható fájl kimarad (naplózva). A decoder üres →
// ffmpegDecoder(ffmpegPath).
MeetingActivity computeMeetingActivity(const Meeting& m, const QString& folder,
                                       const QString& ffmpegPath = QStringLiteral("ffmpeg"),
                                       int frameMs = trackactivity::kDefaultFrameMs,
                                       const trackactivity::PcmDecoder& decoder = {});

using trackactivity::windowEnergyDb;

} // namespace tanara
