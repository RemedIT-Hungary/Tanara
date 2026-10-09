//
// AppController — a címkék bekötése: javaslat-kérés (Computing → Ready, gyorsítótár, szűrés
// felrakás / elutasítás után), együtt járó címkék, LLM-javaslat (kézi és az összefoglaló utáni
// automatikus) ál-HTTP-szerver ellen, beágyazó provider beállítása + modellváltás, és a
// beállítások perzisztenciája. Izolált TANARA_HOME; valódi hálózat / LM Studio nincs.
//
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QJsonArray>

#include "tanara/AppController.h"
#include "tanara/SettingsManager.h"
#include "tanara/embedding/EmbeddingIndex.h"
#include "tanara/embedding/EmbeddingPreparer.h"
#include "tanara/embedding/EmbeddingProviderRegistry.h"
#include "tanara/library/MeetingLibrary.h"
#include "tanara/store/JsonSerialization.h"
#include "tanara/store/MeetingStore.h"
#include "tanara/tags/MeetingProfiles.h"
#include "tanara/tags/TagService.h"
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
    std::unique_ptr<AppController> m_app;
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
    m_app = std::make_unique<AppController>();
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
    TagService* tags = m_app->tags();
    QVERIFY(tags && m_app->profiles() && m_app->embeddings() && m_app->embeddingPreparer());
    const Tag nordvik = tags->addTag(m_lib.nordvik1, QStringLiteral("Nordvik"));
    const Tag log = tags->addTag(m_lib.nordvik1, QStringLiteral("Logisztika"));

    QStringList order;
    connect(m_app.get(), &AppController::tagSuggestionsComputing, this, [&](const QString& id) { if (id == m_lib.nordvik2) order << "computing"; });
    connect(m_app.get(), &AppController::tagSuggestionsReady, this, [&](const QString& id) { if (id == m_lib.nordvik2) order << "ready"; });
    QSignalSpy ready(m_app.get(), &AppController::tagSuggestionsReady);
    m_app->requestTagSuggestions(m_lib.nordvik2);
    QVector<TagSuggestion> list = waitReady(ready, m_lib.nordvik2);
    QCOMPARE(order.value(0), QStringLiteral("computing"));
    QCOMPARE(order.value(1), QStringLiteral("ready"));
    QCOMPARE(list.size(), 2);
    QCOMPARE(m_app->pendingTagSuggestions(m_lib.nordvik2).size(), 2);

    // Gyorsítótárból: azonnal (ugyanabban a körben) jön.
    ready.clear();
    m_app->requestTagSuggestions(m_lib.nordvik2);
    QCOMPARE(ready.count(), 1);

    // Felrakás → a kiadott lista szűrve újra kimegy.
    ready.clear();
    tags->addTag(m_lib.nordvik2, nordvik.id, TagSource::Suggestion);
    QVERIFY(ready.count() >= 1);
    list = ready.last().at(1).value<QVector<TagSuggestion>>();
    QCOMPARE(list.size(), 1);
    QCOMPARE(list.first().tagId, log.id);
    // Elutasítás → kiesik.
    ready.clear();
    tags->reject(m_lib.nordvik2, list.first());
    QVERIFY(ready.count() >= 1);
    QVERIFY(m_app->pendingTagSuggestions(m_lib.nordvik2).isEmpty());

    // Kikapcsolt javaslatok: üres lista.
    AppSettings s = m_app->settings()->settings();
    s.tagSuggestions = false;
    m_app->settings()->setSettings(s);
    ready.clear();
    m_app->requestTagSuggestions(m_lib.nordvik1);
    QCOMPARE(ready.count(), 1);
    QVERIFY(ready.first().at(1).value<QVector<TagSuggestion>>().isEmpty());

    // Import / felvétel előtt: cím alapján.
    s.tagSuggestions = true;
    m_app->settings()->setSettings(s);
    const QVector<TagSuggestion> draft = m_app->draftTagSuggestions(QStringLiteral("Nordvik heti egyeztetés"));
    QVERIFY(!draft.isEmpty());
    QCOMPARE(draft.first().tagId, nordvik.id);
}

void TagsControllerTest::asyncCoalesceStaleDropAndCache()
{
    TagService* tags = m_app->tags();
    tags->addTag(m_lib.nordvik1, QStringLiteral("Nordvik"));
    QSignalSpy ready(m_app.get(), &AppController::tagSuggestionsReady);
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

    // Háttérszálon: a hívásban nem jön eredmény; az ismételt kérések egy számolásba olvadnak.
    m_app->requestTagSuggestions(m_lib.nordvik2);
    m_app->requestTagSuggestions(m_lib.nordvik2);
    m_app->requestTagSuggestions(m_lib.nordvik2);
    QCOMPARE(readyFor(m_lib.nordvik2), 0);
    QVector<TagSuggestion> list = waitReady(ready, m_lib.nordvik2);
    QCOMPARE(names(list), QStringList{ QStringLiteral("Nordvik") });
    QTest::qWait(150);
    QCOMPARE(readyFor(m_lib.nordvik2), 1);

    // Gyorsítótárból azonnal; a címkekészlet változása után újra háttérszálon számol.
    ready.clear();
    m_app->requestTagSuggestions(m_lib.nordvik2);
    QCOMPARE(ready.count(), 1);
    tags->addTag(m_lib.budget2, QStringLiteral("Belső"));
    ready.clear();
    m_app->requestTagSuggestions(m_lib.nordvik2);
    QCOMPARE(ready.count(), 0);
    QCOMPARE(names(waitReady(ready, m_lib.nordvik2)), QStringList{ QStringLiteral("Nordvik") });

    // Elavult eredmény: a számolás már elindult (a pillanatkép kész), amikor a szomszéd új címkét
    // kap → a régi állapotból számolt lista nem megy ki, az új állapotból újraszámolódik.
    tags->addTag(m_lib.budget1, QStringLiteral("Belső"));   // gyorsítótár-ürítés
    bool changed = false;
    // Az AppController idle-slotja (a számolás indítása) előbb fut, mint ez.
    const auto conn = connect(m_app->profiles(), &MeetingProfiles::idle, this, [&] {
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
    TagService* tags = m_app->tags();
    const Tag a = tags->create(QStringLiteral("Partnerek"));
    const Tag b = tags->create(QStringLiteral("Szerződés"));
    for (const QString& id : { m_lib.nordvik1, m_lib.budget1 }) { tags->addTag(id, a.id); tags->addTag(id, b.id); }
    QSignalSpy computing(m_app.get(), &AppController::tagSuggestionsComputing);
    QSignalSpy ready(m_app.get(), &AppController::tagSuggestionsReady);
    tags->addTag(m_lib.museum1, a.id);
    ready.clear();
    m_app->requestCooccurSuggestions(m_lib.museum1, a.id);
    QCOMPARE(computing.count(), 1);
    QCOMPARE(ready.count(), 1);
    const auto list = ready.first().at(1).value<QVector<TagSuggestion>>();
    QCOMPARE(list.size(), 1);
    QCOMPARE(list.first().tagId, b.id);
    QCOMPARE(list.first().source, SuggestionSource::Cooccur);
    // Nincs együtt járó: nincs jel.
    ready.clear();
    m_app->requestCooccurSuggestions(m_lib.museum1, b.id);
    QCOMPARE(ready.count(), 0);
}

void TagsControllerTest::llmSuggestionsManualAndAutomatic()
{
    TagService* tags = m_app->tags();
    // Összefoglaló nélkül nincs mit kérni.
    QSignalSpy ready(m_app.get(), &AppController::tagSuggestionsReady);
    m_app->requestLlmTagSuggestions(m_lib.museum2);
    QCOMPARE(ready.count(), 0);
    QCOMPARE(m_http->count("POST", "/chat/completions"), 0);

    const Meeting m = m_app->store()->load(m_lib.museum2);
    QFile md(QDir(m.folder).filePath("summary.md"));
    QVERIFY(md.open(QIODevice::WriteOnly));
    md.write("## Összefoglaló\nA MuseumPlus oktatásáról és a raktár költözéséről volt szó.\n");
    md.close();

    // Üres készletnél az összefoglaló utáni automatikus lépés nem fut.
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

    // Az új név elfogadása: létrejön a címke, a javaslat kiesik.
    ready.clear();
    tags->addTag(m_lib.museum2, list.at(1).name, TagSource::Llm);
    QVERIFY(tags->byName(QStringLiteral("Raktárköltözés")).isValid());
    for (const TagSuggestion& s : m_app->pendingTagSuggestions(m_lib.museum2)) QVERIFY(!s.isNew);

    // Automatikus: az összefoglaló elkészülte után (van már címkekészlet).
    ready.clear();
    emit m_app->summaryReady(m_lib.museum2, md.fileName());
    QTRY_COMPARE(m_http->count("POST", "/chat/completions"), 2);
    QVERIFY(!waitReady(ready, m_lib.museum2).isEmpty() || ready.count() >= 1);

    // Kikapcsolva nem fut.
    AppSettings s = m_app->settings()->settings();
    s.llmTagSuggestions = false;
    m_app->settings()->setSettings(s);
    emit m_app->summaryReady(m_lib.museum2, md.fileName());
    QTest::qWait(50);
    QCOMPARE(m_http->count("POST", "/chat/completions"), 2);

    // Hibás modell-válasz: nincs felugró hiba, a korábbi lista marad.
    QSignalSpy errors(m_app.get(), &AppController::errorOccurred);
    m_http->handler = [](const fakehttp::Request&) -> fakehttp::Reply { return { 500, R"({"error":"x"})" }; };
    const int before = int(m_app->pendingTagSuggestions(m_lib.museum2).size());
    ready.clear();
    m_app->requestLlmTagSuggestions(m_lib.museum2);
    QVERIFY(!waitReady(ready, m_lib.museum2).isEmpty() || before == 0);
    QCOMPARE(errors.count(), 0);
}

void TagsControllerTest::embeddingProviderAndModelChange()
{
    EmbeddingPreparer* prep = m_app->embeddingPreparer();
    QCOMPARE(prep->state().provider, QString());       // alap: nincs beágyazás
    ProviderConfig cfg;
    cfg.baseUrl = m_http->base();
    cfg.model = QStringLiteral("embed-a");
    m_app->setEmbeddingProvider(embeddingproviders::LocalId, cfg);
    QCOMPARE(m_app->settings()->settings().embeddingProviderId, embeddingproviders::LocalId);
    QTRY_COMPARE_WITH_TIMEOUT(int(prep->state().status), int(EmbeddingState::Done), 10000);
    QCOMPARE(prep->state().prepared, 6);
    const QString file = QDir(m_app->store()->load(m_lib.budget1).folder).filePath("text.embeddings.bin");
    QVERIFY(QFile::exists(file));
    QVERIFY(m_app->embeddings()->has(m_lib.budget1));
    const int calls = m_http->count("POST", "/embeddings");

    // Más beállítás változása nem indítja újra.
    AppSettings s = m_app->settings()->settings();
    s.summaryLanguage = QStringLiteral("angol");
    m_app->settings()->setSettings(s);
    QCOMPARE(m_http->count("POST", "/embeddings"), calls);

    // Modellváltás: az index eldobva, az előkészítés elölről.
    QSignalSpy reset(m_app->embeddings(), &EmbeddingIndex::indexReset);
    cfg.model = QStringLiteral("embed-b");
    m_app->setEmbeddingProvider(embeddingproviders::LocalId, cfg);
    QVERIFY(reset.count() >= 1);
    QTRY_COMPARE_WITH_TIMEOUT(int(prep->state().status), int(EmbeddingState::Done), 10000);
    QCOMPARE(prep->state().model, QStringLiteral("embed-b"));
    QCOMPARE(m_http->count("POST", "/embeddings"), calls + 6);
    QCOMPARE(m_http->lastBody("/embeddings").value("model").toString(), QStringLiteral("embed-b"));

    // Új átirat → háttérben beágyazódik.
    QTest::qWait(20);
    tagsfixture::writeSegments(m_app->store()->load(m_lib.budget1).folder, { "Új átirat a költségvetésről." });
    emit m_app->transcriptReady(m_lib.budget1, QString());
    QTRY_COMPARE_WITH_TIMEOUT(m_http->count("POST", "/embeddings"), calls + 7, 5000);
    QTRY_VERIFY(m_app->embeddings()->has(m_lib.budget1));

    // Kikapcsolás: nincs beágyazás.
    m_app->setEmbeddingProvider(QString(), {});
    QVERIFY(!m_app->embeddings()->has(m_lib.budget1));
    QCOMPARE(prep->state().status, EmbeddingState::Idle);
}

void TagsControllerTest::settingsRoundTrip()
{
    AppSettings s = m_app->settings()->settings();
    QVERIFY(s.tagSuggestions);
    QVERIFY(s.llmTagSuggestions);
    QVERIFY(s.embeddingProviderId.isEmpty());
    QVERIFY(s.embeddingConfigs.contains(embeddingproviders::LocalId));   // előre kitöltve
    s.tagSuggestions = false;
    s.llmTagSuggestions = false;
    s.embeddingProviderId = embeddingproviders::CloudId;
    s.embeddingConfigs[embeddingproviders::CloudId].model = QStringLiteral("x");
    m_app->settings()->setSettings(s);
    SettingsManager again(m_app->settings()->settings().metadataDir);
    const AppSettings r = again.settings();
    QVERIFY(!r.tagSuggestions);
    QVERIFY(!r.llmTagSuggestions);
    QCOMPARE(r.embeddingProviderId, embeddingproviders::CloudId);
    QCOMPARE(r.embeddingSelected().model, QStringLiteral("x"));
    QVERIFY(!toJson(r).contains("apiKey"));
}

QTEST_GUILESS_MAIN(TagsControllerTest)
#include "test_tags_controller.moc"
