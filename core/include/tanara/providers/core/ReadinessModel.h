#pragma once
//
// ReadinessModel: headless gating logic: "can a workflow step run, and if not, EXACTLY what is
// missing?". What is missing may be meeting state (no audio track / no transcript), provider config (a
// required field of the selected provider is missing), or auth (a Login provider, not signed in).
// One source of truth: the GUI button enabling AND AppController's guards both use it. Does NOT link Widgets.
#include "tanara/domain/Types.h"

#include <QString>
#include <functional>

namespace tanara::providers {

enum class WorkflowStep { Record, Transcribe, Summarize };   // Identify is SOFT: not here
// Cloud = a Tanara Cloud-specific obstacle (client too old, out of balance):
// fixActionHint: "cloud:update" | "cloud:topup". AppController::canRun adds it.
enum class BlockerKind { MeetingState, ProviderConfig, Auth, Cloud };

struct ReadinessResult {
    bool runnable = false;
    BlockerKind blockerKind = BlockerKind::MeetingState;   // only if !runnable
    QString detail;            // human-readable, Hungarian: "No audio track", "Missing Soniox API key"
    QString fixActionHint;     // machine-readable: "settings:<providerId>:<fieldKey>" | "record" | "transcribe" | "login:<providerId>"
    QString missingFieldKey;   // the missing ConfigField.key, if blockerKind==ProviderConfig
    QString providerId;        // which provider needs configuring
};

class ReadinessModel {
public:
    // hasSecret(secretKey): whether there is a (non-empty) secret in the KeyStore for the given key.
    // So the ReadinessModel does not depend on the KeyStore's concrete type / constness.
    using SecretProbe = std::function<bool(const QString& secretKey)>;

    ReadinessModel(const AppSettings& settings, SecretProbe hasSecret);

    ReadinessResult check(WorkflowStep step, const Meeting& meeting) const;

private:
    AppSettings m_settings;     // a value type, a copy is enough
    SecretProbe m_hasSecret;
};

} // namespace tanara::providers