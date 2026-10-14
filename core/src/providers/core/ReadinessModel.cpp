#include "tanara/providers/core/ReadinessModel.h"
#include "tanara/providers/core/ProviderConfig.h"
#include "tanara/providers/core/ProviderRegistry.h"

#include <QCoreApplication>

namespace tanara::providers {

namespace {

// Extracting the value of a required, NON-secret field from the runtime config.
// The well-known keys (baseUrl/model/temperature/maxTokens) point to the ProviderConfig's own fields;
// everything else comes from ProviderConfig.extra.
// Returned in string form: the caller only asks "is it empty?".
QString configValueFor(const ProviderConfig& cfg, const QString& key)
{
    if (key == QStringLiteral("baseUrl"))
        return cfg.baseUrl;
    if (key == QStringLiteral("model"))
        return cfg.model;
    if (key == QStringLiteral("temperature"))
        return QString::number(cfg.temperature);
    if (key == QStringLiteral("maxTokens"))
        return QString::number(cfg.maxTokens);
    return cfg.extra.value(key).toString();
}

// Checking the selected provider's descriptor through: required fields + the Login branch. The
// caller has already checked the meeting state. `cfg` is the selected provider's runtime config
// (sttSelected()/llmSelected()).
ReadinessResult checkProviderConfig(const ProviderDescriptor& desc,
                                    const QString& providerId,
                                    const ProviderConfig& cfg,
                                    const ReadinessModel::SecretProbe& hasSecret)
{
    // (b) required fields
    for (const ConfigField& field : desc.fields) {
        if (!field.required)
            continue;

        bool present = false;
        if (field.isSecret)
            present = hasSecret(field.secretKey);
        else
            present = !configValueFor(cfg, field.key).isEmpty();

        if (!present) {
            ReadinessResult r;
            r.runnable        = false;
            r.blockerKind     = BlockerKind::ProviderConfig;
            r.detail          = QCoreApplication::translate("ReadinessModel", "Hiányzik: %1 (%2)")
                                    .arg(field.label, desc.displayName);
            r.fixActionHint   = QStringLiteral("settings:%1:%2")
                                    .arg(providerId, field.key);
            r.missingFieldKey = field.key;
            r.providerId      = providerId;
            return r;
        }
    }

    // (c) Login branch: the Login provider's "token"-like secret is missing -> not signed in.
    const QString loginKey = desc.loginSecretKey.isEmpty()
        ? providerId + QStringLiteral(".token") : desc.loginSecretKey;
    if (desc.authMode == AuthMode::Login && !hasSecret(loginKey)) {
        ReadinessResult r;
        r.runnable      = false;
        r.blockerKind   = BlockerKind::Auth;
        r.detail        = desc.id == QLatin1String("tanara-cloud")
            ? QCoreApplication::translate("ReadinessModel", "Jelentkezz be a Tanara Cloudba")
            : QCoreApplication::translate("ReadinessModel", "Nincs bejelentkezve.");
        r.fixActionHint = QStringLiteral("login:%1").arg(providerId);
        r.providerId    = providerId;
        return r;
    }

    return ReadinessResult{ /*runnable*/ true };
}

} // namespace

ReadinessModel::ReadinessModel(const ProviderSettings& settings, SecretProbe hasSecret,
                               const ProviderRegistry* registry)
    : m_settings(settings)
    , m_hasSecret(std::move(hasSecret))
    , m_registry(registry ? registry : &ProviderRegistry::defaultInstance())
{
}

ReadinessResult ReadinessModel::check(WorkflowStep step, const Meeting& meeting) const
{
    switch (step) {
    case WorkflowStep::Record:
        // Meeting-independent: the recording state is handled by AppController, it does not belong here.
        return ReadinessResult{ /*runnable*/ true };

    case WorkflowStep::Transcribe: {
        // (a) meeting state: is there at least one active audio track?
        bool hasActiveTrack = false;
        for (const Track& t : meeting.tracks) {
            if (t.active) {
                hasActiveTrack = true;
                break;
            }
        }
        if (!hasActiveTrack) {
            ReadinessResult r;
            r.runnable      = false;
            r.blockerKind   = BlockerKind::MeetingState;
            r.detail        = QCoreApplication::translate("ReadinessModel", "Nincs hangsáv az átíráshoz.");
            r.fixActionHint = QStringLiteral("record");
            return r;
        }

        // (b)+(c) config and auth check of the selected STT provider.
        const QString sttId = m_settings.stt.selectedId;
        // An unknown / unregistered provider: descriptor() would return an empty descriptor (the required
        // loop is skipped, authMode defaults to ApiKey), so it would wrongly be runnable.
        if (!m_registry->has(CapabilityKind::Transcription, sttId)) {
            ReadinessResult r;
            r.runnable      = false;
            r.blockerKind   = BlockerKind::ProviderConfig;
            r.detail        = QCoreApplication::translate("ReadinessModel", "Ismeretlen vagy nem regisztrált STT-provider: %1").arg(sttId);
            r.fixActionHint = QStringLiteral("settings:%1:").arg(sttId);
            r.providerId    = sttId;
            return r;
        }
        const ProviderDescriptor desc = m_registry->descriptor(CapabilityKind::Transcription, sttId);
        return checkProviderConfig(desc, sttId, m_settings.stt.selected(), m_hasSecret);
    }

    case WorkflowStep::Summarize: {
        // (a) is there a transcript? (mirrors AppController's summarize guard)
        if (!meeting.hasTranscript) {
            ReadinessResult r;
            r.runnable      = false;
            r.blockerKind   = BlockerKind::MeetingState;
            r.detail        = QCoreApplication::translate("ReadinessModel", "Nincs átirat — előbb futtass átírást.");
            r.fixActionHint = QStringLiteral("transcribe");
            return r;
        }

        // (b)+(c) config and auth check of the selected LLM provider.
        const QString llmId = m_settings.llm.selectedId;
        // An unknown / unregistered provider: see the reasoning of the Transcribe branch.
        if (!m_registry->has(CapabilityKind::Chat, llmId)) {
            ReadinessResult r;
            r.runnable      = false;
            r.blockerKind   = BlockerKind::ProviderConfig;
            r.detail        = QCoreApplication::translate("ReadinessModel", "Ismeretlen vagy nem regisztrált LLM-provider: %1").arg(llmId);
            r.fixActionHint = QStringLiteral("settings:%1:").arg(llmId);
            r.providerId    = llmId;
            return r;
        }
        const ProviderDescriptor desc = m_registry->descriptor(CapabilityKind::Chat, llmId);
        return checkProviderConfig(desc, llmId, m_settings.llm.selected(), m_hasSecret);
    }
    }

    return ReadinessResult{ /*runnable*/ true };
}

} // namespace tanara::providers