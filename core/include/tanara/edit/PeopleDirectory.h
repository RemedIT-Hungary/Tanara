#pragma once
//
// People for the picker panels: name + whether there is a voiceprint + in how many meetings they
// appear, and accent-/case-insensitive name search ("odon" matches "Odon" with accents).
#include "tanara/edit/SpeakerEditTypes.h"

#include <QString>
#include <QVector>

namespace tanara {

class MeetingStore;
class PeopleStore;
class VoiceprintStore;

// Folded form for searching: lowercase, without accents (o-double-acute -> o, u-double-acute -> u,
// e-acute -> e ...).
QString foldForSearch(const QString& text);

// True if `needle` is a substring of `text` (accent- and case-insensitively). An empty needle matches everything.
bool matchesSearch(const QString& text, const QString& needle);

// The known people (people.json union the voiceprint DB), sorted by name.
// meetingCount: by a speakerMap value, the mic track's fixed name or a manually added participant.
// The nicknames come from the people.json records (also for someone who has only a voiceprint).
QVector<PersonInfo> listPeople(const PeopleStore* people, const VoiceprintStore* voiceprints,
                               MeetingStore* store);

// Filtering the list by the search field's text; those matching first (name start / word start) come first.
// It also searches the nicknames: someone matching only by nickname comes after the name matches, and
// their matchedAlias field tells which nickname matched.
QVector<PersonInfo> filterPeople(const QVector<PersonInfo>& all, const QString& needle);

} // namespace tanara
