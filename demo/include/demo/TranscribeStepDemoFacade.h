#pragma once
//
// TranscribeStepDemoFacade: the fictional tanara::processing::ITranscribeStepFacade (the
// pre-transcript view) for --demo, --gallery, --qml-shot and the tests. One instance serves one
// design state (matching the design's M03-M05 screens): "steps" (default: no provider set up),
// "ready", "mixing", "running", "uploading", "failed", "note" (steps with template suggestions and
// a detected call), "readyNote" (can start, with the same). The actions do nothing.
#include "tanara/processing/ITranscribeStepFacade.h"

namespace tanara_demo {

class TranscribeStepDemoFacade : public tanara::processing::ITranscribeStepFacade {
    Q_OBJECT
public:
    explicit TranscribeStepDemoFacade(const QString& scenario = {}, QObject* parent = nullptr);

    QString scenario() const { return m_scenario; }

    bool live() const override { return false; }
    tanara::processing::TranscribeStepSnapshot snapshot(const QString& meetingId) const override;
    void setIdentifyAfterTranscription(const QString&, bool) override {}
    void regenerateMixdown(const QString&) override {}
    void clearError(const QString&) override {}
    QObject* noteController() const override { return nullptr; }

private:
    QString m_scenario;
};

} // namespace tanara_demo
