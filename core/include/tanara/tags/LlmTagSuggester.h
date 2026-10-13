#pragma once
//
// LlmTagSuggester: tag suggestions from the language model, after the summary is done.
//
// One chat call with the "tags" prompt (PromptLibrary): the user message is the summary text +
// a numbered list of candidate tags (the similarity-based suggestions and the 20 most frequent
// tags, each with a one-line profile: typical participants, phrases, two sample titles). The
// model returns PLAIN LINES (not JSON; json_schema is unreliable locally):
//   pick: 3, 7
//   new: Name1; Name2
// Parsing is lenient (case, bullets, quotes, "none"). pick gives existing tags (source = Llm), new
// at most 2 new name ideas (isNew); if a new name is a nearDuplicate of an existing one, it
// suggests the existing one instead. Thinking is off (per ProviderConfig::reasoning, as for the summary).
#include "tanara/tags/TagTypes.h"

#include <QObject>
#include <QPointer>

namespace tanara {

class ILlmProvider;
class LlmJob;
class TagService;

struct LlmTagCandidate {
    QString tagId;
    QString name;
    QString profileLine;
};

class LlmTagSuggester : public QObject {
    Q_OBJECT
public:
    static constexpr int kTopUsed = 20;
    static constexpr int kMaxNew = 2;
    static constexpr int kSummaryChars = 8000;

    explicit LlmTagSuggester(TagService* tags, QObject* parent = nullptr);
    ~LlmTagSuggester() override;

    // ---- pure helpers ----
    // The candidates: first the suggestions (existing tags), then the most frequent, without repeats.
    QVector<LlmTagCandidate> candidates(const QVector<TagSuggestion>& suggested, int topUsed = kTopUsed) const;
    static QString buildUserMessage(const QString& summary, const QVector<LlmTagCandidate>& candidates);
    struct Parsed {
        QVector<int> picks;      // 1-based numbers, in the order of the answer
        QStringList  newNames;
    };
    static Parsed parse(const QString& reply);
    // The parsed answer -> suggestions (tags already on the meeting / rejected are dropped).
    QVector<TagSuggestion> toSuggestions(const Parsed& parsed, const QVector<LlmTagCandidate>& candidates,
                                         const QString& meetingId) const;

    // One run: the provider's chat call with the system prompt. finished / failed signal; the provider
    // belongs to the caller (it must live during the run).
    void start(ILlmProvider* provider, const QString& model, const QString& systemPrompt,
               const QString& meetingId, const QString& summary,
               const QVector<TagSuggestion>& suggested);
    void cancel();
    bool isRunning() const { return !m_job.isNull(); }

signals:
    void finished(QString meetingId, QVector<tanara::TagSuggestion> suggestions);
    void failed(QString meetingId, QString error);

private:
    TagService* m_tags;
    QPointer<LlmJob> m_job;
};

} // namespace tanara
