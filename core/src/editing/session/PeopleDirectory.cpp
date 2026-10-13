#include "tanara/editing/session/PeopleDirectory.h"

#include "tanara/editing/session/SpeakerOverlay.h"
#include "tanara/library/store/MeetingStore.h"
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
                               library::MeetingStore* store)
{
    // Name merging case-insensitively (people.json and the voiceprint DB treat it this way).
    QVector<PersonInfo> out;
    QHash<QString, int> indexByFolded;
    auto ensure = [&](const QString& name) -> int {
        const QString n = name.trimmed();
        if (n.isEmpty()) return -1;
        const QString key = n.toCaseFolded();
        const auto it = indexByFolded.constFind(key);
        if (it != indexByFolded.constEnd()) return it.value();
        PersonInfo p;
        p.name = n;
        out.append(p);
        indexByFolded.insert(key, out.size() - 1);
        return out.size() - 1;
    };

    if (people)
        for (const QString& n : people->names()) ensure(n);
    if (voiceprints)
        for (const QString& n : voiceprints->people()) {
            const int i = ensure(n);
            if (i < 0) continue;
            out[i].voiceprintCount = voiceprints->sampleCount(n);
            out[i].hasVoiceprint = out[i].voiceprintCount > 0;
        }

    if (people)
        for (PersonInfo& p : out) p.aliases = people->aliases(p.name);

    if (store) {
        const QVector<Meeting> index = store->loadAll();
        for (const Meeting& entry : index) {
            const Meeting m = store->load(entry.id);
            if (m.id.isEmpty()) continue;
            QSet<QString> present;   // a meeting counts a person once
            for (const QString& v : m.speakerMap) present.insert(v.trimmed().toCaseFolded());
            for (const Track& t : m.tracks)
                if (t.fixedSpeaker) present.insert(t.speakerLabel.trimmed().toCaseFolded());
            if (QFile::exists(speakeredit::overlayPath(m.folder))) {
                const SpeakerOverlay ov = speakeredit::loadOverlay(m.folder);
                for (const OverlayParticipant& p : ov.participants)
                    present.insert(p.person.trimmed().toCaseFolded());
            }
            for (const QString& key : std::as_const(present)) {
                const auto it = indexByFolded.constFind(key);
                if (it != indexByFolded.constEnd()) ++out[it.value()].meetingCount;
            }
        }
    }

    std::sort(out.begin(), out.end(), [](const PersonInfo& a, const PersonInfo& b) {
        return QString::localeAwareCompare(a.name, b.name) < 0;
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