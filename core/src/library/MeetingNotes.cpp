#include "tanara/library/MeetingNotes.h"

#include "tanara/library/TextFold.h"
#include "tanara/stt/ISttProvider.h"

#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace tanara {
namespace meetingnotes {

namespace {

// A figyelő régi mondata (magyar és angol forma), bármilyen app-névvel.
const QRegularExpression& autoCallRe()
{
    static const QRegularExpression re(
        QStringLiteral("^(?:Automatikusan észlelt hívás|Automatically detected call)\\s*:\\s*(\\S.*)$"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

// Hónap- és napnevek (hajtogatva, rövidítve is), névelők, kötőszavak: a címből kimaradnak.
const QSet<QString>& ignoredWords()
{
    static const QSet<QString> words = [] {
        QSet<QString> s;
        for (const char* w : {
                 // hónapok — magyar, angol, rövidítések
                 "januar", "februar", "marcius", "aprilis", "majus", "junius", "julius",
                 "augusztus", "szeptember", "oktober", "november", "december",
                 "jan", "febr", "feb", "marc", "mar", "apr", "maj", "jun", "jul", "aug",
                 "szept", "sep", "sept", "okt", "oct", "nov", "dec",
                 "january", "february", "march", "april", "may", "june", "july", "august",
                 "october",
                 // napok
                 "hetfo", "kedd", "szerda", "csutortok", "pentek", "szombat", "vasarnap",
                 "monday", "tuesday", "wednesday", "thursday", "friday", "saturday", "sunday",
                 "mon", "tue", "wed", "thu", "fri", "sat", "sun",
                 // névelők, kötőszavak
                 "az", "es", "egy", "meg", "vagy", "the", "and", "of", "to", "for", "with",
                 "on", "in", "at", "an", "or" })
            s.insert(QString::fromLatin1(w));
        return s;
    }();
    return words;
}

// Gyenge szavak: gyakoriak a címekben, de egyedül nem tesznek két megbeszélést rokonná
// (az automatikus „Teams-hívás · …” nevek is csak ezekből állnak).
const QSet<QString>& weakWords()
{
    static const QSet<QString> words = [] {
        QSet<QString> s;
        for (const char* w : {
                 "megbeszeles", "meeting", "hivas", "call", "egyeztetes", "felvetel", "recording",
                 "heti", "napi", "havi", "weekly", "daily", "monthly", "sync", "standup",
                 "teams", "zoom", "meet", "google", "slack", "discord", "skype", "webex" })
            s.insert(QString::fromLatin1(w));
        return s;
    }();
    return words;
}

bool sameNote(const QString& a, const QString& b)
{
    return a.simplified() == b.simplified();
}

} // namespace

bool parseAutoCallNote(const QString& note, QString* app)
{
    const QString t = note.trimmed();
    if (t.isEmpty() || t.contains(QLatin1Char('\n')))
        return false;
    const QRegularExpressionMatch mt = autoCallRe().match(t);
    if (!mt.hasMatch())
        return false;
    if (app)
        *app = mt.captured(1).trimmed();
    return true;
}

bool interpretLegacyNote(Meeting& m)
{
    QString app;
    if (!parseAutoCallNote(m.contextNote, &app))
        return false;
    m.contextNote.clear();
    if (m.detectedCallApp.trimmed().isEmpty())
        m.detectedCallApp = app;
    return true;
}

QString sttContextText(const Meeting& m)
{
    const QString note = m.contextNote.trimmed();
    return parseAutoCallNote(note) ? QString() : note;
}

void fillSttContext(SttRequest& req, const Meeting& m)
{
    req.contextGeneral.clear();
    if (!m.title.trimmed().isEmpty())
        req.contextGeneral.insert(QStringLiteral("Megbeszélés"), m.title.trimmed());
    req.context = sttContextText(m);
    QStringList participants;
    for (const Track& t : m.tracks) {
        const QString lbl = t.speakerLabel.trimmed();
        if (t.active && !lbl.isEmpty() && !participants.contains(lbl))
            participants << lbl;
    }
    req.contextTerms = participants;
}

QStringList titleWords(const QString& title)
{
    static const QRegularExpression splitRe(QStringLiteral("[^\\p{L}\\p{N}]+"));
    const QString folded = textfold::fold(textfold::normalize(title));
    QStringList out;
    for (const QString& w : folded.split(splitRe, Qt::SkipEmptyParts)) {
        if (w.size() < 2)
            continue;
        if (std::any_of(w.cbegin(), w.cend(), [](QChar c) { return c.isDigit(); }))
            continue;   // dátum, sorszám, időpont, verziószám
        if (ignoredWords().contains(w) || out.contains(w))
            continue;
        out << w;
    }
    return out;
}

double titleSimilarity(const QStringList& a, const QStringList& b)
{
    if (a.isEmpty() || b.isEmpty())
        return 0.0;
    int common = 0;
    bool strongCommon = false;
    for (const QString& w : a) {
        if (!b.contains(w))
            continue;
        ++common;
        if (!weakWords().contains(w))
            strongCommon = true;
    }
    if (!strongCommon)
        return 0.0;
    const int all = int(a.size() + b.size()) - common;
    return all > 0 ? double(common) / double(all) : 0.0;
}

bool titlesSimilar(const QString& a, const QString& b)
{
    return titleSimilarity(titleWords(a), titleWords(b)) >= kSimilarThreshold;
}

QVector<NoteSuggestion> suggestNotes(const QVector<NoteCandidate>& candidates,
                                     const QString& selfId, const QString& selfTitle,
                                     const QString& currentNote, int limit)
{
    QVector<NoteSuggestion> hits;
    if (limit <= 0)
        return hits;
    const QStringList self = titleWords(selfTitle);
    if (self.isEmpty())
        return hits;
    for (const NoteCandidate& c : candidates) {
        if (c.meetingId == selfId)
            continue;
        const QString note = c.note.trimmed();
        if (note.isEmpty() || parseAutoCallNote(note) || sameNote(note, currentNote))
            continue;
        const double score = titleSimilarity(self, c.words);
        if (score < kSimilarThreshold)
            continue;
        hits.append({c.meetingId, c.title, c.startedAt, note, score});
    }
    // Legújabb elöl (azonos időnél a hasonlóbb).
    std::stable_sort(hits.begin(), hits.end(), [](const NoteSuggestion& x, const NoteSuggestion& y) {
        if (x.startedAt != y.startedAt) return x.startedAt > y.startedAt;
        return x.score > y.score;
    });
    QVector<NoteSuggestion> out;
    for (const NoteSuggestion& h : std::as_const(hits)) {
        const bool dup = std::any_of(out.cbegin(), out.cend(),
                                     [&](const NoteSuggestion& o) { return sameNote(o.note, h.note); });
        if (dup)
            continue;
        out.append(h);
        if (out.size() >= limit)
            break;
    }
    return out;
}

QVector<NoteSuggestion> suggestNotes(const QVector<Meeting>& meetings,
                                     const QString& selfId, const QString& selfTitle,
                                     const QString& currentNote, int limit)
{
    QVector<NoteCandidate> candidates;
    candidates.reserve(meetings.size());
    for (const Meeting& m : meetings)
        candidates.append({m.id, m.title, m.startedAt, m.contextNote, titleWords(m.title)});
    return suggestNotes(candidates, selfId, selfTitle, currentNote, limit);
}

} // namespace meetingnotes
} // namespace tanara
