#pragma once
//
// WaveformService — sávonkénti hullámforma-csúcsok (peaks) a rajzoláshoz (M09).
//
// Aszinkron: a hangfájlt ffmpeg dekódolja (QProcess, ahogy a kód többi része is), a PCM-et
// az eseményhurokban, darabokban dolgozzuk fel — a UI-szál SOSEM blokkol. Az eredmény a
// meeting mappájában gyorsítótárazódik (<hangfájl>.peaks.json), így a következő megnyitás
// azonnali; a gyorsítótár a forrásfájl méretéhez + módosítási idejéhez kötött (ha a fájl
// változik, újraszámol). A dekódolásból a sáv PONTOS hossza is kijön (durationMs).
//
#include <QObject>
#include <QString>
#include <QVector>
#include <QMetaType>

namespace tanara {

struct TrackPeaks {
    QString        trackId;
    QVector<float> peaks;          // vödrönként a |minta| maximuma, 0..1
    qint64         durationMs = 0; // a dekódolt hang hossza
    bool isValid() const { return !peaks.isEmpty(); }
};

class WaveformService : public QObject {
    Q_OBJECT
public:
    static constexpr int kBuckets = 400;   // a gyorsítótárazott felbontás („pár száz vödör”)

    explicit WaveformService(QObject* parent = nullptr);
    ~WaveformService() override;

    // ---- gyorsítótár (szinkron, kicsi fájl) -------------------------------------------
    static QString cachePath(const QString& audioPath);          // <hangfájl>.peaks.json
    // Érvényes gyorsítótár betöltése; érvénytelen TrackPeaks, ha nincs / elavult / sérült.
    static TrackPeaks loadCached(const QString& audioPath);
    static void removeCache(const QString& audioPath);
    // Újramintavételezés a rajzoláshoz kért vödörszámra (max-tartó; nagyításnál ismétel).
    static QVector<float> resample(const QVector<float>& peaks, int buckets);

    // ---- aszinkron számítás -----------------------------------------------------------
    // Csúcsok kérése egy hangfájlra. Érvényes gyorsítótárnál a peaksReady a következő
    // eseményhurok-körben jön; különben sorba áll (egyszerre legfeljebb 2 ffmpeg fut).
    // Ugyanarra a fájlra futó/váró kérésnél nem indul második.
    void request(const QString& meetingId, const QString& trackId, const QString& audioPath);
    // A meeting váró kéréseinek eldobása és a futók leállítása (pl. másik meeting kiválasztása).
    void cancel(const QString& meetingId);
    bool isPending(const QString& audioPath) const;

    void setFfmpegPath(const QString& path) { m_ffmpeg = path; }

signals:
    void peaksReady(QString meetingId, QString trackId, tanara::TrackPeaks peaks);
    void peaksFailed(QString meetingId, QString trackId, QString error);

private:
    struct Job;
    void startNext();
    void finishJob(Job* job, bool ok, const QString& error);

    QString m_ffmpeg{QStringLiteral("ffmpeg")};
    QVector<Job*> m_queue;     // várakozók
    QVector<Job*> m_running;   // futók (max. 2)
};

} // namespace tanara

Q_DECLARE_METATYPE(tanara::TrackPeaks)
