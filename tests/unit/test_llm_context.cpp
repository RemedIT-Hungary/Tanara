//
// LLM kontextus-kezelés: a „nem fér a kontextusba” hiba besorolása (LM Studio / llama.cpp /
// OpenAI alak), a token-becslés és a lépcsők, az LM Studio natív API-jának értelmezése, a
// betöltés előtti döntés, és az előkészítő (LlmModelPreparer) + a kapcsolat-teszt egy helyi
// ál-szerveren. Valódi LM Studióhoz SEMMILYEN kérés nem megy.
//
#include <QtTest>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <functional>

#include "tanara/jobs/JobErrors.h"
#include "tanara/cloud/CloudTypes.h"
#include "tanara/llm/LlmContext.h"
#include "tanara/llm/LlmServer.h"
#include "tanara/provider/ConnectionTester.h"

using namespace tanara;
using namespace tanara::llmctx;

#ifndef Q_MOC_RUN
namespace {

struct FakeRequest { QByteArray method; QString path; QByteArray body; };
struct FakeReply { int status = 200; QByteArray body = "{}"; bool hold = false; };

// Minimális ál-HTTP-szerver: minden kérést naplóz, a választ a handler adja.
class FakeHttp : public QObject {
public:
    std::function<FakeReply(const FakeRequest&)> handler;
    QVector<FakeRequest> log;

    FakeHttp() {
        connect(&m_srv, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket* s = m_srv.nextPendingConnection()) {
                auto buf = std::make_shared<QByteArray>();
                connect(s, &QTcpSocket::readyRead, this, [this, s, buf]() { feed(s, *buf); });
                connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            }
        });
        m_srv.listen(QHostAddress::LocalHost);
    }
    QString base() const { return QStringLiteral("http://127.0.0.1:%1/v1").arg(m_srv.serverPort()); }
    int count(const QByteArray& method, const QString& pathPart) const {
        int n = 0;
        for (const FakeRequest& r : log)
            if (r.method == method && r.path.contains(pathPart)) ++n;
        return n;
    }
    QJsonObject lastBody(const QString& pathPart) const {
        for (int i = log.size() - 1; i >= 0; --i)
            if (log[i].path.contains(pathPart)) return QJsonDocument::fromJson(log[i].body).object();
        return {};
    }

private:
    void feed(QTcpSocket* s, QByteArray& buf) {
        buf += s->readAll();
        const int headEnd = buf.indexOf("\r\n\r\n");
        if (headEnd < 0) return;
        const QByteArray head = buf.left(headEnd);
        qint64 len = 0;
        for (const QByteArray& line : head.split('\n')) {
            const QByteArray l = line.trimmed().toLower();
            if (l.startsWith("content-length:")) len = l.mid(15).trimmed().toLongLong();
        }
        if (buf.size() < headEnd + 4 + len) return;
        FakeRequest req;
        const QList<QByteArray> first = head.left(head.indexOf('\r')).split(' ');
        req.method = first.value(0);
        req.path = QString::fromUtf8(first.value(1));
        req.body = buf.mid(headEnd + 4, len);
        buf.clear();
        log.append(req);
        const FakeReply rep = handler ? handler(req) : FakeReply{};
        if (rep.hold) return;
        s->write("HTTP/1.1 " + QByteArray::number(rep.status) + " X\r\nContent-Type: application/json\r\n"
                 "Content-Length: " + QByteArray::number(rep.body.size()) + "\r\nConnection: close\r\n\r\n"
                 + rep.body);
        s->disconnectFromHost();
    }
    QTcpServer m_srv;
};

// A GET /api/v1/models válasz (a valódi LM Studio alakja), egy modell-példánnyal vagy anélkül.
QByteArray modelsJson(const QString& key, int ctx, int parallel, const QString& otherLoaded = QString())
{
    QJsonArray models;
    QJsonArray inst;
    if (ctx > 0)
        inst.append(QJsonObject{{"id", key}, {"config", QJsonObject{{"context_length", ctx},
                                                                   {"parallel", parallel},
                                                                   {"eval_batch_size", 2048}}}});
    models.append(QJsonObject{{"type", "llm"}, {"key", key}, {"display_name", key},
                              {"loaded_instances", inst}, {"max_context_length", 262144}});
    models.append(QJsonObject{{"type", "llm"}, {"key", "google/gemma-4-12b"},
                              {"loaded_instances", QJsonArray{}}, {"max_context_length", 131072}});
    if (!otherLoaded.isEmpty())
        models.append(QJsonObject{{"type", "llm"}, {"key", otherLoaded},
                                  {"loaded_instances", QJsonArray{QJsonObject{{"id", otherLoaded},
                                      {"config", QJsonObject{{"context_length", 8192}, {"parallel", 1}}}}}},
                                  {"max_context_length", 40960}});
    models.append(QJsonObject{{"type", "embedding"}, {"key", "text-embedding-bge-m3"},
                              {"loaded_instances", QJsonArray{QJsonObject{{"id", "text-embedding-bge-m3"}}}},
                              {"max_context_length", 8192}});
    return QJsonDocument(QJsonObject{{"models", models}}).toJson(QJsonDocument::Compact);
}

// Az incidens: LM Studio a llama.cpp hibáját a saját üzenetébe ágyazva adja vissza.
const char* kLmStudioIncident =
    R"({"error":"Engine protocol predict request returned 400: {\"error\":{\"code\":400,\"message\":\"request (6042 tokens) exceeds the available context size (4096 tokens), try increasing it\",\"type\":\"exceed_context_size_error\",\"n_prompt_tokens\":6042,\"n_ctx\":4096}}"})";
const char* kLlamaCpp =
    R"({"error":{"code":400,"message":"request (6042 tokens) exceeds the available context size (4096 tokens), try increasing it","type":"exceed_context_size_error","n_prompt_tokens":6042,"n_ctx":4096}})";
const char* kOpenAi =
    "{\"error\":{\"message\":\"This model's maximum context length is 8192 tokens. However, your messages resulted in 9000 tokens. Please reduce the length of the messages.\",\"type\":\"invalid_request_error\",\"param\":\"messages\",\"code\":\"context_length_exceeded\"}}";

ProviderConfig cfgFor(const FakeHttp& srv, const QString& model = QStringLiteral("qwen3.8-27b"))
{
    ProviderConfig cfg;
    cfg.type = QStringLiteral("openai-compat");
    cfg.baseUrl = srv.base();
    cfg.model = model;
    return cfg;
}

} // namespace
#endif

class LlmContextTest : public QObject {
    Q_OBJECT
private slots:
    void init() { LlmServerProbe::invalidateCache(); }

    void estimateAndSteps();
    void overflowShapes_data();
    void overflowShapes();
    void classifiedMessage();
    void cloudOverflowHasNoReload();
    void probeParsing();
    void decisionTable();
    void preparerAlreadyFine();
    void preparerReloadsSmallContext();
    void preparerReloadsParallel();
    void preparerLoadsWhenNotLoaded();
    void preparerLoadRefusedOnce();
    void preparerBusyDoesNotUnload();
    void preparerCancelDuringLoad();
    void preparerSkipsWithoutNativeApi();
    void connectionTestReportsServer();
};

#ifndef Q_MOC_RUN

void LlmContextTest::estimateAndSteps()
{
    QCOMPARE(estimateTokens(0), 0);
    QCOMPARE(estimateTokens(2500), 1000);           // 2,5 karakter / token (magyar átirat)
    QCOMPARE(estimateTokens(2501), 1001);
    // Bemenet + sablon, 10% ráhagyással, + a kimeneti keret.
    QCOMPARE(callContextNeed(2500, 3000), int(std::ceil((1000 + 256) * 1.1)) + 3000);
    QCOMPARE(contextStepFor(5000), 8192);
    QCOMPARE(contextStepFor(9000), 16384);
    QCOMPARE(contextStepFor(17000), 20480);
    QCOMPARE(contextStepFor(30000), 32768);
    QCOMPARE(contextStepFor(30000, 20000), 20000);   // a modell maximuma fölé nem
    QCOMPARE(contextStepFor(300000), 300032);        // minden lépcső fölött: 1024-re kerekítve
    // A keret visszafelé: ennyi karakter fér egy részbe a prompt + kimenet mellett.
    const int budget = partBudgetChars(16384, 4000, 3000);
    QVERIFY(budget > 0);
    QVERIFY(callContextNeed(4000 + budget, 3000) <= 16384);
    QVERIFY(callContextNeed(4000 + budget + 100, 3000) > 16384 - 100);
    QCOMPARE(partBudgetChars(4096, 4000, 3000), -1);   // a legkisebb részhez is kevés
    QVERIFY(typicalSummaryPartNeed() > 8192 && typicalSummaryPartNeed() < 16384);
    QCOMPARE(nativeApiRoot(QStringLiteral("http://localhost:1234/v1/")), QStringLiteral("http://localhost:1234"));
    QCOMPARE(nativeApiRoot(QStringLiteral("http://host:8080")), QStringLiteral("http://host:8080"));
}

void LlmContextTest::overflowShapes_data()
{
    QTest::addColumn<QByteArray>("body");
    QTest::addColumn<QString>("text");
    QTest::addColumn<bool>("matched");
    QTest::addColumn<int>("prompt");
    QTest::addColumn<int>("ctx");
    QTest::addColumn<QString>("code");
    QTest::newRow("lm studio (beágyazott)") << QByteArray(kLmStudioIncident) << QString() << true << 6042 << 4096
                                           << "exceed_context_size_error";
    QTest::newRow("llama.cpp") << QByteArray(kLlamaCpp) << QString() << true << 6042 << 4096
                               << "exceed_context_size_error";
    QTest::newRow("openai") << QByteArray(kOpenAi) << QString() << true << 9000 << 8192
                            << "context_length_exceeded";
    QTest::newRow("csak a hibaszöveg (incidens)") << QByteArray()
        << QStringLiteral("LLM hiba (HTTP 400): Engine protocol predict request returned 400: {\"error\":{\"code\":400,"
                          "\"message\":\"request (6042 tokens) exceeds the available context size (4096 tokens), try "
                          "increasing it\",\"type\":\"exceed_context_size_error\",\"n_prompt_tokens\":6042,\"n_ctx\":4096}}")
        << true << 6042 << 4096 << "exceed_context_size_error";
    QTest::newRow("vllm szöveg") << QByteArray("{\"object\":\"error\",\"message\":\"This model's maximum context length is 4096 tokens. However, you requested 5000 tokens (1000 in the messages, 4000 in the completion).\",\"type\":\"BadRequestError\",\"code\":400}")
        << QString() << true << 5000 << 4096 << QString();
    QTest::newRow("számok nélkül") << QByteArray(R"({"error":{"code":"context_length_exceeded","message":"too long"}})")
        << QString() << true << -1 << -1 << "context_length_exceeded";
    QTest::newRow("más hiba") << QByteArray(R"({"error":{"message":"model crashed","type":"server_error"}})")
        << QString() << false << -1 << -1 << QString();
}

void LlmContextTest::overflowShapes()
{
    QFETCH(QByteArray, body);
    QFETCH(QString, text);
    QFETCH(bool, matched);
    QFETCH(int, prompt);
    QFETCH(int, ctx);
    QFETCH(QString, code);
    const ContextOverflow o = parseContextOverflow(body, text);
    QCOMPARE(o.matched, matched);
    QCOMPARE(o.promptTokens, prompt);
    QCOMPARE(o.contextTokens, ctx);
    if (!code.isEmpty()) QCOMPARE(o.code, code);
}

void LlmContextTest::classifiedMessage()
{
    // Az incidens a szolgáltató-hibák útján: emberi üzenet a két számmal, nyers JSON nélkül,
    // a technikai sor más (nem ugyanaz a szöveg kétszer).
    for (const char* body : {kLmStudioIncident, kLlamaCpp}) {
        HttpExchange ex;
        ex.status = 400;
        ex.body = body;
        const QString raw = QStringLiteral("LLM hiba (HTTP 400): Engine protocol predict request returned 400: {…}");
        ContextFailureHint hint;
        hint.lmStudio = true;
        hint.recommendedContext = 12000;
        const JobError e = describeJobFailure(JobKind::Summarize, raw, &ex, &hint);
        QVERIFY2(e.message.contains(QStringLiteral("4096")) || e.message.contains(QStringLiteral("4 096"))
                     || e.message.contains(QStringLiteral("4,096")), qPrintable(e.message));
        QVERIFY2(e.message.contains(QStringLiteral("6042")) || e.message.contains(QStringLiteral("6 042"))
                     || e.message.contains(QStringLiteral("6,042")), qPrintable(e.message));
        QVERIFY(!e.message.contains(QLatin1Char('{')));
        QVERIFY(!e.detail.contains(QLatin1Char('{')));
        QVERIFY(e.detail.startsWith(QStringLiteral("HTTP 400 · exceed_context_size_error")));
        QVERIFY(!e.detail.contains(QStringLiteral("Engine protocol")));
        QCOMPARE(e.fixActionHint, QStringLiteral("llm:reload-context:16384"));
        QVERIFY(isReloadContextHint(e.fixActionHint));
        QCOMPARE(contextFixTokens(e.fixActionHint), 16384);
    }
    // Más szerver (nincs natív API): a minimum a szövegben, a tipp a Beállításokba visz.
    HttpExchange ex;
    ex.status = 400;
    ex.body = kOpenAi;
    ContextFailureHint hint;
    hint.recommendedContext = 30000;
    const JobError e = describeJobFailure(JobKind::AnalyzeTopics, QStringLiteral("x"), &ex, &hint);
    QCOMPARE(e.fixActionHint, QStringLiteral("settings:llm-context:32768"));
    QVERIFY(!isReloadContextHint(e.fixActionHint));
    QVERIFY(e.detail.contains(QStringLiteral("context_length_exceeded")));
    // Ha a becslés a betöltöttnél nem nagyobb, a következő lépcsőt ajánljuk.
    hint.recommendedContext = 4000;
    QCOMPARE(describeJobFailure(JobKind::Summarize, QString(), &ex, &hint).fixActionHint,
             QStringLiteral("settings:llm-context:16384"));
    // Az átírás hibáit nem soroljuk ide.
    QVERIFY(describeJobFailure(JobKind::Transcribe, QString(), &ex).fixActionHint != QStringLiteral("settings:llm-context"));
}

void LlmContextTest::cloudOverflowHasNoReload()
{
    CloudError ce;
    ce.kind = CloudErrorKind::Internal;
    ce.httpStatus = 400;
    ce.code = QStringLiteral("context_length_exceeded");
    ce.message = QStringLiteral("A kérés túl hosszú.");
    ce.requestId = QStringLiteral("req_1");
    const JobError e = describeCloudFailure(JobKind::Summarize, ce);
    QCOMPARE(e.fixActionHint, QStringLiteral("cloud"));
    QVERIFY(e.detail.contains(QStringLiteral("req_1")));
    QVERIFY(!isReloadContextHint(e.fixActionHint));
}

void LlmContextTest::probeParsing()
{
    // Betöltve (a valódi szerver alakja).
    LlmServerInfo i = parseLmStudioModels(modelsJson("qwen3.8-27b", 20480, 1), "qwen3.8-27b");
    QVERIFY(i.isLmStudio());
    QVERIFY(i.modelListed);
    QCOMPARE(i.modelKey, QStringLiteral("qwen3.8-27b"));
    QCOMPARE(i.maxContext, 262144);
    QCOMPARE(i.instances.size(), 1);
    QCOMPARE(i.instances[0].contextLength, 20480);
    QCOMPARE(i.instances[0].parallel, 1);
    QVERIFY(i.otherLoaded.isEmpty());   // a beágyazó (embedding) modell nem számít

    // Nincs betöltve.
    i = parseLmStudioModels(modelsJson("qwen3.8-27b", 0, 0), "qwen3.8-27b");
    QVERIFY(i.isLmStudio() && i.modelListed && !i.loaded());

    // Más modell foglalja a szervert.
    i = parseLmStudioModels(modelsJson("qwen3.8-27b", 0, 0, "racka-4b@q8_0"), "qwen3.8-27b");
    QVERIFY(!i.loaded());
    QCOMPARE(i.otherLoaded, QStringList{QStringLiteral("racka-4b@q8_0")});

    // A beállított modell nincs a listán.
    i = parseLmStudioModels(modelsJson("qwen3.8-27b", 20480, 1), "valami-mas");
    QVERIFY(i.isLmStudio() && !i.modelListed);
    QCOMPARE(i.otherLoaded, QStringList{QStringLiteral("qwen3.8-27b")});

    // Nincs natív API: OpenAI-lista, Soniox-szerű lista, hibatörzs, üres.
    QVERIFY(!parseLmStudioModels(R"({"data":[{"id":"x"}]})", "x").isLmStudio());
    QVERIFY(!parseLmStudioModels(R"({"models":[{"id":"stt-async"}]})", "x").isLmStudio());
    QVERIFY(!parseLmStudioModels(R"({"error":"Unexpected endpoint"})", "x").isLmStudio());
    QVERIFY(!parseLmStudioModels(QByteArray(), "x").isLmStudio());
}

void LlmContextTest::decisionTable()
{
    using A = PreloadDecision::Action;
    const auto info = [](int ctx, int parallel) {
        return parseLmStudioModels(modelsJson("m", ctx, parallel), "m");
    };
    // Már jó: nincs betöltés.
    PreloadDecision d = decidePreload(info(20480, 1), 15000, 0);
    QCOMPARE(d.action, A::None);
    QCOMPARE(d.contextLength, 20480);
    // Kicsi a kontextus → újratöltés a legkisebb elég lépcsővel.
    d = decidePreload(info(4096, 1), 15000, 0);
    QCOMPARE(d.action, A::Reload);
    QCOMPARE(d.contextLength, 16384);
    QCOMPARE(d.unloadIds, QStringList{QStringLiteral("m")});
    QCOMPARE(d.reason, QStringLiteral("context-small"));
    // parallel 4 (az incidens): újratöltés akkor is, ha a kontextus elég — egy szálon.
    d = decidePreload(info(32768, 4), 15000, 0);
    QCOMPARE(d.action, A::Reload);
    QCOMPARE(d.reason, QStringLiteral("parallel"));
    QCOMPARE(d.contextLength, 32768);    // automatikus módban a nagyobb, meglévő marad
    d = decidePreload(info(4096, 4), 6000, 0);
    QCOMPARE(d.action, A::Reload);
    QCOMPARE(d.contextLength, 8192);
    // Nincs betöltve → betöltés.
    d = decidePreload(info(0, 0), 9000, 0);
    QCOMPARE(d.action, A::Load);
    QCOMPARE(d.contextLength, 16384);
    QVERIFY(d.unloadIds.isEmpty());
    // Rögzített beállítás: azzal töltünk; ha kisebb az igénynél, jelezzük.
    d = decidePreload(info(0, 0), 30000, 20480);
    QCOMPARE(d.action, A::Load);
    QCOMPARE(d.contextLength, 20480);
    QVERIFY(d.contextInsufficient);
    // Rögzített, és a betöltött már ekkora: nem töltünk újra (úgysem lenne nagyobb).
    d = decidePreload(info(20480, 1), 30000, 20480);
    QCOMPARE(d.action, A::None);
    // A kért minimum („Betöltés nagyobb kontextussal”) a betöltött fölött → újratöltés.
    d = decidePreload(info(16384, 1), 9000, 0, 30000);
    QCOMPARE(d.action, A::Reload);
    QCOMPARE(d.contextLength, 32768);
    // A modell maximuma fölé nem kérünk.
    LlmServerInfo small = info(0, 0);
    small.maxContext = 12000;
    d = decidePreload(small, 30000, 0);
    QCOMPARE(d.contextLength, 12000);
    QVERIFY(d.contextInsufficient);
    // Nem LM Studio / ismeretlen modell: nincs teendő.
    QCOMPARE(decidePreload(LlmServerInfo{}, 9000, 0).action, A::Skip);
    QCOMPARE(decidePreload(parseLmStudioModels(modelsJson("m", 4096, 1), "x"), 9000, 0).action, A::Skip);
}

void LlmContextTest::preparerAlreadyFine()
{
    FakeHttp srv;
    srv.handler = [](const FakeRequest& r) -> FakeReply {
        if (r.path == "/api/v1/models") return {200, modelsJson("qwen3.8-27b", 20480, 1)};
        return {500, "{}"};
    };
    LlmModelPreparer p(cfgFor(srv));
    QSignalSpy ready(&p, &LlmModelPreparer::ready);
    QSignalSpy failed(&p, &LlmModelPreparer::failed);
    QSignalSpy loading(&p, &LlmModelPreparer::loadingStarted);
    p.start(15000);
    QTRY_COMPARE(ready.count(), 1);
    QCOMPARE(failed.count(), 0);
    QCOMPARE(loading.count(), 0);
    QCOMPARE(srv.log.size(), 1);                 // csak a GET — se kivétel, se betöltés
    QCOMPARE(p.loadedContext(), 20480);
    QVERIFY(!p.didLoad());
}

void LlmContextTest::preparerReloadsSmallContext()
{
    FakeHttp srv;
    srv.handler = [](const FakeRequest& r) -> FakeReply {
        if (r.path == "/api/v1/models") return {200, modelsJson("qwen3.8-27b", 4096, 1)};
        if (r.path.endsWith("/unload")) return {200, R"({"instance_id":"qwen3.8-27b"})"};
        if (r.path.endsWith("/load"))
            return {200, R"({"type":"llm","instance_id":"qwen3.8-27b","status":"loaded","load_config":{"context_length":16384}})"};
        return {404, "{}"};
    };
    LlmModelPreparer p(cfgFor(srv));
    QSignalSpy ready(&p, &LlmModelPreparer::ready);
    QSignalSpy loading(&p, &LlmModelPreparer::loadingStarted);
    p.start(12000);
    QTRY_COMPARE(ready.count(), 1);
    QCOMPARE(loading.count(), 1);
    QCOMPARE(loading.at(0).at(0).toInt(), 16384);
    // Sorrend: lekérdezés → kivétel → betöltés.
    QCOMPARE(srv.log.size(), 3);
    QCOMPARE(srv.log[1].path, QStringLiteral("/api/v1/models/unload"));
    QCOMPARE(QJsonDocument::fromJson(srv.log[1].body).object().value("instance_id").toString(),
             QStringLiteral("qwen3.8-27b"));
    QCOMPARE(srv.log[2].path, QStringLiteral("/api/v1/models/load"));
    const QJsonObject load = QJsonDocument::fromJson(srv.log[2].body).object();
    QCOMPARE(load.value("model").toString(), QStringLiteral("qwen3.8-27b"));
    QCOMPARE(load.value("context_length").toInt(), 16384);
    QCOMPARE(load.value("parallel").toInt(), 1);
    QCOMPARE(load.value("echo_load_config").toBool(), true);
    QCOMPARE(p.loadedContext(), 16384);
    QVERIFY(p.didLoad());
}

void LlmContextTest::preparerReloadsParallel()
{
    FakeHttp srv;
    srv.handler = [](const FakeRequest& r) -> FakeReply {
        if (r.path == "/api/v1/models") return {200, modelsJson("qwen3.8-27b", 4096, 4)};
        if (r.path.endsWith("/unload")) return {404, R"({"error":{"type":"model_not_found"}})"};   // már nincs: rendben
        if (r.path.endsWith("/load")) return {200, R"({"instance_id":"qwen3.8-27b","status":"loaded"})"};
        return {404, "{}"};
    };
    LlmModelPreparer p(cfgFor(srv));
    QSignalSpy ready(&p, &LlmModelPreparer::ready);
    p.start(6000);
    QTRY_COMPARE(ready.count(), 1);
    QCOMPARE(srv.count("POST", "/unload"), 1);
    QCOMPARE(srv.count("POST", "/load"), 1);
    QCOMPARE(srv.lastBody("/load").value("parallel").toInt(), 1);
    QCOMPARE(srv.lastBody("/load").value("context_length").toInt(), 8192);
    QCOMPARE(p.loadedContext(), 8192);   // load_config nélkül a kért érték
}

void LlmContextTest::preparerLoadsWhenNotLoaded()
{
    FakeHttp srv;
    srv.handler = [](const FakeRequest& r) -> FakeReply {
        if (r.path == "/api/v1/models") return {200, modelsJson("qwen3.8-27b", 0, 0, "racka-4b@q8_0")};
        if (r.path.endsWith("/load")) return {200, R"({"instance_id":"qwen3.8-27b","load_config":{"context_length":20480}})"};
        return {500, "{}"};
    };
    LlmModelPreparer p(cfgFor(srv));
    QSignalSpy ready(&p, &LlmModelPreparer::ready);
    p.start(17000);
    QTRY_COMPARE(ready.count(), 1);
    QCOMPARE(srv.count("POST", "/unload"), 0);   // a más modellhez nem nyúlunk
    QCOMPARE(srv.count("POST", "/load"), 1);
    QCOMPARE(srv.lastBody("/load").value("context_length").toInt(), 20480);
    QCOMPARE(p.serverInfo().otherLoaded, QStringList{QStringLiteral("racka-4b@q8_0")});
}

void LlmContextTest::preparerLoadRefusedOnce()
{
    FakeHttp srv;
    srv.handler = [](const FakeRequest& r) -> FakeReply {
        if (r.path == "/api/v1/models") return {200, modelsJson("qwen3.8-27b", 0, 0, "racka-4b@q8_0")};
        if (r.path.endsWith("/load"))
            return {500, R"({"error":{"type":"insufficient_resources","message":"Model loading was stopped due to insufficient system resources."}})"};
        return {500, "{}"};
    };
    LlmModelPreparer p(cfgFor(srv));
    QSignalSpy ready(&p, &LlmModelPreparer::ready);
    QSignalSpy failed(&p, &LlmModelPreparer::failed);
    p.start(60000);
    QTRY_COMPARE(failed.count(), 1);
    QTest::qWait(100);
    QCOMPARE(ready.count(), 0);
    QCOMPARE(srv.count("POST", "/load"), 1);     // egyetlen kísérlet
    const JobError e = failed.at(0).at(0).value<JobError>();
    QVERIFY2(e.message.contains(QStringLiteral("nem fér a videókártyára")), qPrintable(e.message));
    QVERIFY2(e.message.contains(QStringLiteral("racka-4b@q8_0")), qPrintable(e.message));
    QVERIFY(e.detail.contains(QStringLiteral("HTTP 500")));
    QVERIFY(e.detail.contains(QStringLiteral("insufficient")));
    QCOMPARE(e.fixActionHint, QStringLiteral("settings:llm-context"));
    // Ugyanaz az előkészítő nem indul újra (nincs ismétlés).
    p.start(60000);
    QTest::qWait(100);
    QCOMPARE(srv.count("POST", "/load"), 1);
}

void LlmContextTest::preparerBusyDoesNotUnload()
{
    FakeHttp srv;
    srv.handler = [](const FakeRequest& r) -> FakeReply {
        if (r.path == "/api/v1/models") return {200, modelsJson("qwen3.8-27b", 4096, 1)};
        return {200, "{}"};
    };
    LlmModelPreparer p(cfgFor(srv));
    p.setBusyCheck([] { return true; });   // a Tanara épp egy másik kérést futtat
    QSignalSpy failed(&p, &LlmModelPreparer::failed);
    p.start(12000);
    QTRY_COMPARE(failed.count(), 1);
    QCOMPARE(srv.count("POST", "/api/v1/models/"), 0);
    QVERIFY(failed.at(0).at(0).value<JobError>().message.contains(QStringLiteral("másik kérést")));
}

void LlmContextTest::preparerCancelDuringLoad()
{
    FakeHttp srv;
    srv.handler = [](const FakeRequest& r) -> FakeReply {
        if (r.path == "/api/v1/models") return {200, modelsJson("qwen3.8-27b", 0, 0)};
        FakeReply h; h.hold = true; return h;   // a betöltés „tart”
    };
    LlmModelPreparer p(cfgFor(srv));
    QSignalSpy ready(&p, &LlmModelPreparer::ready);
    QSignalSpy failed(&p, &LlmModelPreparer::failed);
    QSignalSpy loading(&p, &LlmModelPreparer::loadingStarted);
    p.start(9000);
    QTRY_COMPARE(srv.count("POST", "/load"), 1);
    QCOMPARE(loading.count(), 1);
    p.cancel();
    QTest::qWait(200);
    QCOMPARE(ready.count(), 0);
    QCOMPARE(failed.count(), 0);
}

void LlmContextTest::preparerSkipsWithoutNativeApi()
{
    FakeHttp srv;
    srv.handler = [](const FakeRequest&) -> FakeReply {
        return {404, R"({"error":"Unexpected endpoint or method."})"};
    };
    LlmModelPreparer p(cfgFor(srv));
    QSignalSpy ready(&p, &LlmModelPreparer::ready);
    p.start(60000);
    QTRY_COMPARE(ready.count(), 1);
    QVERIFY(!p.serverInfo().isLmStudio());
    QCOMPARE(p.loadedContext(), -1);
    QCOMPARE(srv.count("POST", ""), 0);
    // A sikertelen próba is gyorsítótárba kerül: rövid időn belül nem kérdezünk újra.
    LlmModelPreparer p2(cfgFor(srv));
    QSignalSpy ready2(&p2, &LlmModelPreparer::ready);
    p2.start(60000);
    QTRY_COMPARE(ready2.count(), 1);
    QCOMPARE(srv.log.size(), 1);
}

void LlmContextTest::connectionTestReportsServer()
{
    // Tiszta leírás: parallel > 1 és kevés kontextus → figyelmeztetés.
    QString text, warning;
    describeLlmServer(parseLmStudioModels(modelsJson("m", 4096, 4), "m"), &text, &warning);
    QVERIFY2(text.contains(QStringLiteral("LM Studio")), qPrintable(text));
    QVERIFY2(warning.contains(QStringLiteral("4 párhuzamos")), qPrintable(warning));
    QVERIFY2(warning.contains(QStringLiteral("kevés")), qPrintable(warning));
    text.clear(); warning.clear();
    describeLlmServer(parseLmStudioModels(modelsJson("m", 20480, 1), "m"), &text, &warning);
    QVERIFY(warning.isEmpty());
    QVERIFY(text.contains(QStringLiteral("párhuzamos kérések: 1")));
    text.clear();
    describeLlmServer(LlmServerInfo{}, &text, &warning);
    QVERIFY(text.isEmpty());

    // A „Kapcsolat tesztelése” a sikeres /models után a natív API-t is megkérdezi.
    FakeHttp srv;
    srv.handler = [](const FakeRequest& r) -> FakeReply {
        if (r.path == "/v1/models") return {200, R"({"data":[{"id":"qwen3.8-27b"}]})"};
        if (r.path == "/api/v1/models") return {200, modelsJson("qwen3.8-27b", 4096, 4)};
        return {404, "{}"};
    };
    ProviderDescriptor d;
    d.id = QStringLiteral("openai-compat");
    d.kind = ProviderKind::Llm;
    d.authMode = AuthMode::None;
    d.probe = {QStringLiteral("/models"), true};
    ConnectionTester tester;
    QSignalSpy done(&tester, &ConnectionTester::finished);
    tester.test(d, cfgFor(srv));
    QTRY_COMPARE(done.count(), 1);
    const ConnectionTestResult r = done.at(0).at(1).value<ConnectionTestResult>();
    QVERIFY(r.ok());
    QVERIFY(r.server.isLmStudio());
    QCOMPARE(r.server.instances.value(0).parallel, 4);
    QVERIFY(!r.serverText.isEmpty());
    QVERIFY2(r.warning.contains(QStringLiteral("párhuzamos")), qPrintable(r.warning));
    QCOMPARE(srv.count("POST", ""), 0);   // csak olvasó kérések
}

#endif

QTEST_GUILESS_MAIN(LlmContextTest)
#include "test_llm_context.moc"
