#pragma once
//
// Tanara QML — a tanara::JobKind értékei QML-ből: `JobKinds.Transcribe` stb. A
// shell.cancelJob(meetingId, kind) ezt a számot várja. Csak enum-hordozó (nem példányosítható).
//
#include "tanara/jobs/JobTypes.h"

#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace tanara_qml {

class JobKinds : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("JobKinds csak az enum-értékeket hordozza.")
public:
    enum Kind {
        Transcribe    = int(tanara::JobKind::Transcribe),
        Summarize     = int(tanara::JobKind::Summarize),
        ExtractTopics = int(tanara::JobKind::ExtractTopics),
        AnalyzeTopics = int(tanara::JobKind::AnalyzeTopics),
        Mixdown       = int(tanara::JobKind::Mixdown),
        Identify      = int(tanara::JobKind::Identify),
        Import        = int(tanara::JobKind::Import),
    };
    Q_ENUM(Kind)
};

} // namespace tanara_qml
