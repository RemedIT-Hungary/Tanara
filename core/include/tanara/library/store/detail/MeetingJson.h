#pragma once
//
// meeting.json (library-internal): Meeting / Track <-> QJsonObject. Only MeetingRepository and the
// library's own readers use it; everyone else goes through MeetingRepository.
// The meeting's folder is NOT stored: it is where meeting.json is (the reader fills it in).
//
// Version 3: people are referred to by PersonId ("speakerMap": raw label -> personId,
// "participants"[].personId, "participantApproval".candidates[].personId). Older (name-keyed)
// files are not interpreted (a warning points to `tanara-cli migrate person-ids`).
#include "tanara/domain/Meeting.h"

#include <QJsonObject>

namespace tanara::library {

inline constexpr int kMeetingFormatVersion = 3;

QJsonObject toJson(const Track& t);
Track       trackFromJson(const QJsonObject& o);

QJsonObject  toJson(const RecordingGap& g);
RecordingGap recordingGapFromJson(const QJsonObject& o);

QJsonObject toJson(const Meeting& m);
Meeting     meetingFromJson(const QJsonObject& o);   // folder stays empty

} // namespace tanara::library
