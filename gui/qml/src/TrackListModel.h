#pragma once
//
// Tanara QML — a Sávok fül (M09) modellje: a meeting hangsávjai megjelenítésre készen
// (barátságos név, nyers eszköznév, állapot: aktív / eldobott / hiányzó fájl), sávonként a
// gyorsítótárazott hullámforma-csúcsok, és a lekeverés állapota (kész / elavult / fut %).
//
// Források: TrackCatalog (nevek, átnevezés, megkeresés, eldobottak törlése), WaveformService
// (csúcsok — aszinkron, a requestWaveforms() kéri a fül első megjelenésekor), az
// AppController restoreTrack / regenerateMixdown, a MeetingJobTracker (lekeverés fut-e).
// A core jeleire soronként frissül (azonos sáv-sorrendnél csak dataChanged megy).
//
// Controller nélkül vagy App.demo mellett kitalált mintaadat; demoState: "default" (az M09
// rajz: fut a lekeverés) | "loading" (csúcsok számolása) | "idle" (minden kész).
//
#include "tanara/audio/TrackCatalog.h"
#include "tanara/audio/WaveformService.h"

#include <QAbstractListModel>
#include <QList>
#include <QMap>
#include <QPointer>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace tanara {
class AppController;
}

namespace tanara_qml {

class TrackListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QObject* controller READ controllerObject WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(QString meetingId READ meetingId WRITE setMeetingId NOTIFY meetingIdChanged)
    Q_PROPERTY(QString demoState READ demoState WRITE setDemoState NOTIFY demoStateChanged)
    Q_PROPERTY(bool demo READ demo NOTIFY changed)

    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int activeCount READ activeCount NOTIFY changed)
    Q_PROPERTY(int droppedCount READ droppedCount NOTIFY changed)
    // A leghangosabb sáv csúcsa — a hullámformák közös skálája.
    Q_PROPERTY(qreal peakReference READ peakReference NOTIFY peaksChanged)

    // ---- lekeverés ----
    // "none" (még nincs) | "ready" | "stale" (a sávok változtak azóta) | "running"
    Q_PROPERTY(QString mixdownState READ mixdownState NOTIFY changed)
    Q_PROPERTY(int mixdownPercent READ mixdownPercent NOTIFY mixdownPercentChanged)
    // Önálló lekeverés fut → megszakítható (shell.cancelJob(…, JobKinds.Mixdown)); ha az
    // átírás részeként fut, az átírással együtt szakítható meg.
    Q_PROPERTY(bool mixdownCancellable READ mixdownCancellable NOTIFY changed)
    Q_PROPERTY(bool mixdownCancelling READ mixdownCancelling NOTIFY changed)
    Q_PROPERTY(QString mixdownDurationText READ mixdownDurationText NOTIFY peaksChanged)
    Q_PROPERTY(QList<qreal> mixdownPeaks READ mixdownPeaks NOTIFY peaksChanged)
    Q_PROPERTY(bool mixdownPeaksLoading READ mixdownPeaksLoading NOTIFY peaksChanged)

public:
    enum Role {
        TrackIdRole = Qt::UserRole + 1,
        DisplayNameRole,      // amit a UI mutat (a felhasználó neve vagy a barátságos név)
        FriendlyNameRole,     // a szerepből levezetett név (átnevezés visszavonása)
        RenamedRole,
        RawNameRole,          // nyers eszköznév · fájlnév
        IconNameRole,         // mic | monitor-speaker | speaker | audio-lines
        ActiveRole,           // false = eldobott (csendes)
        MissingRole,          // hiányzik a hangfájl
        PathRole,             // abszolút út (lejátszáshoz)
        DurationTextRole,
        PeaksRole,            // QList<qreal>
        PeaksStateRole,       // none | loading | ready | failed
        ColorIndexRole,       // a hullámforma színe (beszélő-paletta)
        // A sáv helye a megbeszélés idővonalán (0..1): a hullámforma a sor szélességének
        // waveStart-jánál kezdődik (később bekapcsolt sáv / második szakasz) és waveSpan
        // hosszú (a fájl hossza a megbeszéléshez mérve).
        WaveStartRole,
        WaveSpanRole,
        SegmentRole,          // az eszköz szakaszának sorszáma (1..), 0 = egyetlen fájl
        StartOffsetRole,      // ms
    };

    // A hullámforma helye a megbeszélés idővonalán: {kezdet, hossz} 0..1 arányban. Ismeretlen
    // fájlhossz (fileDurationMs < 0) → a sáv a kezdetétől a megbeszélés végéig; ismeretlen
    // megbeszélés-hossz → {0, 1}.
    static QPair<qreal, qreal> waveExtent(qint64 startOffsetMs, qint64 fileDurationMs,
                                          qint64 meetingDurationMs);

    explicit TrackListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QObject* controllerObject() const;
    void setController(QObject* controller);
    QString meetingId() const { return m_meetingId; }
    void setMeetingId(const QString& id);
    QString demoState() const { return m_demoState; }
    void setDemoState(const QString& state);
    bool demo() const;

    int count() const { return int(m_rows.size()); }
    int activeCount() const;
    int droppedCount() const;
    qreal peakReference() const;

    QString mixdownState() const { return m_mixdownState; }
    int mixdownPercent() const { return m_mixdownPercent; }
    bool mixdownCancellable() const { return m_mixdownCancellable; }
    bool mixdownCancelling() const { return m_mixdownCancelling; }
    QString mixdownDurationText() const;
    QList<qreal> mixdownPeaks() const { return m_mixdownPeaks; }
    bool mixdownPeaksLoading() const { return m_mixdownPeaksLoading; }

    // A csúcsok kérése a még hiányzó sávokra (a fül első megjelenésekor hívandó).
    Q_INVOKABLE void requestWaveforms();
    // Átnevezés; üres név → vissza a barátságos névre. true, ha változott.
    Q_INVOKABLE bool rename(int row, const QString& name);
    // Eldobott sáv visszaállítása aktívvá.
    Q_INVOKABLE void restore(int row);
    // Hiányzó fájl megkeresése: a kiválasztott fájl a meeting mappájába másolódik. Üres
    // visszatérés = siker; különben az emberi hibaüzenet.
    QString relocate(int row, const QString& filePath);
    // MINDEN eldobott sáv végleges törlése (a megerősítés a hívó dolga). Vissza: hány törlődött.
    int deleteDropped();
    // A QML ezeket hívja. A megerősítő / fájlválasztó ablak beágyazott eseményhurkot futtat,
    // közben a kijelölés másik megbeszélésre válthat (pl. véget ér egy felvétel): ezért a
    // hívó az ablak ELŐTT megjegyzi a megbeszélést (és a sáv azonosítóját, nem a sorát), és
    // itt adja vissza. Ha a modell közben másik megbeszélésre váltott, NEM történik semmi
    // (-1, ill. hibaüzenet).
    // Egy sáv rajzolási skálája: a saját felső szintje, de legalább a közös skála 30%-a —
    // a halk sáv (pl. a ritkán megszólaló saját mikrofon) is olvasható, a csendes mégsem
    // nagyítódik zajjá.
    Q_INVOKABLE qreal rowReference(int row) const;
    Q_INVOKABLE QString trackIdAt(int row) const;
    Q_INVOKABLE QString relocateTrack(const QString& meetingId, const QString& trackId,
                                      const QString& filePath);
    Q_INVOKABLE int deleteDroppedIn(const QString& meetingId);
    // A lekeverés (újra)készítése az aktív sávokból.
    Q_INVOKABLE void refreshMixdown();

signals:
    void controllerChanged();
    void meetingIdChanged();
    void demoStateChanged();
    void mixdownPercentChanged();
    void peaksChanged();
    void changed();

private:
    struct Row {
        tanara::TrackView view;
        QList<qreal> peaks;
        QString peaksState = QStringLiteral("none");
        qint64 durationMs = -1;
        int colorIndex = 0;
    };

    tanara::AppController* app() const;
    void connectController();
    void reload();
    void reloadMixdown();
    void loadDemo();
    void applyPeaks(const QString& trackId, const tanara::TrackPeaks& peaks, bool failed);
    static QList<qreal> toList(const QVector<float>& v);
    static QList<qreal> levelsOf(const tanara::TrackPeaks& peaks);
    static QString iconFor(tanara::TrackRole role);

    QPointer<QObject> m_injected;
    QPointer<tanara::AppController> m_connected;
    QList<QMetaObject::Connection> m_connections;
    QMap<QString, QString> m_deviceNames;   // az eszköz-átnevezések utolsó látott állapota

    QString m_meetingId;
    QString m_demoState;
    QVector<Row> m_rows;
    qint64 m_meetingDurationMs = 0;
    bool m_waveformsRequested = false;

    QString m_mixdownState = QStringLiteral("none");
    int m_mixdownPercent = -1;
    bool m_mixdownCancellable = false;
    bool m_mixdownCancelling = false;
    QString m_mixdownPath;
    qint64 m_mixdownDurationMs = -1;
    QList<qreal> m_mixdownPeaks;
    bool m_mixdownPeaksLoading = false;
};

} // namespace tanara_qml
