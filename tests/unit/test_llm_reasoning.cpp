//
// OpenAI-kompatibilis provider — a „gondolkodás” kapcsolása modellcsaládonként (a kérés
// törzse egy helyi ál-HTTP-szerveren), és hogy a gondolkodás sosem kerül a válaszba.
//
#include <QtTest>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "tanara/llm/OpenAiCompatibleProvider.h"

using namespace tanara;

#ifndef Q_MOC_RUN
namespace {

// Egyetlen válaszra beállítható ál-szerver; a beérkezett kérés-törzseket gyűjti.
class FakeServer : public QObject {
public:
    QByteArray reply = "{}";
    QVector<QJsonObject> bodies;

    FakeServer() {
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

private:
    void feed(QTcpSocket* s, QByteArray& buf) {
        buf += s->readAll();
        const int headEnd = buf.indexOf("\r\n\r\n");
        if (headEnd < 0) return;
        qint64 len = 0;
        for (const QByteArray& line : buf.left(headEnd).split('\n')) {
            const QByteArray l = line.trimmed().toLower();
            if (l.startsWith("content-length:")) len = l.mid(15).trimmed().toLongLong();
        }
        if (buf.size() < headEnd + 4 + len) return;
        bodies.append(QJsonDocument::fromJson(buf.mid(headEnd + 4, len)).object());
        buf.clear();
        s->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                 + QByteArray::number(reply.size()) + "\r\nConnection: close\r\n\r\n" + reply);
        s->disconnectFromHost();
    }
    QTcpServer m_srv;
};

QByteArray completion(const QString& content, const QString& reasoning = QString())
{
    QJsonObject msg{{"role", "assistant"}, {"content", content}};
    if (!reasoning.isEmpty()) msg.insert("reasoning_content", reasoning);
    return QJsonDocument(QJsonObject{{"choices", QJsonArray{QJsonObject{{"message", msg},
                                                                        {"finish_reason", "stop"}}}}})
        .toJson(QJsonDocument::Compact);
}

LlmRequest userRequest()
{
    LlmRequest r;
    r.messages = {{QStringLiteral("system"), QStringLiteral("S")}, {QStringLiteral("user"), QStringLiteral("U")}};
    r.maxTokens = 123;
    return r;
}

} // namespace
#endif

class LlmReasoningTest : public QObject {
    Q_OBJECT
private slots:
    void switchByModelFamily_data();
    void switchByModelFamily();
    void requestBodies();
    void reasoningNeverReachesContent();
    void onlyThinkingFails();
    void stripThinkingVariants();

private:
#ifndef Q_MOC_RUN
    // Egy kérés a valódi providerrel; visszaadja a törzset és a választ (vagy a hibát).
    QJsonObject roundTrip(const QString& model, const QString& reasoning, const QByteArray& reply,
                          QString* text, QString* error);
#endif
};

#ifndef Q_MOC_RUN

QJsonObject LlmReasoningTest::roundTrip(const QString& model, const QString& reasoning,
                                        const QByteArray& reply, QString* text, QString* error)
{
    FakeServer srv;
    srv.reply = reply;
    ProviderConfig cfg;
    cfg.baseUrl = srv.base();
    cfg.model = model;
    cfg.reasoning = reasoning;
    OpenAiCompatibleProvider p(cfg);
    LlmJob* job = p.chat(userRequest());
    QSignalSpy fin(job, &LlmJob::finished);
    QSignalSpy err(job, &LlmJob::failed);
    [&]() { QTRY_VERIFY_WITH_TIMEOUT(fin.count() + err.count() == 1, 5000); }();
    if (text && fin.count()) *text = fin[0][0].toString();
    if (error && err.count()) *error = err[0][0].toString();
    return srv.bodies.value(0);
}

void LlmReasoningTest::switchByModelFamily_data()
{
    QTest::addColumn<QString>("setting");
    QTest::addColumn<QString>("model");
    QTest::addColumn<int>("expected");
    const int none = int(ReasoningSwitch::None), effort = int(ReasoningSwitch::Effort),
              prefill = int(ReasoningSwitch::Prefill);
    QTest::newRow("gemma auto") << "auto" << "google/gemma-4-12b-qat" << effort;
    QTest::newRow("gemma off") << "off" << "google/gemma-4-12b" << effort;
    QTest::newRow("qwen3.8 auto") << "auto" << "qwen3.8-27b" << prefill;
    QTest::newRow("qwen3 coder") << "auto" << "qwen/qwen3-coder-30b" << prefill;
    QTest::newRow("qwen-3 kötőjellel") << "off" << "Qwen-3-14B" << prefill;
    QTest::newRow("qwq") << "auto" << "qwq-32b" << prefill;
    QTest::newRow("qwen2.5 nem gondolkodik") << "auto" << "qwen2.5-7b-instruct" << effort;
    QTest::newRow("ismeretlen") << "auto" << "my-local-model" << effort;
    QTest::newRow("üres beállítás") << "" << "gemma" << effort;
    QTest::newRow("on: nincs kapcsoló") << "on" << "google/gemma-4-12b" << none;
    QTest::newRow("on qwen") << "ON" << "qwen3.8-27b" << none;
}

void LlmReasoningTest::switchByModelFamily()
{
    QFETCH(QString, setting);
    QFETCH(QString, model);
    QFETCH(int, expected);
    QCOMPARE(int(reasoningSwitchFor(setting, model)), expected);
}

void LlmReasoningTest::requestBodies()
{
    // Gemma (auto): reasoning_effort "none", nincs előtöltés.
    QString text;
    QJsonObject b = roundTrip(QStringLiteral("google/gemma-4-12b-qat"), QStringLiteral("auto"),
                              completion(QStringLiteral("ok")), &text, nullptr);
    QCOMPARE(text, QStringLiteral("ok"));
    QCOMPARE(b.value("reasoning_effort").toString(), QStringLiteral("none"));
    QCOMPARE(b.value("model").toString(), QStringLiteral("google/gemma-4-12b-qat"));
    QCOMPARE(b.value("max_tokens").toInt(), 123);
    QCOMPARE(b.value("stream").toBool(true), false);
    QCOMPARE(b.value("messages").toArray().size(), 2);

    // Qwen 3 (auto): üres <think> blokkal előtöltött asszisztens-üzenet, reasoning_effort nélkül.
    b = roundTrip(QStringLiteral("qwen3.8-27b"), QStringLiteral("auto"), completion(QStringLiteral("ok")),
                  nullptr, nullptr);
    QVERIFY(!b.contains("reasoning_effort"));
    const QJsonArray msgs = b.value("messages").toArray();
    QCOMPARE(msgs.size(), 3);
    QCOMPARE(msgs[1].toObject().value("role").toString(), QStringLiteral("user"));
    QCOMPARE(msgs[2].toObject().value("role").toString(), QStringLiteral("assistant"));
    QCOMPARE(msgs[2].toObject().value("content").toString(), QStringLiteral("<think>\n\n</think>\n\n"));

    // Ismeretlen modell: reasoning_effort "none" (a nem ismerő szerver figyelmen kívül hagyja).
    b = roundTrip(QStringLiteral("valami-modell"), QStringLiteral("off"), completion(QStringLiteral("ok")),
                  nullptr, nullptr);
    QCOMPARE(b.value("reasoning_effort").toString(), QStringLiteral("none"));
    QCOMPARE(b.value("messages").toArray().size(), 2);

    // "on": semmilyen kapcsoló.
    b = roundTrip(QStringLiteral("qwen3.8-27b"), QStringLiteral("on"), completion(QStringLiteral("ok")),
                  nullptr, nullptr);
    QVERIFY(!b.contains("reasoning_effort"));
    QCOMPARE(b.value("messages").toArray().size(), 2);

    // A ProviderConfig alapértéke "auto".
    QCOMPARE(ProviderConfig{}.reasoning, QStringLiteral("auto"));
}

void LlmReasoningTest::reasoningNeverReachesContent()
{
    QString text;
    roundTrip(QStringLiteral("gemma"), QStringLiteral("auto"),
              completion(QStringLiteral("{\"execSummary\":\"X\"}"), QStringLiteral("Hosszú gondolatmenet…")),
              &text, nullptr);
    QCOMPARE(text, QStringLiteral("{\"execSummary\":\"X\"}"));
    // A tartalom elejére szivárgott gondolkodás-blokk lekerül.
    roundTrip(QStringLiteral("gemma"), QStringLiteral("auto"),
              completion(QStringLiteral("<think>\nelőbb gondolkodom\n</think>\n\nTOPICS\n- a")), &text, nullptr);
    QCOMPARE(text, QStringLiteral("TOPICS\n- a"));
}

void LlmReasoningTest::onlyThinkingFails()
{
    QString text, error;
    roundTrip(QStringLiteral("gemma"), QStringLiteral("on"),
              completion(QString(), QStringLiteral("csak gondolkodtam")), &text, &error);
    QVERIFY(text.isEmpty());
    QVERIFY2(error.contains(QStringLiteral("gondolkod")), qPrintable(error));
    QVERIFY(!error.contains(QStringLiteral("csak gondolkodtam")));   // a gondolkodás szövege sem szivárog
    // Lezáratlan <think> a tartalomban = nincs válasz.
    error.clear();
    roundTrip(QStringLiteral("gemma"), QStringLiteral("auto"),
              completion(QStringLiteral("<think>végtelen gondolkodás…")), &text, &error);
    QVERIFY(!error.isEmpty());
}

void LlmReasoningTest::stripThinkingVariants()
{
    QCOMPARE(stripThinking(QStringLiteral("válasz")), QStringLiteral("válasz"));
    QCOMPARE(stripThinking(QStringLiteral("  <think>x</think>  válasz")), QStringLiteral("válasz"));
    QCOMPARE(stripThinking(QStringLiteral("<THINK>x</THINK>válasz")), QStringLiteral("válasz"));
    QCOMPARE(stripThinking(QStringLiteral("</think>\n\nválasz")), QStringLiteral("válasz"));
    QCOMPARE(stripThinking(QStringLiteral("<think>nincs vége")), QString());
    // Csak a VEZETŐ blokk számít; a szövegközi címke a válasz része.
    QCOMPARE(stripThinking(QStringLiteral("A <think> címke")), QStringLiteral("A <think> címke"));
}

#endif // Q_MOC_RUN

QTEST_GUILESS_MAIN(LlmReasoningTest)
#include "test_llm_reasoning.moc"
