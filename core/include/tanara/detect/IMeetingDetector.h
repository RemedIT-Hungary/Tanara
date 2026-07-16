#pragma once
//
// IMeetingDetector — egy meeting/hívás detektálásának headless portja.
// A megbízható jel NEM a futó processz, hanem hogy egy hívás-app ÉPP fogja a
// mikrofont (aktív capture-stream). A futó app/ablak-cím MÁSODLAGOS: csak a
// meeting nevéhez/kontextusához. Platformonként külön backend (Linux: pw-dump;
// Windows: WASAPI; macOS: CoreAudio). Headless (core); NEM linkel Qt Widgetset.
//
// Poll-alapú (NEM async job): a poll() olcsó és szinkron; a hívó (a figyelő) egy
// QTimer-rel pörgeti a kívánt intervallummal.
//
#include <QString>
#include <QStringList>

namespace tanara {

// Egy detektálási pillanatkép. active=false → nincs (felismert) meeting.
struct MeetingSignal {
    bool    active = false;   // ismert hívás-app aktívan fogja a mikrofont (nem a miénk)
    QString appId;            // normalizált kulcs, pl. "zoom", "teams", "discord"
    QString appName;          // ember-olvasható név a meeting-címhez, pl. "Zoom"
    QString windowTitle;      // best-effort ablak-cím (másodlagos → meeting-név/kontextus)
    QString sourceRef;        // diagnosztika: melyik stream/session illeszkedett
};

class IMeetingDetector {
public:
    virtual ~IMeetingDetector() = default;

    // Stabil azonosító (pl. "linux-capture", "windows-wasapi", "macos-coreaudio").
    virtual QString id() const = 0;

    // Szinkron, olcsó (jellemzően pár ms). Nincs meeting → {active:false}.
    // KÖTELEZŐ: a saját binárisunk ('tanara') capture-jét (felvétel közben) SOHA
    // ne vegye meetingnek (ön-kizárás), különben a felvevő önmagát detektálná.
    virtual MeetingSignal poll() = 0;

    // Futtatható-e ez a detektor ezen a hoston most (a szükséges eszköz elérhető,
    // az endpoint felderíthető). A registry a createBest()-hez ezt használja.
    virtual bool isAvailable() const = 0;

    // Opcionális testreszabás (a figyelő a settingsből hívja): mely appok számítanak
    // hívás-appnak, és mi a saját binárisunk (ön-kizáráshoz). Default: no-op — a
    // platform-detektorok felülírják, ha támogatják. Beépített defaultokkal működnek e nélkül is.
    virtual void configure(const QStringList& knownCallApps, const QString& selfBinary)
    {
        Q_UNUSED(knownCallApps);
        Q_UNUSED(selfBinary);
    }
};

} // namespace tanara
