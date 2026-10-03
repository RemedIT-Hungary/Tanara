#include "tanara/edit/PeopleDirectory.h"

#include "tanara/edit/SpeakerOverlay.h"
#include "tanara/store/MeetingStore.h"
#include "tanara/store/PeopleStore.h"
#include "tanara/store/VoiceprintStore.h"

#include <QFile>
#include <QHash>
#include <QSet>

#include <algorithm>

namespace tanara {

QString foldForSearch(const QString& text)
{
    // Kanonikus felbontás (ő → o + kettős ékezet), majd a kombináló jelek eldobása.
    const QString decomposed = text.normalized(QString::NormalizationForm_D);
    QString out;
    out.reserve(decomposed.size());
    for (const QChar c : decomposed) {
        const QChar::Category cat = c.category();
        if (cat == QChar::Mark_NonSpacing || cat == QChar::Mark_SpacingCombining
            || cat == QChar::Mark_Enclosing)
            continue;
        out += c;
    }
    return out.toCaseFolded();
}

bool matchesSearch(const QString& text, const QString& needle)
{
    const QString n = foldForSearch(needle.trimmed());
    if (n.isEmpty()) return true;
    return foldForSearch(text).contains(n);
}

QVector<PersonInfo> listPeople(const PeopleStore* people, const VoiceprintStore* voiceprints,
                               MeetingStore* store)
{
    // Név-egyesítés kisbetű-függetlenül (a people.json és a lenyomat-DB így kezeli).
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
            out[i].voiceprintCount = voiceprints->printCount(n);
            out[i].hasVoiceprint = out[i].voiceprintCount > 0;
        }

    if (store) {
        const QVector<Meeting> index = store->loadAll();
        for (const Meeting& entry : index) {
            const Meeting m = store->load(entry.id);
            if (m.id.isEmpty()) continue;
            QSet<QString> present;   // egy meeting egy személyt egyszer számol
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
    const QString n = foldForSearch(needle.trimmed());
    if (n.isEmpty()) return all;
    QVector<PersonInfo> prefix, word, inner;
    for (const PersonInfo& p : all) {
        const QString folded = foldForSearch(p.name);
        const int pos = folded.indexOf(n);
        if (pos < 0) continue;
        if (pos == 0) prefix.append(p);
        else if (folded.at(pos - 1).isSpace() || folded.at(pos - 1) == QLatin1Char('-')) word.append(p);
        else inner.append(p);
    }
    return prefix + word + inner;
}

} // namespace tanara
