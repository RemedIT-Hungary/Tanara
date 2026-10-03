#pragma once
//
// Személyek a választó panelekhez (K5): név + van-e hanglenyomata + hány meetingen
// szerepel, és az ékezet-/kisbetű-független névkeresés („odon" illik az „Ödön"-re).
//
#include "tanara/edit/SpeakerEditTypes.h"

#include <QString>
#include <QVector>

namespace tanara {

class MeetingStore;
class PeopleStore;
class VoiceprintStore;

// Kereséshez hajtogatott alak: kisbetűs, ékezetek nélkül (ő→o, ű→u, é→e …).
QString foldForSearch(const QString& text);

// Igaz, ha a `needle` (ékezet- és kisbetű-függetlenül) részsztringje a `text`-nek.
// Üres needle mindenre illik.
bool matchesSearch(const QString& text, const QString& needle);

// Az ismert személyek (people.json ∪ hanglenyomat-DB), név szerint rendezve.
// meetingCount: speakerMap-érték, mic-sáv fix neve vagy kézzel felvett résztvevő alapján.
QVector<PersonInfo> listPeople(const PeopleStore* people, const VoiceprintStore* voiceprints,
                               MeetingStore* store);

// A lista szűrése a keresőmező szövegére; az elöl egyezők (névkezdet / szókezdet) előre.
QVector<PersonInfo> filterPeople(const QVector<PersonInfo>& all, const QString& needle);

} // namespace tanara
