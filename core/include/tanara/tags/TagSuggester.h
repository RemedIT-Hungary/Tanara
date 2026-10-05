#pragma once
//
// TagSuggester — címkejavaslatok a hasonló megbeszélésekből és az együtt járó címkékből.
//
//  - suggest(): a szomszédok (MeetingProfiles::similar, és ha a meetingnek van beágyazása,
//    EmbeddingIndex::similar, kölcsönös rang-fúzióval — RRF, k = 60) címkéi szavaznak a
//    szomszéd pontszámával. Egy címke szavazata log(2 + globális arány × 10)-zel osztva, hogy a
//    mindenhol ott lévő címkék ne nyerjenek. A meetingen lévő és az elutasított címkék
//    kimaradnak; a legjobb szavazat 15 %-a alatti is; legfeljebb 6.
//  - cooccur(): a frissen felrakott címkével a megbeszéléseinek legalább felén (≥ 2) együtt
//    szereplő címkék.
//  - suggestForDraft(): még nem létező megbeszéléshez (import, felvétel előtt) cím alapján.
//
#include "tanara/tags/TagTypes.h"

namespace tanara {

class TagService;
class MeetingProfiles;
class EmbeddingIndex;

class TagSuggester {
public:
    static constexpr int kMaxSuggestions = 6;
    static constexpr int kRrfK = 60;
    static constexpr double kKeepRatio = 0.15;

    TagSuggester(TagService* tags, MeetingProfiles* profiles, EmbeddingIndex* index = nullptr);

    QVector<TagSuggestion> suggest(const QString& meetingId) const;
    QVector<TagSuggestion> cooccur(const QString& meetingId, const QString& justAddedTagId) const;
    QVector<TagSuggestion> suggestForDraft(const QString& title, const QStringList& participants = {}) const;

    // A szomszédok összefésülése kölcsönös rang-fúzióval (tiszta függvény, tesztelhető).
    static QVector<SimilarHit> fuseRrf(const QVector<SimilarHit>& a, const QVector<SimilarHit>& b, int k = kRrfK);

private:
    QVector<TagSuggestion> vote(const QString& meetingId, const QVector<SimilarHit>& neighbours) const;

    TagService*      m_tags;
    MeetingProfiles* m_profiles;
    EmbeddingIndex*  m_index;
};

} // namespace tanara
