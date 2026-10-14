#include "tanara/editing/session/PeopleDirectory.h"

#include "tanara/editing/session/SpeakerOverlay.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/store/PeopleStore.h"
#include "tanara/library/store/VoiceprintStore.h"

#include <QFile>
#include <QHash>
#include <QSet>

#include <algorithm>

namespace tanara::editing {

bool matchesSearch(const QString& text, const QString& needle)
{
    const QString n = text::foldForSearch(needle.trimmed());
    if (n.isEmpty()) return true;
    return text::foldForSearch(text).contains(n);
}

QVector<PersonInfo> listPeople(const library::PeopleStore* people, const library::VoiceprintStore* voiceprints,
                               library::MeetingRepository* store)
{
    // Every person of people.json (voiceprints belong to these people by id).
    QVector<PersonInfo> out;
    QHash<PersonId, int> indexById;
    if (people)
        for (const Person& person : people->people()) {
            PersonInfo p;
            p.personId = person.id;
            p.name = person.displayName;
            p.aliases = person.aliases;
            indexById.insert(person.id, int(out.size()));
            out.append(p);
        }
    if (voiceprints)
        for (const PersonId& id : voiceprints->people()) {
            const auto it = indexById.constFind(id);
            if (it == indexById.constEnd()) continue;   // a voiceprint of an unknown person
            PersonInfo& p = out[it.value()];
            p.voiceprintCount = voiceprints->sampleCount(id);
            p.hasVoiceprint = p.voiceprintCount > 0;
        }

    if (store) {
        const QVector<Meeting> index = store->loadAll();
        for (const Meeting& entry : index) {
            const Meeting m = store->load(entry.id);
            if (m.id.isEmpty()) continue;
            QSet<PersonId> present;   // a meeting counts a person once
            for (const PersonId& v : m.speakerMap) present.insert(v);
            if (QFile::exists(speakeredit::overlayPath(m.folder))) {
                const SpeakerOverlay ov = speakeredit::loadOverlay(m.folder);
                for (const OverlayParticipant& p : ov.participants) present.insert(p.personId);
            }
            for (const PersonId& key : std::as_const(present)) {
                const auto it = indexById.constFind(key);
                if (it == indexById.constEnd()) continue;
                PersonInfo& p = out[it.value()];
                ++p.meetingCount;
                if (!p.lastSeen.isValid() || m.startedAt > p.lastSeen) p.lastSeen = m.startedAt;
            }
        }
    }

    std::sort(out.begin(), out.end(), [](const PersonInfo& a, const PersonInfo& b) {
        const int c = QString::localeAwareCompare(a.name, b.name);
        return c != 0 ? c < 0 : a.personId < b.personId;
    });
    return out;
}

QVector<PersonInfo> filterPeople(const QVector<PersonInfo>& all, const QString& needle)
{
    const QString n = text::foldForSearch(needle.trimmed());
    if (n.isEmpty()) return all;
    QVector<PersonInfo> prefix, word, inner, alias;
    for (const PersonInfo& p : all) {
        const QString folded = text::foldForSearch(p.name);
        const int pos = folded.indexOf(n);
        if (pos < 0) {
            // Not in the name: the first matching among the nicknames.
            for (const QString& a : p.aliases) {
                if (!text::foldForSearch(a).contains(n)) continue;
                PersonInfo hit = p;
                hit.matchedAlias = a;
                alias.append(hit);
                break;
            }
            continue;
        }
        if (pos == 0) prefix.append(p);
        else if (folded.at(pos - 1).isSpace() || folded.at(pos - 1) == QLatin1Char('-')) word.append(p);
        else inner.append(p);
    }
    return prefix + word + inner + alias;
}

} // namespace tanara::editing