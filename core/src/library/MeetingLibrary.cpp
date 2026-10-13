#include "tanara/library/MeetingLibrary.h"
#include "tanara/jobs/MeetingJobTracker.h"
#include "tanara/store/MeetingStore.h"
#include "tanara/tags/TagService.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <algorithm>

namespace tanara {

// A meeting's transcript text for search: the utterances' text joined with '\n' (NFC), its folded
// (accent/case-insensitive) counterpart, and per utterance the start position + timestamp + raw speaker
// label (to map a hit back to an utterance).
struct MeetingLibrary::TextDoc {
    qint64  mtimeMs = -1;
    qint64  size = -1;
    QString text;
    QString folded;
    QVector<int>    segStart;
    QVector<qint64> segMs;
    QStringList     segSpeaker;
};

struct MeetingLibrary::Impl {
    MeetingStore*      store = nullptr;
    MeetingJobTracker* tracker = nullptr;
    TagService*        tags = nullptr;
    bool               loaded = false;
    QVector<Meeting>   meetings;            // descending by startedAt
    QHash<QString, int> index;              // id -> position in meetings
    QHash<QString, QString> foldedTitle;    // id -> folded title
    QHash<QString, QStringList> titleWords; // id -> the title's comparable words (for suggestions)
    QHash<QString, TextDoc> texts;          // id -> transcript text (lazy)
    QTimer* warmTimer = nullptr;

    void reindex() {
        index.clear();
        for (int i = 0; i < meetings.size(); ++i) index.insert(meetings.at(i).id, i);
    }
    static QString segmentsPath(const Meeting& m) {
        return QDir(m.folder).filePath(QStringLiteral("transcript.segments.json"));
    }
};

MeetingLibrary::MeetingLibrary(MeetingStore* store, MeetingJobTracker* tracker, QObject* parent)
    : QObject(parent), d(new Impl)
{
    d->store = store;
    d->tracker = tracker;
    if (store) {
        connect(store, &MeetingStore::meetingUpdated, this, [this](const QString& id) {
            const bool known = !d->loaded || d->index.contains(id);
            reloadMeeting(id);
            if (known) emit meetingChanged(id); else emit meetingAdded(id);
            emit pendingItemsChanged();
        });
        connect(store, &MeetingStore::meetingAdded, this, [this](const QString& id) {
            // On createMeeting the store first emits meetingUpdated (saveMeeting): if we already picked it up
            // from there, there is nothing to do here.
            if (d->loaded && d->index.contains(id)) return;
            reloadMeeting(id);
            emit meetingAdded(id);
            emit pendingItemsChanged();
        });
        connect(store, &MeetingStore::meetingRemoved, this, [this](const QString& id) {
            if (d->loaded) {
                const int i = d->index.value(id, -1);
                if (i >= 0) { d->meetings.removeAt(i); d->reindex(); }
            }
            d->texts.remove(id);
            d->foldedTitle.remove(id);
            d->titleWords.remove(id);
            emit meetingRemoved(id);
            emit pendingItemsChanged();
        });
    }
    if (tracker) {
        connect(tracker, &MeetingJobTracker::stateChanged, this, [this](const QString& id) {
            emit meetingChanged(id);
            emit pendingItemsChanged();
        });
    }
}

MeetingLibrary::~MeetingLibrary()
{
    delete d;
}

void MeetingLibrary::ensureLoaded() const
{
    if (d->loaded || !d->store) return;
    d->meetings.clear();
    const QVector<Meeting> rows = d->store->loadAll();   // from the index, newest first
    d->meetings.reserve(rows.size());
    for (const Meeting& row : rows) {
        // The full meeting (speakerMap, tracks) from disk; if meeting.json is unreadable, the index row will
        // do (title, date, flags).
        Meeting full = d->store->load(row.id);
        if (full.id.isEmpty()) full = row;
        if (full.folder.isEmpty()) full.folder = row.folder;
        d->meetings.append(full);
    }
    std::stable_sort(d->meetings.begin(), d->meetings.end(),
                     [](const Meeting& a, const Meeting& b) { return a.startedAt > b.startedAt; });
    d->reindex();
    d->foldedTitle.clear();
    d->titleWords.clear();
    d->loaded = true;
}

void MeetingLibrary::reloadMeeting(const QString& id)
{
    if (!d->loaded || !d->store) {
        d->texts.remove(id);
        return;
    }
    const Meeting full = d->store->load(id);
    if (full.id.isEmpty()) return;
    d->foldedTitle.remove(id);
    d->titleWords.remove(id);

    // The transcript text becomes invalid only if the file really changed (a rename or speaker
    // assignment does not make it reread the whole transcript).
    const auto tit = d->texts.constFind(id);
    if (tit != d->texts.constEnd()) {
        const QFileInfo fi(Impl::segmentsPath(full));
        const qint64 mt = fi.exists() ? fi.lastModified().toMSecsSinceEpoch() : -1;
        const qint64 sz = fi.exists() ? fi.size() : -1;
        if (mt != tit->mtimeMs || sz != tit->size)
            d->texts.remove(id);
    }

    const int i = d->index.value(id, -1);
    if (i >= 0) {
        const bool sameOrder = d->meetings.at(i).startedAt == full.startedAt;
        d->meetings[i] = full;
        if (sameOrder) return;
        d->meetings.removeAt(i);
    }
    // Sorted insertion (newest first).
    const auto pos = std::lower_bound(d->meetings.begin(), d->meetings.end(), full,
        [](const Meeting& a, const Meeting& b) { return a.startedAt > b.startedAt; });
    d->meetings.insert(pos, full);
    d->reindex();
}

void MeetingLibrary::invalidate(const QString& meetingId)
{
    if (meetingId.isEmpty()) {
        d->loaded = false;
        d->meetings.clear();
        d->index.clear();
        d->texts.clear();
        d->foldedTitle.clear();
        d->titleWords.clear();
        emit reset();
        emit pendingItemsChanged();
        return;
    }
    d->texts.remove(meetingId);
    reloadMeeting(meetingId);
    emit meetingChanged(meetingId);
}

const MeetingLibrary::TextDoc* MeetingLibrary::textDoc(const Meeting& m) const
{
    const auto it = d->texts.constFind(m.id);
    if (it != d->texts.constEnd())
        return it->segStart.isEmpty() ? nullptr : &*it;

    TextDoc doc;
    if (!m.folder.isEmpty()) {
        const QString path = Impl::segmentsPath(m);
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) {
            const QFileInfo fi(path);
            doc.mtimeMs = fi.lastModified().toMSecsSinceEpoch();
            doc.size = fi.size();
            const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
            doc.segStart.reserve(arr.size());
            doc.segMs.reserve(arr.size());
            for (const QJsonValue& v : arr) {
                const QJsonObject o = v.toObject();
                const QString t = textfold::normalize(o.value(QStringLiteral("text")).toString()).trimmed();
                if (t.isEmpty()) continue;
                if (!doc.text.isEmpty()) doc.text += QLatin1Char('\n');
                doc.segStart.append(int(doc.text.size()));
                doc.segMs.append(qint64(o.value(QStringLiteral("startMs")).toDouble()));
                doc.segSpeaker.append(o.value(QStringLiteral("speaker")).toString());
                doc.text += t;
            }
            doc.folded = textfold::fold(doc.text);
        }
    }
    const auto ins = d->texts.insert(m.id, doc);
    return ins->segStart.isEmpty() ? nullptr : &*ins;
}

QStringList MeetingLibrary::participantsOf(const Meeting& m)
{
    QStringList out;
    auto add = [&out](const QString& raw) {
        const QString n = raw.trimmed();
        if (!n.isEmpty() && !out.contains(n)) out << n;
    };
    // The own (microphone) track's fixed speaker: the recorder took part too. Generic labels ("Microphone 2",
    // "System") are not people.
    for (const Track& t : m.tracks) {
        if (t.kind != TrackKind::Mic || !t.active) continue;
        if (t.speakerLabel.startsWith(QStringLiteral("Mikrofon"))) continue;
        add(t.speakerLabel);
    }
    for (auto it = m.speakerMap.constBegin(); it != m.speakerMap.constEnd(); ++it)
        add(it.value());
    return out;
}

DateSection MeetingLibrary::sectionFor(const QDateTime& startedAt, const QDate& today)
{
    const QDate d = startedAt.date();
    if (!d.isValid()) return DateSection::Earlier;
    if (d >= today) return DateSection::Today;                  // (a future clock skew goes here too)
    if (d == today.addDays(-1)) return DateSection::Yesterday;
    const QDate monday = today.addDays(-(today.dayOfWeek() - 1));
    if (d >= monday) return DateSection::ThisWeek;
    return DateSection::Earlier;
}

QString MeetingLibrary::sectionKey(DateSection s)
{
    switch (s) {
    case DateSection::Today:     return QStringLiteral("today");
    case DateSection::Yesterday: return QStringLiteral("yesterday");
    case DateSection::ThisWeek:  return QStringLiteral("thisWeek");
    case DateSection::Earlier:   return QStringLiteral("earlier");
    }
    return QStringLiteral("earlier");
}

QString MeetingLibrary::sectionTitle(DateSection s)
{
    switch (s) {
    case DateSection::Today:     return tr("Ma");
    case DateSection::Yesterday: return tr("Tegnap");
    case DateSection::ThisWeek:  return tr("Ezen a héten");
    case DateSection::Earlier:   return tr("Korábban");
    }
    return tr("Korábban");
}

LibraryEntry MeetingLibrary::makeEntry(const Meeting& m, const QDate& today) const
{
    LibraryEntry e;
    e.id = m.id;
    e.title = m.title;
    e.startedAt = m.startedAt;
    e.durationMs = m.durationMs;
    e.hasTranscript = m.hasTranscript;
    e.hasSummary = m.hasSummary;
    e.section = sectionFor(m.startedAt, today);
    e.participants = participantsOf(m);
    if (d->tags) {
        for (const QString& id : m.tagIds) {
            const Tag t = d->tags->tag(id);
            if (!t.isValid()) continue;
            e.tagIds << t.id;
            e.tagNames << t.name;
        }
    }
    if (d->tracker) {
        e.state = d->tracker->state(m);
    } else {
        e.state.meetingId = m.id;
        e.state.transcriptState = m.hasTranscript ? StepState::Done : StepState::None;
        e.state.summaryState    = m.hasSummary ? StepState::Done : StepState::None;
        e.state.identifyState   = (m.hasTranscript && !m.speakerMap.isEmpty()) ? StepState::Done
                                                                               : StepState::None;
    }
    return e;
}

LibraryResult MeetingLibrary::query(const LibraryQuery& q, const QDateTime& now) const
{
    ensureLoaded();
    LibraryResult res;
    res.totalMeetings = int(d->meetings.size());
    const QDate today = now.date();
    const QString fq = textfold::foldQuery(q.text);
    QStringList wantPeople;
    for (const QString& p : q.people) {
        const QString f = textfold::foldQuery(p);
        if (!f.isEmpty()) wantPeople << f;
    }

    // Tag filter: only existing tags count.
    QStringList wantTags;
    for (const QString& id : q.tags)
        if (!id.isEmpty() && (!d->tags || d->tags->tag(id).isValid())) wantTags << id;
    const bool tagFilter = !wantTags.isEmpty() || q.untagged;
    auto tagsOk = [&](const Meeting& m) {
        if (!tagFilter) return true;
        QStringList have;
        for (const QString& id : m.tagIds)
            if (!d->tags || d->tags->tag(id).isValid()) have << id;
        if (q.untagged && have.isEmpty()) return true;
        if (wantTags.isEmpty()) return false;
        if (q.tagsAll) {
            for (const QString& w : wantTags) if (!have.contains(w)) return false;
            return true;
        }
        for (const QString& w : wantTags) if (have.contains(w)) return true;
        return false;
    };

    for (const Meeting& m : std::as_const(d->meetings)) {
        if (q.noTranscript && m.hasTranscript) continue;
        if (q.noSummary && m.hasSummary) continue;
        if (!tagsOk(m)) continue;
        if (!wantPeople.isEmpty()) {
            QStringList have;
            for (const QString& p : participantsOf(m)) have << textfold::foldQuery(p);
            bool all = true;
            for (const QString& w : wantPeople)
                if (!have.contains(w)) { all = false; break; }
            if (!all) continue;
        }

        if (fq.isEmpty()) {
            res.entries.append(makeEntry(m, today));
            continue;
        }

        // Title match (we cache the folded title per meeting).
        const QString nfcTitle = textfold::normalize(m.title);
        auto ft = d->foldedTitle.constFind(m.id);
        if (ft == d->foldedTitle.constEnd())
            ft = d->foldedTitle.insert(m.id, textfold::fold(nfcTitle));
        const textfold::Range titleHit = textfold::find(*ft, fq);

        // Transcript match.
        textfold::Range textHit;
        const TextDoc* doc = m.hasTranscript ? textDoc(m) : nullptr;
        if (doc) textHit = textfold::find(doc->folded, fq);

        // Tag match (accent- and case-insensitively in the tag's name).
        QString tagHit;
        if (d->tags) {
            for (const QString& id : m.tagIds) {
                const Tag t = d->tags->tag(id);
                if (t.isValid() && textfold::find(textfold::fold(textfold::normalize(t.name)), fq).isValid()) {
                    tagHit = t.name;
                    break;
                }
            }
        }
        if (!titleHit.isValid() && !textHit.isValid() && tagHit.isEmpty()) continue;

        LibraryEntry e = makeEntry(m, today);
        e.tagMatch = !tagHit.isEmpty();
        e.tagMatchName = tagHit;
        e.title = nfcTitle;               // the titleMatch positions are valid for this (NFC) form
        e.titleMatch = titleHit;
        if (textHit.isValid()) {
            e.snippet = textfold::snippet(doc->text, textHit, &e.snippetMatch);
            // Which utterance the match falls into (the last one that does not start later).
            const auto up = std::upper_bound(doc->segStart.constBegin(), doc->segStart.constEnd(),
                                             textHit.start);
            const int seg = int(up - doc->segStart.constBegin()) - 1;
            if (seg >= 0) {
                e.snippetMs = doc->segMs.at(seg);
                const QString raw = doc->segSpeaker.at(seg);
                e.snippetSpeaker = m.speakerMap.value(raw, raw);
            }
        }
        res.entries.append(e);
    }
    return res;
}

LibraryEntry MeetingLibrary::entry(const QString& meetingId, const QDateTime& now) const
{
    ensureLoaded();
    const int i = d->index.value(meetingId, -1);
    if (i < 0) return {};
    return makeEntry(d->meetings.at(i), now.date());
}

Meeting MeetingLibrary::meeting(const QString& meetingId) const
{
    ensureLoaded();
    const int i = d->index.value(meetingId, -1);
    return i < 0 ? Meeting{} : d->meetings.at(i);
}

int MeetingLibrary::meetingCount() const
{
    ensureLoaded();
    return int(d->meetings.size());
}

QVector<meetingnotes::NoteSuggestion> MeetingLibrary::noteSuggestions(const QString& meetingId,
                                                                      const QString& currentNote,
                                                                      int limit) const
{
    ensureLoaded();
    const int self = d->index.value(meetingId, -1);
    if (self < 0)
        return {};
    // Only meetings with a note are candidates; the title words are cached per meeting.
    QVector<meetingnotes::NoteCandidate> candidates;
    for (const Meeting& m : std::as_const(d->meetings)) {
        if (m.id == meetingId || m.contextNote.trimmed().isEmpty())
            continue;
        auto w = d->titleWords.constFind(m.id);
        if (w == d->titleWords.constEnd())
            w = d->titleWords.insert(m.id, meetingnotes::titleWords(m.title));
        candidates.append({m.id, m.title, m.startedAt, m.contextNote, *w});
    }
    return meetingnotes::suggestNotes(candidates, meetingId, d->meetings.at(self).title,
                                      currentNote, limit);
}

QVector<PersonPresence> MeetingLibrary::people() const
{
    ensureLoaded();
    QHash<QString, int> counts;
    for (const Meeting& m : std::as_const(d->meetings))
        for (const QString& p : participantsOf(m)) counts[p]++;
    QVector<PersonPresence> out;
    out.reserve(counts.size());
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it)
        out.append({ it.key(), it.value() });
    std::sort(out.begin(), out.end(), [](const PersonPresence& a, const PersonPresence& b) {
        if (a.meetingCount != b.meetingCount) return a.meetingCount > b.meetingCount;
        return a.name.localeAwareCompare(b.name) < 0;
    });
    return out;
}

void MeetingLibrary::setTagService(TagService* tags)
{
    if (d->tags == tags) return;
    if (d->tags) d->tags->disconnect(this);
    d->tags = tags;
    if (tags) connect(tags, &TagService::tagsChanged, this, &MeetingLibrary::tagsChanged);
    emit tagsChanged();
}

QVector<TagUsage> MeetingLibrary::tagOptions() const
{
    ensureLoaded();
    QVector<TagUsage> out;
    if (!d->tags) return out;
    QHash<QString, TagUsage> byId;
    for (const TagUsage& u : d->tags->all(TagService::Sort::Alpha)) {
        TagUsage x;
        x.tag = u.tag;
        byId.insert(u.tag.id, x);
        out.append(x);
    }
    for (const Meeting& m : std::as_const(d->meetings))
        for (const QString& id : m.tagIds) {
            auto it = byId.find(id);
            if (it == byId.end()) continue;
            it->meetingCount++;
            if (!it->firstUsedAt.isValid() || m.startedAt < it->firstUsedAt) it->firstUsedAt = m.startedAt;
            if (!it->lastUsedAt.isValid() || m.startedAt > it->lastUsedAt) it->lastUsedAt = m.startedAt;
        }
    for (TagUsage& u : out) u = byId.value(u.tag.id);
    std::stable_sort(out.begin(), out.end(), [](const TagUsage& a, const TagUsage& b) {
        return a.meetingCount > b.meetingCount;   // on an equal count the alphabetical order stays
    });
    return out;
}

int MeetingLibrary::untaggedCount() const
{
    ensureLoaded();
    int n = 0;
    for (const Meeting& m : std::as_const(d->meetings)) {
        bool any = false;
        for (const QString& id : m.tagIds)
            if (!d->tags || d->tags->tag(id).isValid()) { any = true; break; }
        if (!any) ++n;
    }
    return n;
}

QVector<PendingItem> MeetingLibrary::pendingItems(int limit) const
{
    ensureLoaded();
    QVector<PendingItem> out;
    for (const Meeting& m : std::as_const(d->meetings)) {
        if (limit > 0 && out.size() >= limit) break;
        PendingItem it;
        it.meetingId = m.id;
        it.title = m.title;
        it.startedAt = m.startedAt;
        it.durationMs = m.durationMs;
        if (m.hasTranscript) {
            // A finished transcript: "waiting for you" only if its summary is stale (and not refreshing right
            // now). Staleness comes from the tracker's probe (the speaker-editor layer).
            if (!d->tracker || !m.hasSummary) continue;
            const MeetingProcessingState st = d->tracker->state(m);
            if (!st.summaryStale) continue;
            it.kind = PendingKind::StaleSummary;
            it.correctedSpeakers = st.staleCorrectedSpeakers;
            out.append(it);
            continue;
        }
        if (d->tracker) {
            const MeetingProcessingState st = d->tracker->state(m);
            if (st.transcriptState == StepState::Running) continue;   // we are already working on it
            if (st.transcriptState == StepState::Failed) {
                it.kind = PendingKind::TranscriptionFailed;
                it.error = st.transcriptError;
            }
        }
        out.append(it);
    }
    return out;
}

bool MeetingLibrary::isWarm() const
{
    ensureLoaded();
    for (const Meeting& m : std::as_const(d->meetings))
        if (m.hasTranscript && !d->texts.contains(m.id)) return false;
    return true;
}

void MeetingLibrary::warmUp()
{
    ensureLoaded();
    if (d->warmTimer) return;   // already running
    d->warmTimer = new QTimer(this);
    d->warmTimer->setInterval(0);
    connect(d->warmTimer, &QTimer::timeout, this, [this]() {
        // ~8 ms of work per slice so the UI thread can breathe meanwhile.
        QElapsedTimer clock;
        clock.start();
        bool pending = false;
        for (const Meeting& m : std::as_const(d->meetings)) {
            if (!m.hasTranscript || d->texts.contains(m.id)) continue;
            if (clock.elapsed() >= 8) { pending = true; break; }
            textDoc(m);
        }
        if (pending) return;
        d->warmTimer->stop();
        d->warmTimer->deleteLater();
        d->warmTimer = nullptr;
        emit warmedUp();
    });
    d->warmTimer->start();
}

} // namespace tanara
