#pragma once
//
// PlaybackRouting — „melyik alkalmazás melyik kimenetre szól épp” + az audio-eszközök
// halmazának változása (hot-plug jel). A felvevő ebből írja ki az eszköz-sor alá, hogy
// pl. „▸ Microsoft Teams”, és ebből tudja meg olcsón, hogy újra kell-e sorolni az eszközöket.
//
// Linux: PipeWire (`pw-dump`) — Stream/Output/Audio node → Link → Audio/Sink node. A
// PulseAudio-kompatibilis réteg (pipewire-pulse) alatt is ez a gráf él.
// Windows / macOS: nincs megvalósítva (WASAPI audio session-ök végpontonként lenne) —
// az available() hamis, a pillanatkép üres; a hívó ilyenkor egyszerűen nem ír ki appot.
//
// Headless (core); NEM linkel Qt Gui/Widgets-et.
//
#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <memory>

namespace tanara {

struct PlaybackRoute {
    QString appName;           // ember-olvasható („Microsoft Teams”, „Firefox”)
    QString appBinary;         // application.process.binary (kisbetűs)
    QString sinkName;          // a kimenet node.name-je (pl. alsa_output.usb-…)
    QString sinkDescription;   // a kimenet leírása — a monitor-eszköz neve „Monitor of <ez>”
    bool    running = false;   // a stream épp szól (nem szüneteltetett)
};

struct AudioGraphSnapshot {
    QVector<PlaybackRoute> routes;
    // A hangeszközök (Audio/Sink + Audio/Source node-ok) rendezett kulcslistája: ha ez
    // változik, eszközt dugtak be / húztak ki → újra kell sorolni.
    QStringList deviceKeys;

    // A megadott capture-eszközre (kimenet monitorja / loopbackje, a miniaudio neve szerint,
    // pl. „Monitor of Kanto YU4 - …”) épp játszó alkalmazás neve; több közül a ténylegesen
    // szóló az első. Üres, ha senki nem játszik oda (vagy az eszköz nem kimenet-monitor).
    QString appForOutput(const QString& captureDeviceName) const;
    // Fordítva: az adott app (név- vagy bináris-részlet) melyik kimenetre szól (leírás).
    QString outputForApp(const QString& appNameOrBinary) const;
};

namespace detail {
// TISZTA függvény (unit-tesztelhető): a `pw-dump` JSON-jából a lejátszás-útvonalak.
AudioGraphSnapshot parsePwDumpGraph(const QByteArray& json);
} // namespace detail

// Egyszeri, SZINKRON lekérdezés (a figyelő értesítéséhez). Nem elérhető platformon üres.
AudioGraphSnapshot queryAudioGraph(int timeoutMs = 1500);

// Időzített, NEM blokkoló figyelő (a pw-dump aszinkron QProcess-ként fut).
class PlaybackRouteMonitor : public QObject {
    Q_OBJECT
public:
    explicit PlaybackRouteMonitor(QObject* parent = nullptr);
    ~PlaybackRouteMonitor() override;

    // Van-e megvalósítás ezen a hoston (Linux + pw-dump a PATH-on).
    static bool available();

    AudioGraphSnapshot snapshot() const;

public slots:
    void start(int intervalMs = 2000);
    void stop();

signals:
    void routesChanged();      // más app szól más kimenetre
    void deviceSetChanged();   // eszköz jelent meg / tűnt el

private:
    void poll();
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace tanara
