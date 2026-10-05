#pragma once
//
// Címkék — közös érték-típusok (a TagService, a javaslók és a UI közti szerződés).
//
// A címke jelentését nem a neve adja, hanem az, hogy mely megbeszélésekre került rá: a
// javaslatok (hasonló megbeszélések, együtt járó címkék, nyelvi modell) ebből dolgoznak.
// A meetingenkénti hozzárendelés a meeting.json-ban él (Meeting::tagIds), a címkekészlet a
// <metadataDir>/tags.json-ban (TagService).
//
#include <QDateTime>
#include <QMetaType>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara {

struct Tag {
    QString   id;          // QUuid kapcsos zárójelek nélkül; üres = nincs ilyen címke
    QString   name;        // megjelenített név (normalizeTagName)
    QDateTime createdAt;
    bool isValid() const { return !id.isEmpty(); }
};

struct TagUsage {
    Tag       tag;
    int       meetingCount = 0;
    QDateTime firstUsedAt;   // a legkorábbi megbeszélése
    QDateTime lastUsedAt;    // a legutóbbi megbeszélése, ill. a legutóbbi felrakás ideje
};

// Honnan került fel a címke (a TagService naplózza; a viselkedést nem befolyásolja).
enum class TagSource { Manual, Suggestion, Llm, Bulk };
// Melyik javasló adta a javaslatot.
enum class SuggestionSource { Similar, Cooccur, Llm };
// Egy indoklás-sor fajtája („Közös résztvevő” / „Közös kifejezések” / „Hasonló cím”).
enum class ReasonKind { Participant, Terms, Title };

struct SuggestionReason {
    ReasonKind  kind = ReasonKind::Terms;
    QStringList values;
};

struct MeetingRef {
    QString   meetingId;
    QString   title;
    QDateTime startedAt;
    qint64    durationMs = 0;
};

struct TagSuggestion {
    QString tagId;                   // üres, ha isNew
    QString name;
    bool    isNew = false;           // a nyelvi modell által kitalált név, még nincs a készletben
    SuggestionSource source = SuggestionSource::Similar;
    double  score = 0.0;             // 0..1, csak a sorrendhez
    QVector<SuggestionReason> reasons;
    QVector<MeetingRef> similarMeetings;   // „Hasonló megbeszélés” linkek
    QString baseTagId;               // Cooccur: a címke, amely mellé gyakran („<Tag> mellé gyakran”)
};

// Amit a rendszer egy címkéről a használatából megtanult (levezetett, csak olvasható).
struct TagProfile {
    QString tagId;
    int     meetingCount = 0;
    QVector<QPair<QString, int>> topParticipants;   // név, a címke hány megbeszélésén
    QStringList topTerms;                           // legfeljebb 12
    QVector<QPair<QString, int>> cooccurring;       // tagId, hányszor együtt
    QVector<MeetingRef> meetings;                   // legújabb elöl
};

// Egy hasonló megbeszélés (MeetingProfiles::similar / EmbeddingIndex::similar).
struct SimilarHit {
    QString meetingId;
    double  score = 0.0;
    QVector<SuggestionReason> reasons;
};

// A címke-beviteli mező felugró listájának egy sora (TagService::inputRows).
struct TagInputRow {
    enum Kind {
        Recent,         // üres mező: legutóbb használt
        Match,          // gépelés: találat a készletből
        New,            // „+ Új címke: „…””
        NearDuplicate,  // „HASONLÓ MÁR VAN”: a meglévő, nagyon hasonló nevű címke
        ForceNew,       // „Mégis új: „…””
    };
    Kind    kind = Match;
    Tag     tag;               // New / ForceNew: üres id, name = a (normalizált) begépelt név
    int     meetingCount = 0;
    int     matchStart = -1;   // a találat helye a tag.name-ben (kiemeléshez); -1 = nincs
    int     matchLen = 0;
};

} // namespace tanara

Q_DECLARE_METATYPE(tanara::Tag)
Q_DECLARE_METATYPE(tanara::TagUsage)
Q_DECLARE_METATYPE(tanara::TagSuggestion)
Q_DECLARE_METATYPE(QVector<tanara::TagSuggestion>)
Q_DECLARE_METATYPE(tanara::TagProfile)
Q_DECLARE_METATYPE(tanara::SimilarHit)
Q_DECLARE_METATYPE(tanara::TagInputRow)
