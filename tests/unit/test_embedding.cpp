// Embedding: the OpenAI-compatible provider against a fake HTTP server (batching, order,
// error), text.embeddings.bin both ways, chunking, library preparation (progress, cancel,
// error, resume) and invalidation on a model change. No real endpoint.
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QJsonArray>

#include "tanara/editing/session/SpeakerOverlay.h"
#include "tanara/library/index/EmbeddingIndex.h"
#include "tanara/library/index/EmbeddingPreparer.h"
#include "tanara/providers/embedding/EmbeddingProviderRegistry.h"
#include "tanara/providers/embedding/OpenAiCompatibleEmbeddingProvider.h"
#include "tanara/providers/core/ProviderRegistry.h"
#include "tanara/library/store/MeetingStore.h"
#include "fake_http.h"
#include "tags_fixture.h"

using namespace tanara;

#ifndef Q_MOC_RUN
namespace {

// Deterministic "embedding": from the text's length and first letter; the index field
// intentionally arrives in reverse order (the client must sort by index).
QByteArray embeddingsReply(const QByteArray& body)
{
    const QJsonArray input = QJsonDocument::fromJson(body).object().value("input").toArray();
    QJsonArray data;
    for (int i = int(input.size()) - 1; i >= 0; --i) {
        const QString t = input.at(i).toString();
        data.append(QJsonObject{ { "index", i }, { "object", "embedding" },
                                 { "embedding", QJsonArray{ double(t.size()), double(t.isEmpty() ? 0 : t.at(0).unicode()), 1.0 } } });
    }
    return QJsonDocument(QJsonObject{ { "object", "list" }, { "data", data } }).toJson(QJsonDocument::Compact);
}

} // namespace
#endif

class EmbeddingTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    void registryDescriptors();
    void providerBatchesAndOrder();
    void providerErrors();
    void chunking();
    void indexFileRoundTrip();
    void preparerProgressAndDone();
    void preparerErrorAndResume();
    void preparerCancelAndResume();
    void modelChangeInvalidates();

private:
    library::EmbeddingPreparer::ProviderFactory factory();
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<library::MeetingStore> m_store;
    std::unique_ptr<fakehttp::Server> m_http;
};

void EmbeddingTest::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    m_store = std::make_unique<library::MeetingStore>(m_dir->filePath("rec"), m_dir->filePath("meta"));
    m_http = std::make_unique<fakehttp::Server>();
    m_http->handler = [](const fakehttp::Request& r) -> fakehttp::Reply {
        if (r.path.endsWith("/embeddings")) return { 200, embeddingsReply(r.body) };
        return { 404, "{}" };
    };
}

void EmbeddingTest::cleanup()
{
    m_http.reset();
    m_store.reset();
    m_dir.reset();
}

library::EmbeddingPreparer::ProviderFactory EmbeddingTest::factory()
{
    const QString base = m_http->base();
    return [base](QObject* parent) -> providers::IEmbeddingProvider* {
        ProviderConfig cfg;
        cfg.baseUrl = base;
        cfg.model = QStringLiteral("teszt-embed");
        return new providers::OpenAiCompatibleEmbeddingProvider(cfg, parent);
    };
}

void EmbeddingTest::registryDescriptors()
{
    providers::registerBuiltinProviders();
    const auto& reg = providers::EmbeddingProviderRegistry::instance();
    QVERIFY(reg.has(providers::embeddingproviders::LocalId));
    QVERIFY(reg.has(providers::embeddingproviders::CloudId));
    const providers::ProviderDescriptor local = reg.descriptor(providers::embeddingproviders::LocalId);
    QCOMPARE(local.kind, providers::ProviderKind::Embedding);
    QCOMPARE(local.probe.path, QStringLiteral("/models"));
    QVERIFY(local.probe.listsModels);
    bool url = false, model = false;
    for (const providers::ConfigField& f : local.fields) {
        if (f.key == "baseUrl") url = f.type == providers::ConfigFieldType::Url;
        if (f.key == "model") model = f.type == providers::ConfigFieldType::Combo && f.dynamicOptions;
    }
    QVERIFY(url && model);
    const providers::ProviderDescriptor cloud = reg.descriptor(providers::embeddingproviders::CloudId);
    QCOMPARE(cloud.authMode, providers::AuthMode::Login);
    QVERIFY(cloud.fields.isEmpty());
    std::unique_ptr<QObject> holder(new QObject);
    QVERIFY(reg.create(providers::embeddingproviders::LocalId, ProviderConfig{}, holder.get()));
}

void EmbeddingTest::providerBatchesAndOrder()
{
    ProviderConfig cfg;
    cfg.baseUrl = m_http->base() + "/";
    cfg.model = QStringLiteral("teszt-embed");
    cfg.apiKey = QStringLiteral("titok");
    providers::OpenAiCompatibleEmbeddingProvider p(cfg);
    QStringList texts;
    for (int i = 0; i < 20; ++i) texts << QString(i + 1, QLatin1Char('a' + (i % 26)));
    providers::EmbeddingJob* job = p.embed({ texts, QString() });
    QSignalSpy done(job, &providers::EmbeddingJob::finished);
    QVERIFY(done.wait(5000));
    QCOMPARE(m_http->count("POST", "/v1/embeddings"), 2);   // 16 + 4
    QCOMPARE(m_http->log.first().body.contains("\"model\":\"teszt-embed\""), true);
    QVERIFY(m_http->log.first().head.contains("Bearer titok"));
    const auto vectors = done.first().first().value<QVector<QVector<float>>>();
    QCOMPARE(vectors.size(), 20);
    for (int i = 0; i < 20; ++i) QCOMPARE(int(vectors.at(i).at(0)), i + 1);   // in order
}

void EmbeddingTest::providerErrors()
{
    ProviderConfig cfg;
    cfg.baseUrl = m_http->base();
    cfg.model = QStringLiteral("teszt-embed");
    providers::OpenAiCompatibleEmbeddingProvider p(cfg);
    m_http->handler = [](const fakehttp::Request&) -> fakehttp::Reply {
        return { 401, R"({"error":{"code":"auth_required","message":"Jelentkezz be a Tanara Cloudba.","request_id":"r1"}})" };
    };
    providers::EmbeddingJob* job = p.embed({ { "x" }, QString() });
    QSignalSpy failed(job, &providers::EmbeddingJob::failed);
    QVERIFY(failed.wait(5000));
    QVERIFY(failed.first().first().toString().contains("Jelentkezz be"));
    QCOMPARE(job->errorStatus(), 401);

    m_http->handler = [](const fakehttp::Request&) -> fakehttp::Reply { return { 200, R"({"data":[]})" }; };
    providers::EmbeddingJob* job2 = p.embed({ { "x", "y" }, QString() });
    QSignalSpy failed2(job2, &providers::EmbeddingJob::failed);
    QVERIFY(failed2.wait(5000));

    // Without a title no request goes out.
    providers::OpenAiCompatibleEmbeddingProvider none(ProviderConfig{});
    providers::EmbeddingJob* job3 = none.embed({ { "x" }, QString() });
    QSignalSpy failed3(job3, &providers::EmbeddingJob::failed);
    QVERIFY(failed3.wait(2000));
}

void EmbeddingTest::chunking()
{
    QVector<TranscriptLine> lines;
    for (int i = 0; i < 10; ++i) {
        TranscriptLine l;
        l.startMs = i * 1000; l.endMs = i * 1000 + 900;
        l.text = QString(400, QLatin1Char('x'));
        lines.append(l);
    }
    TranscriptLine empty; empty.text = "   ";
    lines.insert(3, empty);
    const QVector<library::EmbeddingChunk> chunks = library::EmbeddingIndex::chunkTranscript(lines, 1500);
    QCOMPARE(chunks.size(), 4);                      // 3 utterances (1202 chars) / chunk
    QCOMPARE(chunks.first().startMs, 0);
    QCOMPARE(chunks.first().endMs, 2900);
    QVERIFY(chunks.first().text.size() <= 1500);
    QCOMPARE(chunks.last().endMs, 9900);
    // A single over-long utterance is one chunk by itself.
    TranscriptLine big; big.text = QString(4000, QLatin1Char('y'));
    QCOMPARE(library::EmbeddingIndex::chunkTranscript({ big }, 1500).size(), 1);
}

void EmbeddingTest::indexFileRoundTrip()
{
    const QString path = m_dir->filePath("x.bin");
    QVector<library::EmbeddingChunk> in(2);
    in[0].startMs = 5; in[0].endMs = 10; in[0].vector = { 1.5f, -2.0f, 0.25f };
    in[1].startMs = 11; in[1].endMs = 20; in[1].vector = { 0.0f, 1.0f, 3.0f };
    QVERIFY(library::EmbeddingIndex::writeFile(path, QStringLiteral("bge-m3 ő"), in));
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.read(6), QByteArray("TNEMB1"));
    f.close();
    QString model;
    QVector<library::EmbeddingChunk> out;
    QVERIFY(library::EmbeddingIndex::readFile(path, &model, &out));
    QCOMPARE(model, QStringLiteral("bge-m3 ő"));
    QCOMPARE(out.size(), 2);
    QCOMPARE(out.at(1).startMs, 11);
    QCOMPARE(out.at(1).endMs, 20);
    QCOMPARE(out.at(0).vector, in.at(0).vector);
    // Vectors of different length / a corrupt file: rejected.
    in[1].vector = { 1.0f };
    QVERIFY(!library::EmbeddingIndex::writeFile(m_dir->filePath("y.bin"), "m", in));
    QFile bad(m_dir->filePath("bad.bin"));
    QVERIFY(bad.open(QIODevice::WriteOnly));
    bad.write("TNEMB1garbage");
    bad.close();
    QVERIFY(!library::EmbeddingIndex::readFile(m_dir->filePath("bad.bin"), &model, &out));
    // Mean: normalized.
    QVector<library::EmbeddingChunk> c(2);
    c[0].vector = { 2.0f, 0.0f }; c[1].vector = { 0.0f, 5.0f };
    const QVector<float> mean = library::EmbeddingIndex::meanVector(c);
    QVERIFY(qAbs(mean.at(0) - mean.at(1)) < 1e-6);
    QVERIFY(qAbs(double(mean.at(0)) * mean.at(0) + double(mean.at(1)) * mean.at(1) - 1.0) < 1e-6);
}

void EmbeddingTest::preparerProgressAndDone()
{
    const tagsfixture::Library lib = tagsfixture::buildLibrary(*m_store);
    tagsfixture::addMeeting(*m_store, "Átirat nélkül", QDate(2026, 9, 20), {}, {});
    library::EmbeddingIndex index(m_store.get());
    library::EmbeddingPreparer prep(m_store.get(), &index, m_dir->filePath("meta/embedding-state.json"));
    QSignalSpy changed(&prep, &library::EmbeddingPreparer::stateChanged);
    QVERIFY(!prep.isConfigured());
    prep.start();                                    // without a provider: does not start
    QCOMPARE(prep.state().status, library::EmbeddingState::Idle);

    prep.setProvider(providers::embeddingproviders::LocalId, QStringLiteral("teszt-embed"), factory());
    QCOMPARE(prep.state().total, 6);
    QCOMPARE(prep.state().prepared, 0);
    QVector<int> progress;
    connect(&prep, &library::EmbeddingPreparer::stateChanged, this, [&]() {
        if (prep.state().status == library::EmbeddingState::Running) progress << prep.state().prepared;
    });
    prep.start();
    QTRY_COMPARE_WITH_TIMEOUT(int(prep.state().status), int(library::EmbeddingState::Done), 10000);
    QCOMPARE(prep.state().prepared, 6);
    QVERIFY(prep.state().lastRun.isValid());
    QVERIFY(progress.contains(3));                   // per-step progress
    QVERIFY(index.has(lib.nordvik1));
    QVERIFY(QFile::exists(QDir(m_store->load(lib.museum1).folder).filePath("text.embeddings.bin")));
    // State file.
    QFile f(m_dir->filePath("meta/embedding-state.json"));
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QJsonObject st = QJsonDocument::fromJson(f.readAll()).object();
    QCOMPARE(st.value("model").toString(), QStringLiteral("teszt-embed"));
    QVERIFY(!st.value("lastRun").toString().isEmpty());
    // Restart: nothing to do, no request goes out.
    const int before = m_http->count("POST", "/embeddings");
    prep.start();
    QTRY_COMPARE(int(prep.state().status), int(library::EmbeddingState::Done));
    QCOMPARE(m_http->count("POST", "/embeddings"), before);
}

void EmbeddingTest::preparerErrorAndResume()
{
    tagsfixture::buildLibrary(*m_store);
    library::EmbeddingIndex index(m_store.get());
    library::EmbeddingPreparer prep(m_store.get(), &index, m_dir->filePath("meta/embedding-state.json"));
    prep.setProvider(providers::embeddingproviders::LocalId, QStringLiteral("teszt-embed"), factory());
    int calls = 0;
    m_http->handler = [&calls](const fakehttp::Request& r) -> fakehttp::Reply {
        if (++calls == 3) return { 500, R"({"error":{"message":"A modell nincs betöltve."}})" };
        return { 200, embeddingsReply(r.body) };
    };
    prep.start();
    QTRY_COMPARE_WITH_TIMEOUT(int(prep.state().status), int(library::EmbeddingState::Error), 10000);
    QCOMPARE(prep.state().prepared, 2);
    QVERIFY(prep.state().error.contains("nincs betöltve"));
    QVERIFY(!prep.state().lastRun.isValid());
    prep.resume();
    QTRY_COMPARE_WITH_TIMEOUT(int(prep.state().status), int(library::EmbeddingState::Done), 10000);
    QCOMPARE(prep.state().prepared, 6);
    QCOMPARE(calls, 7);                              // the two finished ones did not run again
}

void EmbeddingTest::preparerCancelAndResume()
{
    tagsfixture::buildLibrary(*m_store);
    library::EmbeddingIndex index(m_store.get());
    library::EmbeddingPreparer prep(m_store.get(), &index, QString());
    prep.setProvider(providers::embeddingproviders::LocalId, QStringLiteral("teszt-embed"), factory());
    bool hold = true;
    m_http->handler = [&hold](const fakehttp::Request& r) -> fakehttp::Reply {
        if (hold) return { 200, "{}", true };
        return { 200, embeddingsReply(r.body) };
    };
    prep.start();
    QTRY_VERIFY(m_http->count("POST", "/embeddings") >= 1);
    prep.cancel();
    QCOMPARE(prep.state().status, library::EmbeddingState::Idle);
    QCOMPARE(prep.state().prepared, 0);
    hold = false;
    prep.resume();
    QTRY_COMPARE_WITH_TIMEOUT(int(prep.state().status), int(library::EmbeddingState::Done), 10000);
    QCOMPARE(prep.state().prepared, 6);
}

void EmbeddingTest::modelChangeInvalidates()
{
    const tagsfixture::Library lib = tagsfixture::buildLibrary(*m_store);
    library::EmbeddingIndex index(m_store.get());
    QSignalSpy reset(&index, &library::EmbeddingIndex::indexReset);
    library::EmbeddingPreparer prep(m_store.get(), &index, m_dir->filePath("meta/embedding-state.json"));
    prep.setProvider(providers::embeddingproviders::LocalId, QStringLiteral("teszt-embed"), factory());
    prep.start();
    QTRY_COMPARE_WITH_TIMEOUT(int(prep.state().status), int(library::EmbeddingState::Done), 10000);
    QVERIFY(index.has(lib.budget1));

    // Another model: the old index is not valid (the file's model differs), "up to date" is cleared.
    prep.setProvider(providers::embeddingproviders::LocalId, QStringLiteral("masik-embed"), factory());
    QVERIFY(!index.has(lib.budget1));
    QCOMPARE(prep.state().prepared, 0);
    QVERIFY(!prep.state().lastRun.isValid());
    // invalidateAll: the files are deleted too.
    index.invalidateAll();
    QVERIFY(!QFile::exists(QDir(m_store->load(lib.budget1).folder).filePath("text.embeddings.bin")));
    QVERIFY(reset.count() >= 2);
    // A new transcript makes the index stale.
    prep.start();
    QTRY_COMPARE_WITH_TIMEOUT(int(prep.state().status), int(library::EmbeddingState::Done), 10000);
    QVERIFY(index.has(lib.budget1));
    QTest::qWait(20);
    tagsfixture::writeSegments(m_store->load(lib.budget1).folder, { "Új átirat a költségvetésről." });
    QVERIFY(!index.has(lib.budget1));
}

QTEST_GUILESS_MAIN(EmbeddingTest)
#include "test_embedding.moc"
