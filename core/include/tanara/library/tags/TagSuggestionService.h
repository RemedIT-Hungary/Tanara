#pragma once
//
// TagSuggestionService: the orchestration of tag suggestions for a meeting (split out of AppController, P12c).
//
//  - The similarity-based list (similar meetings' tags, optionally with the embedding level) is made on ONE
//    background thread from a main-thread snapshot (TagSuggester::snapshot / suggestFrom). At most one run per
//    meeting; a request arriving meanwhile gets its result. A generation counter grows whenever the tags,
//    the transcripts or the embedding index change: a result computed from an older generation is dropped
//    and (if still awaited) recomputed.
//  - The co-occurrence and draft suggestions are cheap and run on the main thread (TagSuggester).
//  - The LLM branch goes through the ITagSuggester port (processing::LlmTagSuggestionProvider, registered as
//    ("tag-suggestion", "llm")); the app supplies the session (provider + prompt + cloud-run bookkeeping).
//  - The published list ("pending") is filtered again whenever the meeting's tags or rejections change.
//  - The embedding-provider selection of the settings is applied to the EmbeddingPreparer here.
//
// Threading: everything but the pool's computation runs on the main thread (TagService is main-thread only).
#include "tanara/domain/Tag.h"
#include "tanara/providers/embedding/ITextEmbedder.h"
#include "tanara/providers/tags/ITagSuggester.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QThreadPool>
#include <QVector>

#include <functional>

namespace tanara::library {

class EmbeddingIndex;
class EmbeddingPreparer;
class MeetingProfiles;
class MeetingRepository;
class TagService;

class TagSuggestionService : public QObject {
    Q_OBJECT
public:
    // The live settings the service reads (a function: the settings may change at run time).
    struct Config {
        bool tagSuggestions = true;        // the feature as a whole
        bool llmTagSuggestions = false;    // the automatic LLM pass after a summary
        // The embedding provider selection, resolved by the app (id empty = none / not registered;
        // a model that is empty also means "no provider").
        QString embeddingId;
        QString embeddingBaseUrl;
        QString embeddingModel;
    };

    // One LLM run prepared by the app. The suggester is created with the given parent (the service) and
    // is deleted by the service when the run ends or is cancelled; null = no provider available.
    struct LlmSession {
        providers::ITagSuggester* suggester = nullptr;
        QString model;
        QString systemPrompt;
        // Called when the run ends: true = answered, false = failed (not called when cancelled).
        std::function<void(bool ok)> done;
    };

    struct Deps {
        MeetingRepository*  store = nullptr;
        TagService*         tags = nullptr;
        MeetingProfiles*    profiles = nullptr;
        EmbeddingIndex*     embeddings = nullptr;       // may be null (no embedding level)
        EmbeddingPreparer*  embeddingPreparer = nullptr;
        std::function<Config()> config;
        std::function<LlmSession(const QString& meetingId, QObject* parent)> llm;   // null = no LLM branch
        // The embedder of an embedding provider id (null = unavailable).
        std::function<providers::ITextEmbedder*(const QString& providerId, QObject* parent)> embedder;
        // Test seam: called on the pool thread right before the similarity computation (e.g. to hold it).
        std::function<void()> backgroundHook;
    };

    explicit TagSuggestionService(const Deps& deps, QObject* parent = nullptr);
    ~TagSuggestionService() override;

    // ---- requests (the result arrives through suggestionsReady) ----
    void requestSuggestions(const QString& meetingId);                       // similarity (+ participants)
    void requestCooccur(const QString& meetingId, const QString& justAddedTagId);
    void requestLlm(const QString& meetingId);                               // the summary must exist
    // The last list published for the meeting (empty if none).
    QVector<TagSuggestion> pending(const QString& meetingId) const;
    // For a not-yet-existing meeting (import, before recording): by title; main thread, synchronous.
    QVector<TagSuggestion> draft(const QString& title) const;

    // Drops what is waiting for the meeting and stops its running LLM call; the published list stays.
    // Returns whether anything was running or waiting.
    bool cancel(const QString& meetingId);

    // ---- events the owner forwards ----
    void invalidate();                              // drops the cache, grows the generation
    void republish(const QString& meetingId);       // participants changed: filter + people part again
    void transcriptChanged(const QString& meetingId);   // invalidate + profile + embedding of the meeting
    void summaryReady(const QString& meetingId);        // the automatic LLM pass (per the settings)
    // Applies the settings' embedding provider to the preparer; restart = a settings change (not startup).
    void applyEmbeddingSettings(bool restart);

    // Stops accepting work and waits for the running computation (it reads the profiles / the index).
    void shutdown();

signals:
    void suggestionsReady(QString meetingId, QVector<tanara::TagSuggestion> suggestions);
    void computing(QString meetingId);

private:
    Config config() const;
    void publish(const QString& meetingId, QVector<TagSuggestion> list);
    void schedule(const QString& meetingId);
    void startJob(const QString& meetingId);
    void finishJob(const QString& meetingId, quint64 gen, QVector<TagSuggestion> list);
    void startLlm(const QString& meetingId, const QVector<TagSuggestion>& suggested);
    void abortLlm();
    QString summaryText(const QString& meetingId) const;

    Deps m_d;
    QHash<QString, QVector<TagSuggestion>> m_similarCache;   // meetingId -> the similarity-based list
    QHash<QString, QVector<TagSuggestion>> m_pending;        // meetingId -> the last list published
    QSet<QString> m_requests;                                // waiting for the profiles to be ready
    struct Run {
        bool running = false;
        bool publish = false;    // requestSuggestions is waiting for it
        bool llm = false;        // requestLlm is waiting for it (for the candidate list)
    };
    QHash<QString, Run> m_runs;
    quint64 m_gen = 0;
    QThreadPool m_pool;          // 1 thread
    bool m_shutdown = false;
    QString m_embeddingKey;      // provider|baseUrl|model, to detect a model change

    // The running LLM call (one at a time: the local model works on one thread).
    QPointer<QObject> m_llmProvider;
    QPointer<providers::TagSuggestJob> m_llmJob;
    QString m_llmMeetingId;
};

} // namespace tanara::library
