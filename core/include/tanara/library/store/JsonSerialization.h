#pragma once
//
// JSON (de)serialization for the value types.
// Free functions in the `tanara` namespace: QJsonObject <-> DTO.
//
// IMPORTANT: when serializing ProviderConfig the `apiKey` does NOT go into the JSON;
// secrets belong in the KeyStore.
#include "tanara/domain/Types.h"
#include <QJsonObject>
#include <QJsonArray>

namespace tanara::library {

// ---- Track ----------------------------------------------------------------
QJsonObject toJson(const Track& t);
Track       trackFromJson(const QJsonObject& o);

// ---- ActionItem / Summary -------------------------------------------------
QJsonObject toJson(const ActionItem& a);
ActionItem  actionItemFromJson(const QJsonObject& o);

QJsonObject toJson(const Summary& s);
Summary     summaryFromJson(const QJsonObject& o);

// ---- participants / evidence ---------------------------------------------
QJsonObject toJson(const Evidence& e);
Evidence    evidenceFromJson(const QJsonObject& o);
QJsonObject toJson(const Participant& p);
Participant participantFromJson(const QJsonObject& o);
QJsonObject toJson(const ParticipantApproval& a);
ParticipantApproval participantApprovalFromJson(const QJsonObject& o);
// Enum ⟷ string ("manual|voice|tag|calendar", "sure|doubt|invited", "voice|side|…").
QString participantSourceName(ParticipantSource s);
ParticipantSource participantSourceFromName(const QString& s);
QString participantGroupName(ParticipantGroup g);
ParticipantGroup participantGroupFromName(const QString& s);
QString evidenceKindName(EvidenceKind k);
QString polarityName(Polarity p);

// ---- Meeting --------------------------------------------------------------
QJsonObject toJson(const Meeting& m);
Meeting     meetingFromJson(const QJsonObject& o);

// ---- ProviderConfig (apiKey KIHAGYVA) -------------------------------------
QJsonObject toJson(const ProviderConfig& p);
ProviderConfig providerConfigFromJson(const QJsonObject& o);

// ---- AppSettings ----------------------------------------------------------
QJsonObject toJson(const AppSettings& s);
AppSettings appSettingsFromJson(const QJsonObject& o);

} // namespace tanara::library