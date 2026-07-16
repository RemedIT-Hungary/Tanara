#pragma once
//
// LinuxCaptureDetector — PipeWire (`pw-dump`) alapú meeting-detektor Linuxra.
// Aktív mikrofon-fogó stream egy ismert hívás-apptól = folyik hívás (a jelet a
// PwDumpParser tiszta függvénye értékeli). A teljes tartalom Q_OS_LINUX alatt, hogy
// más platformon üres fordítási egység legyen (a core CMakeLists GLOB-ol).
//
// NEM QObject: a poll() szinkron, nincs signal/slot → nincs moc-függőség (a #if
// mögötti Q_OBJECT-et az AUTOMOC nem dolgozná fel megbízhatóan).
//
#include <QtGlobal>
#if defined(Q_OS_LINUX)

#include "tanara/detect/IMeetingDetector.h"

#include <QString>
#include <QStringList>

namespace tanara {

class LinuxCaptureDetector : public IMeetingDetector {
public:
    LinuxCaptureDetector();

    QString id() const override;
    MeetingSignal poll() override;
    bool isAvailable() const override;
    void configure(const QStringList& knownCallApps, const QString& selfBinary) override;

private:
    QStringList m_knownApps;                       // ismert hívás-appok (bináris/app-név részletek)
    QString     m_selfBinary = QStringLiteral("tanara");  // ön-kizárás
};

} // namespace tanara

#endif // Q_OS_LINUX
