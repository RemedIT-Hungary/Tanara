#pragma once
//
// The OLD, name-keyed person data (before the PersonId change, P11), read ONLY by the one-time
// converter (`tanara-cli migrate person-ids`). The app itself does not read these formats (D8); this
// code is deleted together with the command before 1.0-alpha.
//
// Old shapes:
//  - people.json v1: {"people": ["Name", ...]} + a sibling people-details.json
//    {"people": [{"name", "aliases", "note"}]};
//  - people.json v2: {"version": 2, "people": [{"name", "aliases", "note", "tags", "defaultSide",
//    "defaultSideManual", ...}], "unlisted": [...]};
//  - voiceprints.json (no version): {"people": [{"name", "prints": [ {voiceprint} ]}]};
//  - tags.json v1: "rejected" items with a "person" NAME;
//  - meeting.json (no version): "speakerMap" raw -> NAME, "participants"[].personName,
//    "participantApproval".candidates[].personName;
//  - transcript.speakers.json v1/v2: "participants"[].person, "identified"[raw].person (names);
//  - participants.analysis.json: "clusters"[].matchName.
// Names are compared case-insensitively (trimmed), as the old app did.
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara::cli::legacy {

// One old person record (people.json v1/v2, merged with people-details.json).
struct LegacyPerson {
    QString     name;
    QStringList aliases;
    QString     note;
    QStringList tags;
    QJsonObject extra;      // every other field of the record (defaultSide, defaultSideManual, unknown ones)
    bool        listed = true;
};

struct LegacyPeopleFile {
    bool exists = false;
    bool parsed = false;    // false: missing or unreadable
    int  version = 0;       // 0 = none (v1)
    bool isCurrent = false; // already the new (v3) shape: nothing to convert
    QVector<LegacyPerson> people;   // listed first, then unlisted; one record per name
    QJsonObject rootExtra;          // unknown root fields
    QString error;
};

struct LegacyVoiceprintOwner {
    QString name;
    QJsonArray prints;      // the voiceprint objects verbatim
};

struct LegacyVoiceprintFile {
    bool exists = false;
    bool parsed = false;
    bool isCurrent = false; // version 3 (keyed by PersonId)
    QVector<LegacyVoiceprintOwner> owners;
    QString error;
};

// Reads <people.json> and, for v1, the sibling people-details.json.
LegacyPeopleFile readPeople(const QString& peopleJson);
LegacyVoiceprintFile readVoiceprints(const QString& voiceprintsJson);

// Comparison key of a name (trimmed, case-folded).
QString nameKey(const QString& name);

// A JSON file as an object (empty + error text if missing / unreadable).
QJsonObject readObject(const QString& path, bool* exists, QString* error);

} // namespace tanara::cli::legacy
