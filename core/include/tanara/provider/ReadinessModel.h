#pragma once
//
// ReadinessModel — headless kapuzás-logika: „futtatható-e egy munkafolyamat-lépés,
// és ha nem, PONTOSAN mi hiányzik?". A hiány lehet meeting-állapot (nincs hangsáv /
// nincs átirat), provider-konfig (a kiválasztott provider kötelező mezője hiányzik),
// vagy auth (Login-provider, nincs bejelentkezve). Egy igazságforrás: a GUI gomb-
// engedélyezés ÉS az AppController guardjai is ezt használják. NEM linkel Widgetset.
//
#include "tanara/Types.h"

#include <QString>
#include <functional>

namespace tanara {

enum class WorkflowStep { Record, Transcribe, Summarize };   // az Identify SOFT → nincs itt
enum class BlockerKind { MeetingState, ProviderConfig, Auth };

struct ReadinessResult {
    bool runnable = false;
    BlockerKind blockerKind = BlockerKind::MeetingState;   // csak ha !runnable
    QString detail;            // ember-olvasható, magyar: „Nincs hangsáv", „Hiányzó Soniox API-kulcs"
    QString fixActionHint;     // gép-olvasható: "settings:<providerId>:<fieldKey>" | "record" | "transcribe" | "login:<providerId>"
    QString missingFieldKey;   // a hiányzó ConfigField.key, ha blockerKind==ProviderConfig
    QString providerId;        // melyik providert kell beállítani
};

class ReadinessModel {
public:
    // hasSecret(secretKey) → van-e (nem-üres) titok a KeyStore-ban az adott kulcsra.
    // Így a ReadinessModel nem függ a KeyStore konkrét típusától / const-ságától.
    using SecretProbe = std::function<bool(const QString& secretKey)>;

    ReadinessModel(const AppSettings& settings, SecretProbe hasSecret);

    ReadinessResult check(WorkflowStep step, const Meeting& meeting) const;

private:
    AppSettings m_settings;     // value-típus, másolat elég
    SecretProbe m_hasSecret;
};

} // namespace tanara
