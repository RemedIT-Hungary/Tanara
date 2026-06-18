#include <QtTest>
#include <QTimer>
#include <QSignalSpy>

#include "tanara/Types.h"
#include "tanara/SummaryService.h"
#include "tanara/llm/ILlmProvider.h"

using namespace tanara;

// --- Fake job: a kötés után aszinkron (QTimer::singleShot(0)) emittál finished()-t ---
class FakeLlmJob : public LlmJob {
    Q_OBJECT
public:
    explicit FakeLlmJob(QString canned, QObject* parent = nullptr)
        : LlmJob(parent), m_canned(std::move(canned)) {}

    void cancel() override {}

    void fire() {
        QTimer::singleShot(0, this, [this]() { emit finished(m_canned); });
    }

private:
    QString m_canned;
};

// --- Fake provider: minden chat()-re egy beégetett JSON-t adó jobot ad vissza ---
class FakeLlmProvider : public QObject, public ILlmProvider {
    Q_OBJECT
public:
    explicit FakeLlmProvider(QString canned, QObject* parent = nullptr)
        : QObject(parent), m_canned(std::move(canned)) {}

    QString name() const override { return QStringLiteral("fake"); }
    bool supportsStreaming() const override { return false; }

    LlmJob* chat(const LlmRequest&) override {
        auto* job = new FakeLlmJob(m_canned, this);
        job->fire();
        return job;
    }

private:
    QString m_canned;
};

class SummaryTest : public QObject {
    Q_OBJECT
private slots:
    void parsesCannedJsonIntoSummary();
    void parsesFencedJson();
    void renderMarkdownHasHungarianHeaders();
    void emptySectionsSkipped();
};

void SummaryTest::parsesCannedJsonIntoSummary()
{
    const QString canned = QStringLiteral(R"JSON(
    {
      "execSummary": "Megbeszéltük a Tanara projekt indítását.",
      "decisions": ["Qt6-ot használunk", "MVP-ben nincs streaming"],
      "actionItems": [
        {"text": "Provider megírása", "owner": "Ádám", "due": "2026-06-20"},
        {"text": "Tesztek", "owner": "Béla", "due": ""}
      ],
      "participants": ["Ádám", "Béla"]
    })JSON");

    FakeLlmProvider provider(canned);
    SummaryService svc(&provider);

    QSignalSpy readySpy(&svc, &SummaryService::summaryReady);
    QSignalSpy failSpy(&svc, &SummaryService::summaryFailed);

    MergedTranscript t;
    t.language = QStringLiteral("hu");
    svc.summarize(t, QStringLiteral("kontextus"), QStringList{QStringLiteral("Tanara")});

    QVERIFY(readySpy.wait(2000));
    QCOMPARE(failSpy.count(), 0);
    QCOMPARE(readySpy.count(), 1);

    const auto sum = qvariant_cast<tanara::Summary>(readySpy.at(0).at(0));
    QCOMPARE(sum.execSummary, QStringLiteral("Megbeszéltük a Tanara projekt indítását."));
    QCOMPARE(sum.decisions.size(), 2);
    QCOMPARE(sum.decisions.value(0), QStringLiteral("Qt6-ot használunk"));
    QCOMPARE(sum.actionItems.size(), 2);
    QCOMPARE(sum.actionItems.value(0).text, QStringLiteral("Provider megírása"));
    QCOMPARE(sum.actionItems.value(0).owner, QStringLiteral("Ádám"));
    QCOMPARE(sum.actionItems.value(0).due, QStringLiteral("2026-06-20"));
    QCOMPARE(sum.participants.size(), 2);
}

void SummaryTest::parsesFencedJson()
{
    const QString canned = QStringLiteral(
        "```json\n"
        "{\"execSummary\":\"X\",\"decisions\":[],\"actionItems\":[],\"participants\":[]}\n"
        "```");

    FakeLlmProvider provider(canned);
    SummaryService svc(&provider);
    QSignalSpy readySpy(&svc, &SummaryService::summaryReady);
    QSignalSpy failSpy(&svc, &SummaryService::summaryFailed);

    MergedTranscript t;
    svc.summarize(t, QString(), QStringList());

    QVERIFY(readySpy.wait(2000));
    QCOMPARE(failSpy.count(), 0);
    const auto sum = qvariant_cast<tanara::Summary>(readySpy.at(0).at(0));
    QCOMPARE(sum.execSummary, QStringLiteral("X"));
}

void SummaryTest::renderMarkdownHasHungarianHeaders()
{
    tanara::Summary sum;
    sum.execSummary = QStringLiteral("Összefoglaló szöveg.");
    sum.decisions = QStringList{QStringLiteral("Döntés 1")};
    ActionItem ai;
    ai.text = QStringLiteral("Teendő");
    ai.owner = QStringLiteral("Ádám");
    ai.due = QStringLiteral("holnap");
    sum.actionItems.append(ai);
    sum.participants = QStringList{QStringLiteral("Ádám"), QStringLiteral("Béla")};

    const QString md = sum.renderMarkdown();
    QVERIFY(md.contains(QStringLiteral("## Vezetői összefoglaló")));
    QVERIFY(md.contains(QStringLiteral("## Döntések")));
    QVERIFY(md.contains(QStringLiteral("## Teendők")));
    QVERIFY(md.contains(QStringLiteral("## Résztvevők")));
    QVERIFY(md.contains(QStringLiteral("- [ ] Teendő — Ádám (holnap)")));
    QVERIFY(md.contains(QStringLiteral("Ádám, Béla")));
}

void SummaryTest::emptySectionsSkipped()
{
    tanara::Summary sum;
    sum.execSummary = QStringLiteral("Csak összefoglaló.");
    const QString md = sum.renderMarkdown();
    QVERIFY(md.contains(QStringLiteral("## Vezetői összefoglaló")));
    QVERIFY(!md.contains(QStringLiteral("## Döntések")));
    QVERIFY(!md.contains(QStringLiteral("## Teendők")));
    QVERIFY(!md.contains(QStringLiteral("## Résztvevők")));
}

QTEST_GUILESS_MAIN(SummaryTest)
#include "test_summary.moc"
