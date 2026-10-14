#pragma once
//
// meeting.json (library-internal): Meeting / Track <-> QJsonObject. Only MeetingRepository and the
// library's own readers use it; everyone else goes through MeetingRepository.
// The meeting's folder is NOT stored: it is where meeting.json is (the reader fills it in).
#include "tanara/domain/Meeting.h"

#include <QJsonObject>

namespace tanara::library {

QJsonObject toJson(const Track& t);
Track       trackFromJson(const QJsonObject& o);

QJsonObject toJson(const Meeting& m);
Meeting     meetingFromJson(const QJsonObject& o);   // folder stays empty

} // namespace tanara::library
