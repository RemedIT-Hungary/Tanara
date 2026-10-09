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
// Szálkezelés: a TagService a fő szálé, ezért a javaslathoz szükséges címke-adatokból a fő
// szálon egy kis pillanatkép (TagSnapshot) készül; a suggestFrom ezt, valamint a szálbiztos
// MeetingProfiles / EmbeddingIndex olvasóit használja, így háttérszálon is futtatható. A
// TagService-t közvetlenül olvasó metódusok (suggest, cooccur, suggestForDraft) a fő szálé.
//
#include "tanara/tags/TagTypes.h"

#include <QHash>
#include <QSet>

namespace tanara {

class TagService;
class MeetingProfiles;
class EmbeddingIndex;

// A címke-adatok másolata egy meeting javaslatához (érték-típus, szálak között átadható).
struct TagSnapshot {
    QString meetingId;                              // üres: tervezet (még nem létező meeting)
    QHash<QString, QStringList> tagsByMeeting;      // címkézett meetingek → címkéik
    QHash<QString, Tag> tags;                       // id → címke
    QHash<QString, int> counts;                     // id → meetingek száma
    QSet<QString> rejected;                         // a meetingen elutasított címke-azonosítók
    QHash<QString, MeetingRef> refs;                // a címkézett meetingek hivatkozásai
    int totalMeetings = 0;
};

class TagSuggester {
public:
    static constexpr int kMaxSuggestions = 6;
    static constexpr int kRrfK = 60;
    static constexpr double kKeepRatio = 0.15;

    TagSuggester(TagService* tags, MeetingProfiles* profiles, EmbeddingIndex* index = nullptr);

    QVector<TagSuggestion> suggest(const QString& meetingId) const;
    // A pillanatkép elkészítése (fő szál; a meeting számától függően ~ezredmásodpercek).
    static TagSnapshot snapshot(const TagService& tags, const QString& meetingId);
    // A suggest() szálbiztos magja: bármelyik szálról hívható.
    static QVector<TagSuggestion> suggestFrom(const TagSnapshot& snap, const MeetingProfiles* profiles,
                                              const EmbeddingIndex* index = nullptr);
    QVector<TagSuggestion> cooccur(const QString& meetingId, const QString& justAddedTagId) const;
    QVector<TagSuggestion> suggestForDraft(const QString& title, const QStringList& participants = {}) const;

    // A szomszédok összefésülése kölcsönös rang-fúzióval (tiszta függvény, tesztelhető).
    static QVector<SimilarHit> fuseRrf(const QVector<SimilarHit>& a, const QVector<SimilarHit>& b, int k = kRrfK);

private:
    static QVector<TagSuggestion> vote(const TagSnapshot& snap, const QVector<SimilarHit>& neighbours);

    TagService*      m_tags;
    MeetingProfiles* m_profiles;
    EmbeddingIndex*  m_index;
};

} // namespace tanara
