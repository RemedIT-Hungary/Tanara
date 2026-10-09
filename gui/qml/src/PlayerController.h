#pragma once
//
// PlayerController — a kijelölt megbeszélés lekeverésének lejátszója (gui/qml/CONTRACT.md).
//
// Egy példány él, a Main.qml birtokolja; a tartalom-komponensek `player` property-ként kapják.
//  - meetingId beállítása betölti a megbeszélés hangját (lekeverés; ha nincs, a legnagyobb
//    aktív sáv) és megállítja a lejátszást. A hang-motor LUSTÁN jön létre (az első
//    lejátszáskor), hogy az indulás ne triggerelje a Qt Multimedia eszköz-próbáit.
//  - positionMs lejátszás közben ~25×/mp frissül (az átirat-kiemeléshez).
//  - playRange() egy megszólalást játszik le, majd szünetel.
//  - playFile() egy másik fájlt (egy sávot) hallgat bele; közben a lekeverés pozíciója és
//    a `playing` érintetlen (az előnézet állapota: previewPath / previewPlaying /
//    previewPositionMs / previewDurationMs). stopPreview() visszatér a lekeveréshez.
//    Az előnézet a sávfájlt ÖNMAGÁBAN játssza (fájl-idő, 0-tól): egy később kezdődő sáv
//    (Track::startOffsetMs) előnézete a sáv saját elejétől szól, a megbeszélés-idő nem számít.
//
#include "PlayerBackend.h"

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <functional>

namespace tanara {
class AppController;
}

namespace tanara_qml {

class PlayerController : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString meetingId READ meetingId WRITE setMeetingId NOTIFY meetingIdChanged)
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(int positionMs READ positionMs NOTIFY positionMsChanged)
    Q_PROPERTY(int durationMs READ durationMs NOTIFY durationMsChanged)
    Q_PROPERTY(qreal rate READ rate WRITE setRate NOTIFY rateChanged)
    Q_PROPERTY(qreal volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(QString previewPath READ previewPath NOTIFY previewPathChanged)
    // A szerződésen túli kényelmi állapot az előnézethez (Sávok fül).
    Q_PROPERTY(bool previewPlaying READ previewPlaying NOTIFY previewPlayingChanged)
    Q_PROPERTY(int previewPositionMs READ previewPositionMs NOTIFY previewPositionMsChanged)
    Q_PROPERTY(int previewDurationMs READ previewDurationMs NOTIFY previewDurationMsChanged)
    // A lejátszott hangfájl (lekeverés vagy tartalék-sáv) abszolút útja; üres, ha nincs.
    Q_PROPERTY(QString audioPath READ audioPath NOTIFY availableChanged)

public:
    using BackendFactory = std::function<PlayerBackend*(QObject* parent)>;
    // A valódi hang-motor gyára (a main.cpp állítja be). Üres → beépített néma motor.
    static void setBackendFactory(BackendFactory factory);
    // Egy önálló hang-motor ugyanebből a gyárból (gyár nélkül a néma motor) — más ablak saját
    // lejátszásához (Személyek: hangminta meghallgatása). A hívó a tulajdonosa (parent).
    static PlayerBackend* createBackend(QObject* parent);

    explicit PlayerController(QObject* parent = nullptr);
    ~PlayerController() override;

    // A controller alapból az App-singletoné; tesztben injektálható.
    void setController(tanara::AppController* controller);

    QString meetingId() const { return m_meetingId; }
    void setMeetingId(const QString& id);
    bool available() const { return m_available; }
    bool playing() const { return m_playing; }
    int positionMs() const { return int(m_positionMs); }
    int durationMs() const { return int(m_durationMs); }
    qreal rate() const { return m_rate; }
    void setRate(qreal rate);
    qreal volume() const { return m_volume; }
    void setVolume(qreal volume);
    QString previewPath() const { return m_previewPath; }
    bool previewPlaying() const { return m_previewPlaying; }
    int previewPositionMs() const { return int(m_previewPositionMs); }
    int previewDurationMs() const { return int(m_previewDurationMs); }
    QString audioPath() const { return m_mixPath; }

    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void toggle();
    Q_INVOKABLE void seek(int ms);
    Q_INVOKABLE void playRange(int startMs, int endMs);
    Q_INVOKABLE void playFile(const QString& absolutePath);
    Q_INVOKABLE void stopPreview();
    // A forrás újra-feloldása (pl. elkészült a lekeverés) a pozíció megtartásával.
    Q_INVOKABLE void reload();

signals:
    void meetingIdChanged();
    void availableChanged();
    void playingChanged();
    void positionMsChanged();
    void durationMsChanged();
    void rateChanged();
    void volumeChanged();
    void previewPathChanged();
    void previewPlayingChanged();
    void previewPositionMsChanged();
    void previewDurationMsChanged();
    void errorOccurred(const QString& message);

private:
    void attach();
    void resolveSource();
    void ensureBackend();
    void loadMix();
    void startMix();
    void setPosition(qint64 ms);
    void setPlaying(bool playing);
    void onTick();
    void onBackendPlaying(bool playing);
    void onBackendDuration(qint64 ms);
    void onBackendFinished();

    QPointer<tanara::AppController> m_controller;
    QPointer<tanara::AppController> m_attached;     // akinek a jeleire épp rá vagyunk kötve
    bool m_controllerInjected = false;
    PlayerBackend* m_backend = nullptr;
    QTimer m_tick;

    QString m_meetingId;
    QString m_mixPath;          // a megbeszélés hangja (üres: demó / nincs)
    bool m_mixLoaded = false;   // a motorban épp a megbeszélés hangja van (nem előnézet)
    bool m_available = false;
    bool m_playing = false;
    qint64 m_positionMs = 0;
    qint64 m_durationMs = 0;
    bool m_ended = false;       // a forrás végére ért (a motor ilyenkor 0-t is jelenthet)
    qint64 m_rangeEndMs = -1;   // playRange vége; -1 = folyamatos lejátszás
    qreal m_rate = 1.0;
    qreal m_volume = 1.0;

    QString m_previewPath;
    bool m_previewPlaying = false;
    qint64 m_previewPositionMs = 0;
    qint64 m_previewDurationMs = 0;
};

} // namespace tanara_qml
