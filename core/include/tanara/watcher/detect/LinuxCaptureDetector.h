#pragma once
//
// LinuxCaptureDetector: a PipeWire (`pw-dump`) based meeting detector for Linux.
// An active microphone-holding stream from a known call app = a call is going on (the signal is
// evaluated by PwDumpParser's pure function). The whole content is under Q_OS_LINUX so it is an
// empty translation unit on other platforms (core's CMakeLists uses GLOB).
//
// NOT a QObject: poll() is synchronous, no signal/slot, so no moc dependency (AUTOMOC would not
// process a Q_OBJECT behind an #if reliably).
#include <QtGlobal>
#if defined(Q_OS_LINUX)

#include "tanara/watcher/detect/IMeetingDetector.h"

#include <QString>
#include <QStringList>

namespace tanara::watcher {

class LinuxCaptureDetector : public IMeetingDetector {
public:
    LinuxCaptureDetector();

    QString id() const override;
    MeetingSignal poll() override;
    bool isAvailable() const override;
    void configure(const QStringList& knownCallApps, const QString& selfBinary) override;

private:
    QStringList m_knownApps;                       // known call apps (binary/app-name fragments)
    QString     m_selfBinary = QStringLiteral("tanara");  // self-exclusion
};

} // namespace tanara::watcher
#endif // Q_OS_LINUX
