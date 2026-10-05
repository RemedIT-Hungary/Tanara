#include "tanara/tags/TagSuggester.h"
#include "tanara/embedding/EmbeddingIndex.h"
#include "tanara/tags/MeetingProfiles.h"
#include "tanara/tags/TagService.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace tanara {

TagSuggester::TagSuggester(TagService* tags, MeetingProfiles* profiles, EmbeddingIndex* index)
    : m_tags(tags), m_profiles(profiles), m_index(index)
{
}

QVector<SimilarHit> TagSuggester::fuseRrf(const QVector<SimilarHit>& a, const QVector<SimilarHit>& b, int k)
{
    QHash<QString, SimilarHit> fused;
    QStringList order;
    auto add = [&](const QVector<SimilarHit>& list) {
        for (int rank = 0; rank < list.size(); ++rank) {
            const SimilarHit& h = list.at(rank);
            auto it = fused.find(h.meetingId);
            if (it == fused.end()) {
                SimilarHit f;
                f.meetingId = h.meetingId;
                it = fused.insert(h.meetingId, f);
                order << h.meetingId;
            }
            it->score += 1.0 / double(k + rank + 1);
            // Indoklás: fajtánként az első forrásé (a klasszikus a konkrétabb).
            for (const SuggestionReason& r : h.reasons) {
                const bool have = std::any_of(it->reasons.cbegin(), it->reasons.cend(),
                                              [&](const SuggestionReason& x) { return x.kind == r.kind; });
                if (!have) it->reasons.append(r);
            }
        }
    };
    add(a);
    add(b);
    QVector<SimilarHit> out;
    for (const QString& id : std::as_const(order)) out.append(fused.value(id));
    std::stable_sort(out.begin(), out.end(), [](const SimilarHit& x, const SimilarHit& y) {
        return x.score > y.score;
    });
    return out;
}

QVector<TagSuggestion> TagSuggester::vote(const QString& meetingId, const QVector<SimilarHit>& neighbours) const
{
    if (!m_tags) return {};
    const QStringList applied = meetingId.isEmpty() ? QStringList() : m_tags->tagsOf(meetingId);
    const int total = std::max(1, m_tags->totalMeetings());

    struct Vote {
        double score = 0;
        QVector<QPair<double, QString>> voters;            // (hozzájárulás, meetingId)
        QHash<int, QStringList> reasons;                   // ReasonKind → értékek
    };
    QHash<QString, Vote> votes;
    QHash<QString, SimilarHit> byId;
    for (const SimilarHit& n : neighbours) {
        byId.insert(n.meetingId, n);
        for (const QString& tagId : m_tags->tagsOf(n.meetingId)) {
            if (applied.contains(tagId)) continue;
            if (!meetingId.isEmpty() && m_tags->isRejected(meetingId, tagId)) continue;
            const double share = double(m_tags->meetingCount(tagId)) / double(total);
            const double contrib = n.score / std::log(2.0 + share * 10.0);
            Vote& v = votes[tagId];
            v.score += contrib;
            v.voters.append({ contrib, n.meetingId });
            for (const SuggestionReason& r : n.reasons) {
                QStringList& vals = v.reasons[int(r.kind)];
                for (const QString& x : r.values)
                    if (!vals.contains(x)) vals << x;
            }
        }
    }
    if (votes.isEmpty()) return {};
    double best = 0;
    for (const Vote& v : std::as_const(votes)) best = std::max(best, v.score);

    QVector<TagSuggestion> out;
    for (auto it = votes.constBegin(); it != votes.constEnd(); ++it) {
        if (it->score < kKeepRatio * best) continue;
        const Tag t = m_tags->tag(it.key());
        if (!t.isValid()) continue;
        TagSuggestion s;
        s.tagId = t.id;
        s.name = t.name;
        s.source = SuggestionSource::Similar;
        s.score = best > 0 ? it->score / best : 0.0;
        for (const ReasonKind k : { ReasonKind::Participant, ReasonKind::Terms, ReasonKind::Title }) {
            const QStringList vals = it->reasons.value(int(k));
            if (!vals.isEmpty()) s.reasons.append({ k, vals.mid(0, k == ReasonKind::Title ? 2 : 3) });
        }
        QVector<QPair<double, QString>> voters = it->voters;
        std::sort(voters.begin(), voters.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        for (const auto& v : std::as_const(voters)) {
            if (s.similarMeetings.size() >= 3) break;
            s.similarMeetings.append(m_tags->meetingRef(v.second));
        }
        out.append(s);
    }
    std::sort(out.begin(), out.end(), [](const TagSuggestion& a, const TagSuggestion& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.name.localeAwareCompare(b.name) < 0;
    });
    if (out.size() > kMaxSuggestions) out.resize(kMaxSuggestions);

    return out;
}

QVector<TagSuggestion> TagSuggester::suggest(const QString& meetingId) const
{
    if (!m_profiles || !m_tags) return {};
    QVector<SimilarHit> neighbours = m_profiles->similar(meetingId, 20);
    if (m_index && m_index->has(meetingId)) {
        const QVector<SimilarHit> emb = m_index->similar(meetingId, 20);
        if (!emb.isEmpty()) neighbours = fuseRrf(neighbours, emb);
    }
    return vote(meetingId, neighbours);
}

QVector<TagSuggestion> TagSuggester::suggestForDraft(const QString& title, const QStringList& participants) const
{
    if (!m_profiles || !m_tags) return {};
    return vote(QString(), m_profiles->similarToDraft(title, participants, QString(), 20));
}

QVector<TagSuggestion> TagSuggester::cooccur(const QString& meetingId, const QString& justAddedTagId) const
{
    QVector<TagSuggestion> out;
    if (!m_tags) return out;
    const QStringList with = m_tags->meetingsWith(justAddedTagId);
    // A most címkézett meeting nem számít a mintába.
    QStringList base;
    for (const QString& id : with) if (id != meetingId) base << id;
    const int n = int(base.size());
    if (n < 2) return out;
    const QStringList applied = m_tags->tagsOf(meetingId);
    QHash<QString, QStringList> co;
    for (const QString& mid : std::as_const(base))
        for (const QString& t : m_tags->tagsOf(mid))
            if (t != justAddedTagId) co[t] << mid;
    for (auto it = co.constBegin(); it != co.constEnd(); ++it) {
        const int c = int(it->size());
        if (c < 2 || 2 * c < n) continue;
        if (applied.contains(it.key()) || m_tags->isRejected(meetingId, it.key())) continue;
        const Tag t = m_tags->tag(it.key());
        if (!t.isValid()) continue;
        TagSuggestion s;
        s.tagId = t.id;
        s.name = t.name;
        s.source = SuggestionSource::Cooccur;
        s.baseTagId = justAddedTagId;
        s.score = double(c) / double(n);
        for (const QString& mid : it.value()) {   // legújabb elöl (a meetingsWith sorrendje)
            if (s.similarMeetings.size() >= 3) break;
            s.similarMeetings.append(m_tags->meetingRef(mid));
        }
        out.append(s);
    }
    std::sort(out.begin(), out.end(), [](const TagSuggestion& a, const TagSuggestion& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.name.localeAwareCompare(b.name) < 0;
    });
    if (out.size() > kMaxSuggestions) out.resize(kMaxSuggestions);
    return out;
}

} // namespace tanara
