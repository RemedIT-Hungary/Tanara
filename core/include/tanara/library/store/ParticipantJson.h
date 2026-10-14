#pragma once
//
// JSON of the participant value types (meeting.json "participants" / "participantApproval", and the
// CLI's --json output). Free functions: QJsonObject <-> DTO.
#include "tanara/domain/Participant.h"

#include <QJsonObject>
#include <QString>

namespace tanara::library {

QJsonObject toJson(const Evidence& e);
Evidence    evidenceFromJson(const QJsonObject& o);
QJsonObject toJson(const Participant& p);
Participant participantFromJson(const QJsonObject& o);
QJsonObject toJson(const ParticipantApproval& a);
ParticipantApproval participantApprovalFromJson(const QJsonObject& o);

// Enum <-> string ("manual|voice|tag|calendar", "sure|doubt|invited", "voice|side|…").
QString participantSourceName(ParticipantSource s);
ParticipantSource participantSourceFromName(const QString& s);
QString participantGroupName(ParticipantGroup g);
ParticipantGroup participantGroupFromName(const QString& s);
QString evidenceKindName(EvidenceKind k);
QString polarityName(Polarity p);

} // namespace tanara::library
