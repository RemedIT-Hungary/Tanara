#include "tanara/provider/ReadinessModel.h"
#include "tanara/provider/ProviderRegistry.h"

namespace tanara {

namespace {

// Egy required, NEM-titok mező értékének kinyerése a futásidejű configból.
// A jól-ismert kulcsok (baseUrl/model/temperature/maxTokens) a ProviderConfig
// saját mezőire mutatnak; minden más a ProviderConfig.extra-ból jön.
// Visszatérés string-formában — a hívó csak az „üres-e?" kérdést teszi fel.
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

// A kiválasztott provider descriptorának végigellenőrzése: required mezők +
// Login-ág. A meeting-állapotot a hívó már leellenőrizte. A `cfg` a kiválasztott
// provider futásidejű configja (sttSelected()/llmSelected()).
ReadinessResult checkProviderConfig(const ProviderDescriptor& desc,
                                    const QString& providerId,
                                    const ProviderConfig& cfg,
                                    const ReadinessModel::SecretProbe& hasSecret)
{
    // (b) required mezők
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
            r.detail          = QStringLiteral("Hiányzik: %1 (%2)")
                                    .arg(field.label, desc.displayName);
            r.fixActionHint   = QStringLiteral("settings:%1:%2")
                                    .arg(providerId, field.key);
            r.missingFieldKey = field.key;
            r.providerId      = providerId;
            return r;
        }
    }

    // (c) Login-ág: a Login-provider „token" jellegű titka hiányzik → nincs bejelentkezve.
    if (desc.authMode == AuthMode::Login
        && !hasSecret(providerId + QStringLiteral(".token"))) {
        ReadinessResult r;
        r.runnable      = false;
        r.blockerKind   = BlockerKind::Auth;
        r.detail        = QStringLiteral("Nincs bejelentkezve.");
        r.fixActionHint = QStringLiteral("login:%1").arg(providerId);
        r.providerId    = providerId;
        return r;
    }

    return ReadinessResult{ /*runnable*/ true };
}

} // namespace

ReadinessModel::ReadinessModel(const AppSettings& settings, SecretProbe hasSecret)
    : m_settings(settings)
    , m_hasSecret(std::move(hasSecret))
{
}

ReadinessResult ReadinessModel::check(WorkflowStep step, const Meeting& meeting) const
{
    switch (step) {
    case WorkflowStep::Record:
        // Meeting-független: a felvétel-állapotot az AppController kezeli, ide nem tartozik.
        return ReadinessResult{ /*runnable*/ true };

    case WorkflowStep::Transcribe: {
        // (a) meeting-állapot: van-e legalább egy aktív hangsáv?
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
            r.detail        = QStringLiteral("Nincs hangsáv az átíráshoz.");
            r.fixActionHint = QStringLiteral("record");
            return r;
        }

        // (b)+(c) a kiválasztott STT provider konfig- és auth-ellenőrzése.
        const QString sttId = m_settings.sttProviderId;
        // Ismeretlen / nem regisztrált provider → a descriptor() üres descriptort adna
        // (required-loop kimarad, authMode default ApiKey) → tévesen runnable lenne.
        if (!SttProviderRegistry::instance().has(sttId)) {
            ReadinessResult r;
            r.runnable      = false;
            r.blockerKind   = BlockerKind::ProviderConfig;
            r.detail        = QStringLiteral("Ismeretlen vagy nem regisztrált STT-provider: %1").arg(sttId);
            r.fixActionHint = QStringLiteral("settings:%1:").arg(sttId);
            r.providerId    = sttId;
            return r;
        }
        const ProviderDescriptor desc = SttProviderRegistry::instance().descriptor(sttId);
        return checkProviderConfig(desc, sttId, m_settings.sttSelected(), m_hasSecret);
    }

    case WorkflowStep::Summarize: {
        // (a) van-e átirat? (tükrözi az AppController summarize-guardját)
        if (!meeting.hasTranscript) {
            ReadinessResult r;
            r.runnable      = false;
            r.blockerKind   = BlockerKind::MeetingState;
            r.detail        = QStringLiteral("Nincs átirat — előbb futtass átírást.");
            r.fixActionHint = QStringLiteral("transcribe");
            return r;
        }

        // (b)+(c) a kiválasztott LLM provider konfig- és auth-ellenőrzése.
        const QString llmId = m_settings.llmProviderId;
        // Ismeretlen / nem regisztrált provider → ld. a Transcribe-ág indoklását.
        if (!LlmProviderRegistry::instance().has(llmId)) {
            ReadinessResult r;
            r.runnable      = false;
            r.blockerKind   = BlockerKind::ProviderConfig;
            r.detail        = QStringLiteral("Ismeretlen vagy nem regisztrált LLM-provider: %1").arg(llmId);
            r.fixActionHint = QStringLiteral("settings:%1:").arg(llmId);
            r.providerId    = llmId;
            return r;
        }
        const ProviderDescriptor desc = LlmProviderRegistry::instance().descriptor(llmId);
        return checkProviderConfig(desc, llmId, m_settings.llmSelected(), m_hasSecret);
    }
    }

    return ReadinessResult{ /*runnable*/ true };
}

} // namespace tanara
