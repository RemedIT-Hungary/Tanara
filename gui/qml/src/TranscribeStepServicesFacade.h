#pragma once
//
// TranscribeStepServicesFacade: the real tanara::processing::ITranscribeStepFacade (the
// pre-transcript view), over the store, the task tracker and the settings; the gating, the
// provider's name, the identification switch and the mixdown are handed in as functions (they still
// live in the application's controller). One per controller, built by AppContext::servicesFor(), which
// also relays the controller's mixdown / track notifications.
//
#include "tanara/processing/ITranscribeStepFacade.h"
#include "tanara/providers/core/ReadinessModel.h"

#include <QPointer>

#include <functional>

namespace tanara::app { class SettingsManager; }
namespace tanara::library { class MeetingJobTracker; class MeetingRepository; }

namespace tanara_qml {

class TranscribeStepServicesFacade : public tanara::processing::ITranscribeStepFacade {
    Q_OBJECT
public:
    struct Deps {
        tanara::library::MeetingRepository* store = nullptr;
        tanara::library::MeetingJobTracker* jobs = nullptr;
        tanara::app::SettingsManager*       settings = nullptr;
        std::function<tanara::providers::ReadinessResult(tanara::providers::WorkflowStep, const QString&)> canRun;
        std::function<QString(tanara::providers::WorkflowStep)> providerLabel;
        std::function<bool()> identifyAvailable;
        std::function<bool(const QString&)> identifyAfterTranscription;
        std::function<void(const QString&, bool)> setIdentifyAfterTranscription;
        std::function<void(const QString&)> regenerateMixdown;
        QObject* noteController = nullptr;
    };
    explicit TranscribeStepServicesFacade(Deps deps, QObject* parent = nullptr);

    bool live() const override { return true; }
    tanara::processing::TranscribeStepSnapshot snapshot(const QString& meetingId) const override;
    void setIdentifyAfterTranscription(const QString& meetingId, bool enabled) override;
    void regenerateMixdown(const QString& meetingId) override;
    void clearError(const QString& meetingId) override;
    QObject* noteController() const override { return m_noteController; }

private:
    Deps m_deps;
    QPointer<tanara::library::MeetingRepository> m_store;
    QPointer<tanara::library::MeetingJobTracker> m_jobs;
    QPointer<QObject> m_noteController;
};

} // namespace tanara_qml
