// AppController: wiring the tags: suggestion request (Computing to Ready, cache, filtering
// after apply / reject), co-occurring tags, LLM suggestion (manual and the automatic one after
// the summary) against a fake HTTP server, configuring the embedding provider + model change,
// and the settings' persistence. Isolated TANARA_HOME; no real network / LM Studio.
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QJsonArray>

#include "tanara/app/AppController.h"
#include "tanara/app/SettingsManager.h"
#include "tanara/library/index/EmbeddingIndex.h"
#include "tanara/library/index/EmbeddingPreparer.h"
#include "tanara/providers/embedding/EmbeddingProviderRegistry.h"
#include "tanara/library/MeetingLibrary.h"
#include "tanara/library/store/JsonSerialization.h"
#include "tanara/library/store/MeetingStore.h"
#include "tanara/library/tags/MeetingProfiles.h"
#include "tanara/library/tags/TagService.h"
#include "fake_http.h"
#include "tags_fixture.h"

using namespace tanara;

#ifndef Q_MOC_RUN
namespace {

QByteArray chatReply(const QString& content)
{
    const QJsonObject reply{ { "choices", QJsonArray{ QJsonObject{
        { "message", QJsonObject{ { "role", "assistant" }, { "content", content } } },
        { "finish_reason", "stop" } } } } };
    return QJsonDocument(reply).toJson(QJsonDocument::Compact);
}

QByteArray embeddingsReply(const QByteArray& body)
{
    const QJsonArray input = QJsonDocument::fromJson(body).object().value("input").toArray();
    QJsonArray data;
    for (int i = 0; i < input.size(); ++i)
        data.append(QJsonObject{ { "index", i }, { "embedding", QJsonArray{ 1.0, double(input.at(i).toString().size()), 0.5 } } });
    return QJsonDocument(QJsonObject{ { "data", data } }).toJson(QJsonDocument::Compact);
}

} // namespace
#endif

class TagsControllerTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void cleanup();

    void suggestionsComputingThenReady();
    void asyncCoalesceStaleDropAndCache();
    void cooccurAfterAdd();
    void llmSuggestionsManualAndAutomatic();
    void embeddingProviderAndModelChange();
    void settingsRoundTrip();

private:
    QVector<TagSuggestion> waitReady(QSignalSpy& spy, const QString& meetingId);
    std::unique_ptr<QTemporaryDir> m_home;
    std::unique_ptr<fakehttp::Server> m_http;
    std::unique_ptr<app::AppController> m_app;
    tagsfixture::Library m_lib;
    QString m_llmReply = QStringLiteral("pick: 1\nnew: none");
};

void TagsControllerTest::initTestCase()
{
    qputenv("TANARA_CLOUD", "off");
}

void TagsControllerTest::init()
{
    m_home = std::make_unique<QTemporaryDir>();
    QVERIFY(m_home->isValid());
    qputenv("TANARA_HOME", m_home->path().toUtf8());
    m_http = std::make_unique<fakehttp::Server>();
    m_http->handler = [this](const fakehttp::Request& r) -> fakehttp::Reply {
        if (r.path.endsWith("/chat/completions")) return { 200, chatReply(m_llmReply) };
        if (r.path.endsWith("/embeddings")) return { 200, embeddingsReply(r.body) };
        return { 404, "{}" };
    };
    m_app = std::make_unique<app::AppController>();
    AppSettings s = m_app->settings()->settings();
    QVERIFY(s.audioDir.startsWith(m_home->path()));
    s.llmProviderId = QStringLiteral("openai-compat");
    s.llmConfigs[s.llmProviderId].baseUrl = m_http->base();
    s.llmConfigs[s.llmProviderId].model = QStringLiteral("google/gemma-4-12b");
    m_app->settings()->setSettings(s);
    m_lib = tagsfixture::buildLibrary(*m_app->store());
}

void TagsControllerTest::cleanup()
{
    m_app.reset();
    m_http.reset();
    m_home.reset();
}

QVector<TagSuggestion> TagsControllerTest::waitReady(QSignalSpy& spy, const QString& meetingId)
{
    for (int round = 0; round < 50; ++round) {
        for (const QList<QVariant>& args : std::as_const(spy))
            if (args.at(0).toString() == meetingId) return args.at(1).value<QVector<TagSuggestion>>();
        spy.wait(200);
    }
    return {};
}

void TagsControllerTest::suggestionsComputingThenReady()
{
    library::TagService* tags = m_app->tags();
    QVERIFY(tags && m_app->profiles() && m_app->embeddings() && m_app->embeddingPreparer());
    const Tag nordvik = tags->addTag(m_lib.nordvik1, QStringLiteral("Nordvik"));
    const Tag log = tags->addTag(m_lib.nordvik1, QStringLiteral("Logisztika"));

    QStringList order;
    connect(m_app.get(), &app::AppController::tagSuggestionsComputing, this, [&](const QString& id) { if (id == m_lib.nordvik2) order << "computing"; });
    connect(m_app.get(), &app::AppController::tagSuggestionsReady, this, [&](const QString& id) { if (id == m_lib.nordvik2) order << "ready"; });
    QSignalSpy ready(m_app.get(), &app::AppController::tagSuggestionsReady);
    m_app->requestTagSuggestions(m_lib.nordvik2);
    QVector<TagSuggestion> list = waitReady(ready, m_lib.nordvik2);
    QCOMPARE(order.value(0), QStringLiteral("computing"));
    QCOMPARE(order.value(1), QStringLiteral("ready"));
    QCOMPARE(list.size(), 2);
    QCOMPARE(m_app->pendingTagSuggestions(m_lib.nordvik2).size(), 2);

    // From the cache: it comes at once (in the same loop round).
    ready.clear();
    m_app->requestTagSuggestions(m_lib.nordvik2);
    QCOMPARE(ready.count(), 1);

    // Apply: the emitted list goes out again, filtered.
    ready.clear();
    tags->addTag(m_lib.nordvik2, nordvik.id, TagSource::Suggestion);
    QVERIFY(ready.count() >= 1);
    list = ready.last().at(1).value<QVector<TagSuggestion>>();
    QCOMPARE(list.size(), 1);
    QCOMPARE(list.first().tagId, log.id);
    // Reject: it drops out.
    ready.clear();
    tags->reject(m_lib.nordvik2, list.first());
    QVERIFY(ready.count() >= 1);
    QVERIFY(m_app->pendingTagSuggestions(m_lib.nordvik2).isEmpty());

    // Suggestions switched off: an empty list.
    AppSettings s = m_app->settings()->settings();
    s.tagSuggestions = false;
    m_app->settings()->setSettings(s);
    ready.clear();
    m_app->requestTagSuggestions(m_lib.nordvik1);
    QCOMPARE(ready.count(), 1);
    QVERIFY(ready.first().at(1).value<QVector<TagSuggestion>>().isEmpty());

    // Before import / recording: by title.
    s.tagSuggestions = true;
    m_app->settings()->setSettings(s);
    const QVector<TagSuggestion> draft = m_app->draftTagSuggestions(QStringLiteral("Nordvik heti egyeztetés"));
    QVERIFY(!draft.isEmpty());
    QCOMPARE(draft.first().tagId, nordvik.id);
}

void TagsControllerTest::asyncCoalesceStaleDropAndCache()
{
    library::TagService* tags = m_app->tags();
    tags->addTag(m_lib.nordvik1, QStringLiteral("Nordvik"));
    QSignalSpy ready(m_app.get(), &app::AppController::tagSuggestionsReady);
    auto readyFor = [&](const QString& id) {
        int n = 0;
        for (const QList<QVariant>& a : std::as_const(ready)) if (a.at(0).toString() == id) ++n;
        return n;
    };
    auto names = [](const QVector<TagSuggestion>& list) {
        QStringList out;
        for (const TagSuggestion& sg : list) out << sg.name;
        return out;
    };

    // On a background thread: no result in the call; repeated requests merge into one computation.
    m_app->requestTagSuggestions(m_lib.nordvik2);
    m_app->requestTagSuggestions(m_lib.nordvik2);
    m_app->requestTagSuggestions(m_lib.nordvik2);
    QCOMPARE(readyFor(m_lib.nordvik2), 0);
    QVector<TagSuggestion> list = waitReady(ready, m_lib.nordvik2);
    QCOMPARE(names(list), QStringList{ QStringLiteral("Nordvik") });
    QTest::qWait(150);
    QCOMPARE(readyFor(m_lib.nordvik2), 1);

    // From the cache at once; after the tag set changes it computes again on the background thread.
    ready.clear();
    m_app->requestTagSuggestions(m_lib.nordvik2);
    QCOMPARE(ready.count(), 1);
    tags->addTag(m_lib.budget2, QStringLiteral("Belső"));
    ready.clear();
    m_app->requestTagSuggestions(m_lib.nordvik2);
    QCOMPARE(ready.count(), 0);
    QCOMPARE(names(waitReady(ready, m_lib.nordvik2)), QStringList{ QStringLiteral("Nordvik") });

    // Stale result: the computation has already started (the snapshot is ready) when the
    // neighbor gets a new tag, so the list computed from the old state does not go out, and it is
    // recomputed from the new state.
    tags->addTag(m_lib.budget1, QStringLiteral("Belső"));   // cache clear
    bool changed = false;
    // AppController's idle slot (starting the computation) runs before this.
    const auto conn = connect(m_app->profiles(), &library::MeetingProfiles::idle, this, [&] {
        if (changed) return;
        changed = true;
        tags->addTag(m_lib.nordvik1, QStringLiteral("Logisztika"));
    });
    ready.clear();
    m_app->requestTagSuggestions(m_lib.nordvik2);
    list = waitReady(ready, m_lib.nordvik2);
    disconnect(conn);
    QVERIFY(changed);
    QVERIFY2(names(list).contains(QStringLiteral("Logisztika")), qPrintable(names(list).join(", ")));
    QTest::qWait(150);
    QCOMPARE(readyFor(m_lib.nordvik2), 1);
}

void TagsControllerTest::cooccurAfterAdd()
{
    library::TagService* tags = m_app->tags();
    const Tag a = tags->create(QStringLiteral("Partnerek"));
    const Tag b = tags->create(QStringLiteral("Szerződés"));
    for (const QString& id : { m_lib.nordvik1, m_lib.budget1 }) { tags->addTag(id, a.id); tags->addTag(id, b.id); }
    QSignalSpy computing(m_app.get(), &app::AppController::tagSuggestionsComputing);
    QSignalSpy ready(m_app.get(), &app::AppController::tagSuggestionsReady);
    tags->addTag(m_lib.museum1, a.id);
    ready.clear();
    m_app->requestCooccurSuggestions(m_lib.museum1, a.id);
    QCOMPARE(computing.count(), 1);
    QCOMPARE(ready.count(), 1);
    const auto list = ready.first().at(1).value<QVector<TagSuggestion>>();
    QCOMPARE(list.size(), 1);
    QCOMPARE(list.first().tagId, b.id);
    QCOMPARE(list.first().source, SuggestionSource::Cooccur);
    // No co-occurring tag: no signal.
    ready.clear();
    m_app->requestCooccurSuggestions(m_lib.museum1, b.id);
    QCOMPARE(ready.count(), 0);
}

void TagsControllerTest::llmSuggestionsManualAndAutomatic()
{
    library::TagService* tags = m_app->tags();
    // Without a summary there is nothing to request.
    QSignalSpy ready(m_app.get(), &app::AppController::tagSuggestionsReady);
    m_app->requestLlmTagSuggestions(m_lib.museum2);
    QCOMPARE(ready.count(), 0);
    QCOMPARE(m_http->count("POST", "/chat/completions"), 0);

    const Meeting m = m_app->store()->load(m_lib.museum2);
    QFile md(QDir(m.folder).filePath("summary.md"));
    QVERIFY(md.open(QIODevice::WriteOnly));
    md.write("## Összefoglaló\nA MuseumPlus oktatásáról és a raktár költözéséről volt szó.\n");
    md.close();

    // With an empty set the automatic step after the summary does not run.
    emit m_app->summaryReady(m_lib.museum2, md.fileName());
    QTest::qWait(50);
    QCOMPARE(m_http->count("POST", "/chat/completions"), 0);

    const Tag museum = tags->addTag(m_lib.museum1, QStringLiteral("MuseumPlus"));
    m_llmReply = QStringLiteral("pick: 1\nnew: Raktárköltözés");
    ready.clear();
    m_app->requestLlmTagSuggestions(m_lib.museum2);
    QVector<TagSuggestion> list = waitReady(ready, m_lib.museum2);
    QCOMPARE(m_http->count("POST", "/chat/completions"), 1);
    QCOMPARE(list.size(), 2);
    QCOMPARE(list.at(0).tagId, museum.id);
    QCOMPARE(list.at(0).source, SuggestionSource::Llm);
    QVERIFY(list.at(1).isNew);
    const QJsonObject body = m_http->lastBody("/chat/completions");
    QCOMPARE(body.value("reasoning_effort").toString(), QStringLiteral("none"));
    QVERIFY(body.value("messages").toArray().at(1).toObject().value("content").toString().contains("raktár költözéséről"));

    // Accepting the new name: the tag is created, the suggestion drops out.
    ready.clear();
    tags->addTag(m_lib.museum2, list.at(1).name, TagSource::Llm);
    QVERIFY(tags->byName(QStringLiteral("Raktárköltözés")).isValid());
    for (const TagSuggestion& s : m_app->pendingTagSuggestions(m_lib.museum2)) QVERIFY(!s.isNew);

    // Automatic: after the summary is finished (there is already a tag set).
    ready.clear();
    emit m_app->summaryReady(m_lib.museum2, md.fileName());
    QTRY_COMPARE(m_http->count("POST", "/chat/completions"), 2);
    QVERIFY(!waitReady(ready, m_lib.museum2).isEmpty() || ready.count() >= 1);

    // Switched off it does not run.
    AppSettings s = m_app->settings()->settings();
    s.llmTagSuggestions = false;
    m_app->settings()->setSettings(s);
    emit m_app->summaryReady(m_lib.museum2, md.fileName());
    QTest::qWait(50);
    QCOMPARE(m_http->count("POST", "/chat/completions"), 2);

    // A bad model reply: no pop-up error, the earlier list stays.
    QSignalSpy errors(m_app.get(), &app::AppController::errorOccurred);
    m_http->handler = [](const fakehttp::Request&) -> fakehttp::Reply { return { 500, R"({"error":"x"})" }; };
    const int before = int(m_app->pendingTagSuggestions(m_lib.museum2).size());
    ready.clear();
    m_app->requestLlmTagSuggestions(m_lib.museum2);
    QVERIFY(!waitReady(ready, m_lib.museum2).isEmpty() || before == 0);
    QCOMPARE(errors.count(), 0);
}

void TagsControllerTest::embeddingProviderAndModelChange()
{
    library::EmbeddingPreparer* prep = m_app->embeddingPreparer();
    QCOMPARE(prep->state().provider, QString());       // default: no embedding
    ProviderConfig cfg;
    cfg.baseUrl = m_http->base();
    cfg.model = QStringLiteral("embed-a");
    m_app->setEmbeddingProvider(providers::embeddingproviders::LocalId, cfg);
    QCOMPARE(m_app->settings()->settings().embeddingProviderId, providers::embeddingproviders::LocalId);
    QTRY_COMPARE_WITH_TIMEOUT(int(prep->state().status), int(library::EmbeddingState::Done), 10000);
    QCOMPARE(prep->state().prepared, 6);
    const QString file = QDir(m_app->store()->load(m_lib.budget1).folder).filePath("text.embeddings.bin");
    QVERIFY(QFile::exists(file));
    QVERIFY(m_app->embeddings()->has(m_lib.budget1));
    const int calls = m_http->count("POST", "/embeddings");

    // A change of another setting does not restart it.
    AppSettings s = m_app->settings()->settings();
    s.summaryLanguage = QStringLiteral("angol");
    m_app->settings()->setSettings(s);
    QCOMPARE(m_http->count("POST", "/embeddings"), calls);

    // Model change: the index dropped, the preparation from the start.
    QSignalSpy reset(m_app->embeddings(), &library::EmbeddingIndex::indexReset);
    cfg.model = QStringLiteral("embed-b");
    m_app->setEmbeddingProvider(providers::embeddingproviders::LocalId, cfg);
    QVERIFY(reset.count() >= 1);
    QTRY_COMPARE_WITH_TIMEOUT(int(prep->state().status), int(library::EmbeddingState::Done), 10000);
    QCOMPARE(prep->state().model, QStringLiteral("embed-b"));
    QCOMPARE(m_http->count("POST", "/embeddings"), calls + 6);
    QCOMPARE(m_http->lastBody("/embeddings").value("model").toString(), QStringLiteral("embed-b"));

    // New transcript: it is embedded in the background.
    QTest::qWait(20);
    tagsfixture::writeSegments(m_app->store()->load(m_lib.budget1).folder, { "Új átirat a költségvetésről." });
    emit m_app->transcriptReady(m_lib.budget1, QString());
    QTRY_COMPARE_WITH_TIMEOUT(m_http->count("POST", "/embeddings"), calls + 7, 5000);
    QTRY_VERIFY(m_app->embeddings()->has(m_lib.budget1));

    // Switch off: no embedding.
    m_app->setEmbeddingProvider(QString(), {});
    QVERIFY(!m_app->embeddings()->has(m_lib.budget1));
    QCOMPARE(prep->state().status, library::EmbeddingState::Idle);
}

void TagsControllerTest::settingsRoundTrip()
{
    AppSettings s = m_app->settings()->settings();
    QVERIFY(s.tagSuggestions);
    QVERIFY(s.llmTagSuggestions);
    QVERIFY(s.embeddingProviderId.isEmpty());
    QVERIFY(s.embeddingConfigs.contains(providers::embeddingproviders::LocalId));   // prefilled
    s.tagSuggestions = false;
    s.llmTagSuggestions = false;
    s.embeddingProviderId = providers::embeddingproviders::CloudId;
    s.embeddingConfigs[providers::embeddingproviders::CloudId].model = QStringLiteral("x");
    m_app->settings()->setSettings(s);
    app::SettingsManager again(m_app->settings()->settings().metadataDir);
    const AppSettings r = again.settings();
    QVERIFY(!r.tagSuggestions);
    QVERIFY(!r.llmTagSuggestions);
    QCOMPARE(r.embeddingProviderId, providers::embeddingproviders::CloudId);
    QCOMPARE(r.embeddingSelected().model, QStringLiteral("x"));
    QVERIFY(!library::toJson(r).contains("apiKey"));
}

QTEST_GUILESS_MAIN(TagsControllerTest)
#include "test_tags_controller.moc"
