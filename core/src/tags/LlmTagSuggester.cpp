#include "tanara/tags/LlmTagSuggester.h"
#include "tanara/llm/ILlmProvider.h"
#include "tanara/tags/TagNames.h"
#include "tanara/tags/TagService.h"

#include <QRegularExpression>

namespace tanara {

LlmTagSuggester::LlmTagSuggester(TagService* tags, QObject* parent)
    : QObject(parent), m_tags(tags)
{
}

LlmTagSuggester::~LlmTagSuggester()
{
    cancel();
}

QVector<LlmTagCandidate> LlmTagSuggester::candidates(const QVector<TagSuggestion>& suggested, int topUsed) const
{
    QVector<LlmTagCandidate> out;
    if (!m_tags) return out;
    QStringList seen;
    auto add = [&](const QString& id) {
        if (id.isEmpty() || seen.contains(id)) return;
        const Tag t = m_tags->tag(id);
        if (!t.isValid()) return;
        seen << id;
        out.append({ t.id, t.name, m_tags->profileLine(t.id) });
    };
    for (const TagSuggestion& s : suggested) if (!s.isNew) add(s.tagId);
    int n = 0;
    for (const TagUsage& u : m_tags->all(TagService::Sort::MostUsed)) {
        if (n >= topUsed) break;
        if (u.meetingCount <= 0) continue;
        add(u.tag.id);
        ++n;
    }
    return out;
}

QString LlmTagSuggester::buildUserMessage(const QString& summary, const QVector<LlmTagCandidate>& candidates)
{
    QString msg = QStringLiteral("MEETING SUMMARY\n");
    msg += summary.trimmed().left(kSummaryChars);
    msg += QStringLiteral("\n\nCANDIDATE TAGS\n");
    if (candidates.isEmpty()) {
        msg += QStringLiteral("(none yet)\n");
    } else {
        for (int i = 0; i < candidates.size(); ++i) {
            msg += QStringLiteral("%1. %2").arg(i + 1).arg(candidates.at(i).name);
            if (!candidates.at(i).profileLine.isEmpty())
                msg += QStringLiteral(" — %1").arg(candidates.at(i).profileLine);
            msg += QLatin1Char('\n');
        }
    }
    msg += QStringLiteral("\nAnswer with exactly two lines:\npick: <numbers of the fitting candidate tags, comma-separated, or none>\n"
                          "new: <at most 2 new tag names separated by ;, or none>\n");
    return msg;
}

LlmTagSuggester::Parsed LlmTagSuggester::parse(const QString& reply)
{
    Parsed p;
    static const QRegularExpression pickRe(
        QStringLiteral("^[\\s*_>#`\\-•]*(?:pick|picks|picked|choose|chosen|selected|existing)\\s*[*_`]*\\s*[:=\\-–]\\s*(.*)$"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression newRe(
        QStringLiteral("^[\\s*_>#`\\-•]*(?:new|new tags|new names|new tag)\\s*[*_`]*\\s*[:=\\-–]\\s*(.*)$"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression numRe(QStringLiteral("\\d+"));
    static const QRegularExpression noneRe(
        QStringLiteral("^\\s*(?:none|nothing|n/?a|no|-|—|nincs|semmi)?\\s*[.!]?\\s*$"),
        QRegularExpression::CaseInsensitiveOption);

    for (const QString& rawLine : reply.split(QLatin1Char('\n'))) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty()) continue;
        QRegularExpressionMatch m = pickRe.match(line);
        if (m.hasMatch()) {
            auto it = numRe.globalMatch(m.captured(1));
            while (it.hasNext()) {
                const int n = it.next().captured(0).toInt();
                if (n > 0 && !p.picks.contains(n)) p.picks << n;
            }
            continue;
        }
        m = newRe.match(line);
        if (m.hasMatch()) {
            const QString rest = m.captured(1).trimmed();
            if (noneRe.match(rest).hasMatch()) continue;
            const QStringList parts = rest.contains(QLatin1Char(';'))
                ? rest.split(QLatin1Char(';')) : rest.split(QLatin1Char(','));
            for (QString name : parts) {
                name = name.trimmed();
                // Idézőjelek, csillagok, vezető „#”, záró írásjel le.
                static const QRegularExpression strip(QStringLiteral("^[\"'„“”«»*`#\\s]+|[\"'„“”«»*`\\s.!]+$"));
                name.replace(strip, QString());
                name = normalizeTagName(name);
                if (name.isEmpty() || noneRe.match(name).hasMatch() || name.size() > 40) continue;
                if (std::any_of(p.newNames.cbegin(), p.newNames.cend(),
                                [&](const QString& x) { return tagKey(x) == tagKey(name); }))
                    continue;
                p.newNames << name;
            }
        }
    }
    return p;
}

QVector<TagSuggestion> LlmTagSuggester::toSuggestions(const Parsed& parsed,
                                                      const QVector<LlmTagCandidate>& candidates,
                                                      const QString& meetingId) const
{
    QVector<TagSuggestion> out;
    const QStringList applied = m_tags ? m_tags->tagsOf(meetingId) : QStringList();
    auto addExisting = [&](const QString& id, const QString& name) {
        if (id.isEmpty() || applied.contains(id)) return;
        if (m_tags && m_tags->isRejected(meetingId, id)) return;
        if (std::any_of(out.cbegin(), out.cend(), [&](const TagSuggestion& s) { return s.tagId == id; })) return;
        TagSuggestion s;
        s.tagId = id;
        s.name = name;
        s.source = SuggestionSource::Llm;
        s.score = 1.0 - 0.05 * out.size();
        out.append(s);
    };
    for (int n : parsed.picks)
        if (n >= 1 && n <= candidates.size())
            addExisting(candidates.at(n - 1).tagId, candidates.at(n - 1).name);

    int added = 0;
    for (const QString& name : parsed.newNames) {
        if (added >= kMaxNew) break;
        // Létező (vagy nagyon hasonló nevű) címke → azt javasoljuk új helyett.
        Tag existing = m_tags ? m_tags->byName(name) : Tag{};
        if (!existing.isValid() && m_tags) {
            const QVector<Tag> near = m_tags->similarNames(name, 1);
            if (!near.isEmpty()) existing = near.first();
        }
        if (existing.isValid()) { addExisting(existing.id, existing.name); continue; }
        if (m_tags && m_tags->isRejected(meetingId, name)) continue;
        TagSuggestion s;
        s.name = name;
        s.isNew = true;
        s.source = SuggestionSource::Llm;
        s.score = 0.5;
        out.append(s);
        ++added;
    }
    return out;
}

void LlmTagSuggester::start(ILlmProvider* provider, const QString& model, const QString& systemPrompt,
                            const QString& meetingId, const QString& summary,
                            const QVector<TagSuggestion>& suggested)
{
    cancel();
    if (!provider) { emit failed(meetingId, tr("Nincs nyelvi modell beállítva.")); return; }
    const QVector<LlmTagCandidate> cands = candidates(suggested);
    LlmRequest req;
    req.model = model;
    req.temperature = 0.2;
    req.maxTokens = 400;
    req.messages = { { QStringLiteral("system"), systemPrompt },
                     { QStringLiteral("user"), buildUserMessage(summary, cands) } };
    LlmJob* job = provider->chat(req);
    m_job = job;
    connect(job, &LlmJob::finished, this, [this, job, cands, meetingId](const QString& text) {
        job->deleteLater();
        m_job = nullptr;
        emit finished(meetingId, toSuggestions(parse(text), cands, meetingId));
    });
    connect(job, &LlmJob::failed, this, [this, job, meetingId](const QString& error) {
        job->deleteLater();
        m_job = nullptr;
        emit failed(meetingId, error);
    });
}

void LlmTagSuggester::cancel()
{
    if (!m_job) return;
    m_job->disconnect(this);
    m_job->cancel();
    m_job->deleteLater();
    m_job = nullptr;
}

} // namespace tanara
