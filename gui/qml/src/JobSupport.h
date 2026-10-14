#pragma once
//
// Tanara QML: shared helpers of the "views" slice's view models (pre-transcript view,
// Summary, Tracks): time formatting, translating the core's machine hints (fixActionHint) into
// a button label + Settings page, and naming the provider. Stateless functions.
//
#include "tanara/domain/Job.h"
#include "tanara/providers/core/ReadinessModel.h"

#include <QString>
#include <QVariantMap>

namespace tanara {
namespace app { struct AppSettings; }
namespace providers { class ProviderRegistry; }
}

namespace tanara_qml::jobsupport {

// "30:34", above an hour "1:16:04". Negative -> "-".
QString formatDuration(qint64 ms);

// "about 4 minutes left" / "less than 1 minute left"; -1 -> empty (no estimate).
QString formatEta(int seconds);

// The phase state's machine name for QML: waiting | running | done | failed | skipped.
QString stageStateName(tanara::StageState state);

// A fix action: the button's label and the shell.openSettings() page.
struct FixAction {
    QString label;      // empty -> no fix button
    QString page;       // "", "providers", "cloud", "summary", "watcher"
    // Context error with LM Studio: the token count of the "Load with larger context" button (the
    // model reloads with this and the task restarts); 0 -> no such button.
    int reloadContext = 0;
    bool isValid() const { return !label.isEmpty(); }
};

// A failed task's fixActionHint ("settings:stt" | "settings:llm" | "settings:llm-context:N" |
// "llm:reload-context:N" | "cloud" | ...) -> button(s).
FixAction fixActionForError(const tanara::JobError& error);

// Gating (canRun) -> the warning bar's content: { title, text, actionLabel, actionPage,
// reason }; reason is the short justification next to the disabled start button. step: Transcribe / Summarize.
QVariantMap blockerInfo(tanara::providers::WorkflowStep step, const tanara::providers::ReadinessResult& result);

// "Soniox · own key" / "Tanara Cloud": the chosen provider of the given step (usesCloud: the step
// runs on Tanara Cloud; the provider names come from the registry).
QString providerLabel(bool usesCloud, const tanara::app::AppSettings& settings,
                      const tanara::providers::ProviderRegistry& registry, tanara::providers::WorkflowStep step);
// The same without settings (no controller): "own key".
QString providerLabel();

} // namespace tanara_qml::jobsupport
