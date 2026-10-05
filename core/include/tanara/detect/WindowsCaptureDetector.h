#pragma once
//
// WindowsCaptureDetector — WASAPI-alapú meeting-detektor Windowsra (id: "windows-wasapi").
//
// Elsődleges jel: a felvevő (capture) végpontok AKTÍV audio-session-jei. Minden aktív
// capture-végponton (IMMDeviceEnumerator::EnumAudioEndpoints(eCapture, ACTIVE)) végigmegyünk
// a session-ökön (IAudioSessionManager2 → IAudioSessionEnumerator); egy AudioSessionStateActive
// session = egy processz épp fogja a mikrofont. A PID-ből képnév (QueryFullProcessImageNameW),
// abból kisbetűs alapnév, és ezt illesztjük az ismert hívás-appokra (detail/WinCaptureRules.h).
//
// Másodlagos jel (ha nincs illeszkedő session, pl. csomagolt/UWP appok): a
// HKCU\...\CapabilityAccessManager\ConsentStore\microphone kulcsai — egy appnál, amelynek
// LastUsedTimeStart-ja későbbi a LastUsedTimeStop-nál (vagy Stop == 0), épp használatban van
// a mikrofon.
//
// Ön-kizárás: a saját PID-ünk és minden "tanara" / "tanara-*" képnevű processz kimarad.
//
// A teljes tartalom Q_OS_WIN alatt (a core CMakeLists GLOB-ol → máshol üres fordítási egység).
// NEM QObject (szinkron poll, nincs moc-függőség) — a LinuxCaptureDetector mintáját követi.
//
#include <QtGlobal>
#if defined(Q_OS_WIN)

#include "tanara/detect/IMeetingDetector.h"

#include <QString>
#include <QStringList>

#include <memory>

namespace tanara {

class WindowsCaptureDetector : public IMeetingDetector {
public:
    WindowsCaptureDetector();
    ~WindowsCaptureDetector() override;

    QString id() const override;
    MeetingSignal poll() override;
    bool isAvailable() const override;
    void configure(const QStringList& knownCallApps, const QString& selfBinary) override;

private:
    struct Impl;                       // COM-objektumok (a fejléc Windows-fejléc-mentes marad)
    std::unique_ptr<Impl> d;
    QStringList m_knownApps;
    QString     m_selfBinary = QStringLiteral("tanara");
};

} // namespace tanara

#endif // Q_OS_WIN
