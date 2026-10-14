// library::TagSuggestionService on its own (without AppController): the similarity branch on the background
// thread (cache, generation counter: a stale result is dropped and recomputed), the LLM branch through the
// ITagSuggester port over a scripted chat provider (answer, empty answer, failure, no provider, cancel,
// supersede), cancel, the settings switch and the embedding-settings application.
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <atomic>

#include "tanara/library/index/EmbeddingIndex.h"
#include "tanara/library/index/EmbeddingPreparer.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/summary/SummaryRepository.h"
#include "tanara/library/tags/MeetingProfiles.h"
#include "tanara/library/tags/TagService.h"
#include "tanara/library/tags/TagSuggestionService.h"
#include "tanara/processing/analysis/LlmTagSuggestionProvider.h"
#include "tanara/providers/chat/IChatProvider.h"
#include "tags_fixture.h"

using namespace tanara;

namespace {

class ScriptedJob : public providers::LlmJob {
    Q_OBJECT
public:
    explicit ScriptedJob(QObject* parent) : providers::LlmJob(parent) {}
    void cancel() override { cancelled = true; }
    bool cancelled = false;
};

// A chat provider that answers with a scripted reply, fails, or never answers (hang).
class ScriptedChat : public QObject, public providers::IChatProvider {
    Q_OBJECT
public:
    enum class Mode { Reply, Fail, Hang };
    Mode mode = Mode::Reply;
    QString reply = QStringLiteral("pick: 1\nnew: none");
    int calls = 0;
    QString name() const override { return QStringLiteral("scripted"); }
    bool supportsStreaming() const override { return false; }
    providers::LlmJob* chat(const providers::LlmRequest&) override
    {
        ++calls;
        auto* job = new ScriptedJob(this);
        if (mode == Mode::Reply)
            QTimer::singleShot(0, job, [job, r = reply]() { emit job->finished(r); });
        else if (mode == Mode::Fail)
            QTimer::singleShot(0, job, [job]() { emit job->failed(QStringLiteral("boom")); });
        return job;
    }
};

bool waitFor(const std::function<bool()>& cond, int ms = 8000)
{
    QDeadlineTimer dl(ms);
    while (!cond() && !dl.hasExpired()) QTest::qWait(10);
    return cond();
}

QStringList names(const QVector<TagSuggestion>& list)
{
    QStringList out;
    for (const TagSuggestion& t : list) out << t.name;
    return out;
}

} // namespace

class TestTagSuggestionService : public QObject {
    Q_OBJECT
    QTemporaryDir m_tmp;
    int m_n = 0;
    std::unique_ptr<library::MeetingRepository> m_store;
    std::unique_ptr<library::TagService> m_tags;
    std::unique_ptr<library::MeetingProfiles> m_profiles;
    std::unique_ptr<library::EmbeddingIndex> m_index;
    std::unique_ptr<library::EmbeddingPreparer> m_prep;
    std::unique_ptr<library::TagSuggestionService> m_svc;
    tagsfixture::Library m_lib;
    ScriptedChat m_chat;
    library::TagSuggestionService::Config m_cfg;
    bool m_hasProvider = true;
    int m_llmDone = 0;
    bool m_llmOk = false;
    std::atomic<int> m_bgCalls{0};
    QSemaphore m_entered, m_proceed;
    bool m_blockFirst = false;
    QString m_embedderId;

    void writeSummary(const QString& meetingId)
    {
        const Meeting m = m_store->load(meetingId);
        QVERIFY(library::SummaryRepository::writeMarkdown(m.folder, QStringLiteral("## Összefoglaló\nA MuseumPlus oktatás.")));
    }

    void makeService()
    {
        library::TagSuggestionService::Deps d;
        d.store = m_store.get();
        d.tags = m_tags.get();
        d.profiles = m_profiles.get();
        d.embeddings = m_index.get();
        d.embeddingPreparer = m_prep.get();
        d.config = [this]() { return m_cfg; };
        d.llm = [this](const QString&, QObject* parent) {
            library::TagSuggestionService::LlmSession s;
            if (!m_hasProvider) return s;
            s.suggester = new processing::LlmTagSuggestionProvider(m_tags.get(), &m_chat, providers::ProviderConfig{}, parent);
            s.model = QStringLiteral("m");
            s.systemPrompt = QStringLiteral("system prompt");
            s.done = [this](bool ok) { ++m_llmDone; m_llmOk = ok; };
            return s;
        };
        d.embedder = [this](const QString& id, QObject*) -> providers::ITextEmbedder* {
            m_embedderId = id;
            return nullptr;
        };
        d.backgroundHook = [this]() {
            const int n = ++m_bgCalls;
            if (m_blockFirst && n == 1) {
                m_entered.release();
                m_proceed.acquire();
            }
        };
        m_svc = std::make_unique<library::TagSuggestionService>(d);
    }

private slots:
    void init()
    {
        const QString base = m_tmp.filePath(QString::number(++m_n));
        QDir().mkpath(base + "/rec");
        QDir().mkpath(base + "/meta");
        m_store = std::make_unique<library::MeetingRepository>(base + "/rec", base + "/meta");
        m_lib = tagsfixture::buildLibrary(*m_store);
        m_tags = std::make_unique<library::TagService>(m_store.get(), base + "/meta/tags.json");
        m_profiles = std::make_unique<library::MeetingProfiles>(m_store.get());
        m_tags->setProfiles(m_profiles.get());
        m_index = std::make_unique<library::EmbeddingIndex>(m_store.get());
        m_index->setProfiles(m_profiles.get());
        m_prep = std::make_unique<library::EmbeddingPreparer>(m_store.get(), m_index.get(), base + "/meta/emb.json");
        m_tags->addTag(m_lib.museum1, QStringLiteral("MuseumPlus"));
        m_tags->addTag(m_lib.nordvik1, QStringLiteral("Nordvik"));
        m_cfg = {};
    }
    void cleanup()
    {
        m_svc.reset();
        m_prep.reset();
        m_index.reset();
        m_profiles.reset();
        m_tags.reset();
        m_store.reset();
        m_hasProvider = true;
        m_llmDone = 0;
        m_llmOk = false;
        m_bgCalls = 0;
        m_blockFirst = false;
        m_chat.mode = ScriptedChat::Mode::Reply;
        m_chat.reply = QStringLiteral("pick: 1\nnew: none");
        m_chat.calls = 0;
    }

    void similarityPublishesThenServesFromCache()
    {
        makeService();
        QSignalSpy ready(m_svc.get(), &library::TagSuggestionService::suggestionsReady);
        QSignalSpy computing(m_svc.get(), &library::TagSuggestionService::computing);
        m_svc->requestSuggestions(m_lib.museum2);
        QCOMPARE(computing.count(), 1);
        QVERIFY(waitFor([&]() { return ready.count() == 1; }));
        QCOMPARE(ready.at(0).at(0).toString(), m_lib.museum2);
        const auto list = ready.at(0).at(1).value<QVector<TagSuggestion>>();
        QVERIFY2(names(list).contains(QStringLiteral("MuseumPlus")), qPrintable(names(list).join(',')));
        QVERIFY(!names(list).contains(QStringLiteral("Nordvik")));
        QCOMPARE(names(m_svc->pending(m_lib.museum2)), names(list));
        QCOMPARE(m_bgCalls.load(), 1);

        // The cache answers within the call.
        m_svc->requestSuggestions(m_lib.museum2);
        QCOMPARE(ready.count(), 2);
        QCOMPARE(m_bgCalls.load(), 1);
        // An empty meeting id is ignored.
        m_svc->requestSuggestions(QString());
        QCOMPARE(ready.count(), 2);
    }

    void staleGenerationIsDroppedAndRecomputed()
    {
        m_blockFirst = true;
        makeService();
        QSignalSpy ready(m_svc.get(), &library::TagSuggestionService::suggestionsReady);
        m_svc->requestSuggestions(m_lib.museum2);
        // The first computation is on the pool (snapshot taken); the tag set changes under it.
        QVERIFY(waitFor([&]() { return m_entered.available() > 0; }));
        m_entered.acquire();
        m_tags->addTag(m_lib.museum1, QStringLiteral("Rollout"));
        m_proceed.release();
        QVERIFY(waitFor([&]() { return ready.count() >= 1; }));
        QTest::qWait(200);
        // Exactly one publication: the stale result was dropped, the recomputed one carries the new tag.
        QCOMPARE(ready.count(), 1);
        QCOMPARE(m_bgCalls.load(), 2);
        const auto list = ready.at(0).at(1).value<QVector<TagSuggestion>>();
        QVERIFY2(names(list).contains(QStringLiteral("Rollout")), qPrintable(names(list).join(',')));
        QVERIFY(names(list).contains(QStringLiteral("MuseumPlus")));
    }

    void disabledPublishesEmpty()
    {
        m_cfg.tagSuggestions = false;
        makeService();
        QSignalSpy ready(m_svc.get(), &library::TagSuggestionService::suggestionsReady);
        m_svc->requestSuggestions(m_lib.museum2);
        QCOMPARE(ready.count(), 1);
        QVERIFY(ready.at(0).at(1).value<QVector<TagSuggestion>>().isEmpty());
        QCOMPARE(m_bgCalls.load(), 0);
        QVERIFY(m_svc->draft(QStringLiteral("MuseumPlus")).isEmpty());
        m_svc->requestLlm(m_lib.museum2);
        QCOMPARE(ready.count(), 1);
    }

    void appliedTagDropsOutOfThePublishedList()
    {
        makeService();
        QSignalSpy ready(m_svc.get(), &library::TagSuggestionService::suggestionsReady);
        m_svc->requestSuggestions(m_lib.museum2);
        QVERIFY(waitFor([&]() { return ready.count() == 1; }));
        const Tag mp = m_tags->byName(QStringLiteral("MuseumPlus"));
        m_tags->addTag(m_lib.museum2, mp.name);   // applied -> meetingTagsChanged -> republished filtered
        QVERIFY(ready.count() >= 2);
        const auto list = ready.last().at(1).value<QVector<TagSuggestion>>();
        QVERIFY(!names(list).contains(QStringLiteral("MuseumPlus")));
    }

    void llmBranchPicksAndInventsNames()
    {
        makeService();
        writeSummary(m_lib.museum2);
        m_chat.reply = QStringLiteral("pick: 1\nnew: Raktárköltözés");
        QSignalSpy ready(m_svc.get(), &library::TagSuggestionService::suggestionsReady);
        m_svc->requestLlm(m_lib.museum2);   // the candidate list is made on the pool first
        QVERIFY(waitFor([&]() { return m_llmDone == 1; }));
        QVERIFY(m_llmOk);
        QVERIFY(waitFor([&]() { return ready.count() >= 1; }));
        const auto list = ready.last().at(1).value<QVector<TagSuggestion>>();
        QVERIFY2(names(list).contains(QStringLiteral("Raktárköltözés")), qPrintable(names(list).join(',')));
        bool llmSource = false;
        for (const TagSuggestion& t : list) llmSource = llmSource || t.source == SuggestionSource::Llm;
        QVERIFY(llmSource);
        QCOMPARE(m_chat.calls, 1);
    }

    void llmWithoutSummaryDoesNothing()
    {
        makeService();
        QSignalSpy ready(m_svc.get(), &library::TagSuggestionService::suggestionsReady);
        QSignalSpy computing(m_svc.get(), &library::TagSuggestionService::computing);
        m_svc->requestLlm(m_lib.museum2);
        QCOMPARE(computing.count(), 0);
        QCOMPARE(ready.count(), 0);
        QCOMPARE(m_chat.calls, 0);
    }

    void automaticLlmPassFollowsTheSettings()
    {
        makeService();
        writeSummary(m_lib.museum2);
        m_svc->summaryReady(m_lib.museum2);   // llmTagSuggestions is off
        QTest::qWait(100);
        QCOMPARE(m_chat.calls, 0);
        m_cfg.llmTagSuggestions = true;
        m_svc->summaryReady(m_lib.museum2);
        QVERIFY(waitFor([&]() { return m_llmDone == 1; }));
        QCOMPARE(m_chat.calls, 1);
    }

    void llmFailureKeepsTheEarlierList()
    {
        makeService();
        writeSummary(m_lib.museum2);
        QSignalSpy ready(m_svc.get(), &library::TagSuggestionService::suggestionsReady);
        m_svc->requestSuggestions(m_lib.museum2);
        QVERIFY(waitFor([&]() { return ready.count() == 1; }));
        const auto before = m_svc->pending(m_lib.museum2);
        QVERIFY(!before.isEmpty());

        m_chat.mode = ScriptedChat::Mode::Fail;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("LLM-javaslat sikertelen"));
        m_svc->requestLlm(m_lib.museum2);
        QVERIFY(waitFor([&]() { return m_llmDone == 1; }));
        QVERIFY(!m_llmOk);
        QVERIFY(waitFor([&]() { return ready.count() >= 2; }));
        QCOMPARE(names(ready.last().at(1).value<QVector<TagSuggestion>>()), names(before));
    }

    void llmEmptyAnswerKeepsTheEarlierList()
    {
        makeService();
        writeSummary(m_lib.museum2);
        QSignalSpy ready(m_svc.get(), &library::TagSuggestionService::suggestionsReady);
        m_svc->requestSuggestions(m_lib.museum2);
        QVERIFY(waitFor([&]() { return ready.count() == 1; }));
        const auto before = m_svc->pending(m_lib.museum2);
        m_chat.reply = QStringLiteral("pick: none\nnew: none");
        m_svc->requestLlm(m_lib.museum2);
        QVERIFY(waitFor([&]() { return m_llmDone == 1; }));
        QVERIFY(waitFor([&]() { return ready.count() >= 2; }));
        QCOMPARE(names(ready.last().at(1).value<QVector<TagSuggestion>>()), names(before));
    }

    void noProviderPublishesTheEarlierList()
    {
        makeService();
        writeSummary(m_lib.museum2);
        QSignalSpy ready(m_svc.get(), &library::TagSuggestionService::suggestionsReady);
        m_svc->requestSuggestions(m_lib.museum2);
        QVERIFY(waitFor([&]() { return ready.count() == 1; }));
        m_hasProvider = false;
        m_svc->requestLlm(m_lib.museum2);   // the cache is warm: starts within the call
        QCOMPARE(ready.count(), 2);
        QCOMPARE(m_llmDone, 0);
        QCOMPARE(m_chat.calls, 0);
    }

    void cancelStopsTheLlmCall()
    {
        makeService();
        writeSummary(m_lib.museum2);
        QSignalSpy ready(m_svc.get(), &library::TagSuggestionService::suggestionsReady);
        m_svc->requestSuggestions(m_lib.museum2);
        QVERIFY(waitFor([&]() { return ready.count() == 1; }));
        m_svc->requestLlm(m_lib.museum2);   // in flight (the chat call starts in the next turn)
        QVERIFY(m_svc->cancel(m_lib.museum2));
        QCOMPARE(ready.count(), 2);         // the view leaves "computing" with the list as it was
        QTest::qWait(150);
        QCOMPARE(m_chat.calls, 0);
        QCOMPARE(m_llmDone, 0);
        QCOMPARE(ready.count(), 2);
        QVERIFY(!m_svc->cancel(m_lib.museum2));   // nothing left to cancel
    }

    void cancelDropsAWaitingSimilarityRequest()
    {
        m_blockFirst = true;
        makeService();
        QSignalSpy ready(m_svc.get(), &library::TagSuggestionService::suggestionsReady);
        m_svc->requestSuggestions(m_lib.museum2);
        QVERIFY(waitFor([&]() { return m_entered.available() > 0; }));
        m_entered.acquire();
        QVERIFY(m_svc->cancel(m_lib.museum2));
        QCOMPARE(ready.count(), 1);   // the cancel's own republication (empty pending)
        m_proceed.release();
        QTest::qWait(300);
        QCOMPARE(ready.count(), 1);   // the finished computation publishes nothing
        // ...but its result is cached: the next request is answered at once.
        m_svc->requestSuggestions(m_lib.museum2);
        QCOMPARE(ready.count(), 2);
        QCOMPARE(m_bgCalls.load(), 1);
    }

    void newerLlmRunSupersedesTheOlder()
    {
        makeService();
        writeSummary(m_lib.museum2);
        writeSummary(m_lib.museum1);
        QSignalSpy ready(m_svc.get(), &library::TagSuggestionService::suggestionsReady);
        m_svc->requestSuggestions(m_lib.museum2);
        m_svc->requestSuggestions(m_lib.museum1);
        QVERIFY(waitFor([&]() { return ready.count() >= 2; }));
        m_chat.mode = ScriptedChat::Mode::Hang;
        m_svc->requestLlm(m_lib.museum2);   // never answers
        QVERIFY(waitFor([&]() { return m_chat.calls == 1; }));
        const int before = ready.count();
        m_chat.mode = ScriptedChat::Mode::Reply;
        m_svc->requestLlm(m_lib.museum1);   // the first is aborted, its meeting's list goes out again
        QVERIFY(ready.count() > before);
        QVERIFY(waitFor([&]() { return m_llmDone == 1; }));
        QVERIFY(m_llmOk);
        QCOMPARE(m_chat.calls, 2);
    }

    void cooccurAfterAppliedTag()
    {
        makeService();
        QSignalSpy ready(m_svc.get(), &library::TagSuggestionService::suggestionsReady);
        // No co-occurring tags in this library: nothing is emitted.
        m_svc->requestCooccur(m_lib.museum2, m_tags->byName(QStringLiteral("MuseumPlus")).id);
        QCOMPARE(ready.count(), 0);
    }

    void embeddingSettingsAreAppliedToThePreparer()
    {
        makeService();
        m_cfg.embeddingId = QStringLiteral("fake-embed");
        m_cfg.embeddingBaseUrl = QStringLiteral("http://localhost:1/");
        m_cfg.embeddingModel = QStringLiteral("model-a");
        m_svc->applyEmbeddingSettings(false);
        QCOMPARE(m_prep->state().provider, QStringLiteral("fake-embed"));
        QCOMPARE(m_prep->state().model, QStringLiteral("model-a"));
        QVERIFY(m_prep->isConfigured());
        // Another setting changed (same key): nothing happens.
        m_svc->applyEmbeddingSettings(true);
        QCOMPARE(m_prep->state().model, QStringLiteral("model-a"));
        // The selection is dropped.
        m_cfg.embeddingId.clear();
        m_cfg.embeddingModel.clear();
        m_svc->applyEmbeddingSettings(true);
        QVERIFY(m_prep->state().provider.isEmpty());
        QVERIFY(!m_prep->isConfigured());
    }

    void shutdownWaitsAndRefusesNewWork()
    {
        makeService();
        QSignalSpy ready(m_svc.get(), &library::TagSuggestionService::suggestionsReady);
        m_svc->shutdown();
        m_svc->requestSuggestions(m_lib.museum2);
        QTest::qWait(100);
        QCOMPARE(ready.count(), 0);
    }
};

QTEST_MAIN(TestTagSuggestionService)
#include "test_tag_suggestion_service.moc"
