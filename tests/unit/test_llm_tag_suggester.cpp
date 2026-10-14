// LlmTagSuggester: assembling the candidate list and the user message, lenient parsing of the
// "pick: / new:" lines, turning them into suggestions (existing / very similar / new name, at
// most 2), and a full run against a fake HTTP server (thinking off).
#include <QtTest>
#include "tanara/providers/core/ProviderConfig.h"
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QJsonArray>

#include "tanara/processing/summary/PromptLibrary.h"
#include "tanara/providers/chat/OpenAiCompatibleProvider.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/processing/analysis/LlmTagSuggester.h"
#include "tanara/library/tags/TagService.h"
#include "fake_http.h"
#include "tags_fixture.h"

using namespace tanara;

class LlmTagSuggesterTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    void promptExists();
    void candidatesAndMessage();
    void lenientParsing();
    void toSuggestions();
    void fullRunAgainstFakeServer();

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<library::MeetingRepository> m_store;
    std::unique_ptr<library::TagService> m_tags;
    tagsfixture::Library m_lib;
    Tag m_nordvik, m_museum, m_belso;
};

void LlmTagSuggesterTest::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    m_store = std::make_unique<library::MeetingRepository>(m_dir->filePath("rec"), m_dir->filePath("meta"));
    m_lib = tagsfixture::buildLibrary(*m_store);
    m_tags = std::make_unique<library::TagService>(m_store.get(), m_dir->filePath("meta/tags.json"));
    m_nordvik = m_tags->addTag(m_lib.nordvik1, QStringLiteral("Nordvik"));
    m_tags->addTag(m_lib.nordvik2, m_nordvik.id);
    m_museum = m_tags->addTag(m_lib.museum1, QStringLiteral("MuseumPlus"));
    m_belso = m_tags->addTag(m_lib.budget1, QStringLiteral("Belső"));
}

void LlmTagSuggesterTest::cleanup()
{
    m_tags.reset();
    m_store.reset();
    m_dir.reset();
}

void LlmTagSuggesterTest::promptExists()
{
    const QString p = processing::promptBuiltin(QStringLiteral("tags"));
    QVERIFY(p.contains(QStringLiteral("pick:")));
    QVERIFY(p.contains(QStringLiteral("new:")));
    QVERIFY(!p.contains(QStringLiteral("json_schema")));
    QCOMPARE(processing::promptOutputFormat(QStringLiteral("tags")).kind, QStringLiteral("text"));
}

void LlmTagSuggesterTest::candidatesAndMessage()
{
    processing::LlmTagSuggester s(m_tags.get());
    TagSuggestion sug;
    sug.tagId = m_museum.id;
    sug.name = m_museum.name;
    const QVector<processing::LlmTagCandidate> c = s.candidates({ sug });
    QCOMPARE(c.size(), 3);
    QCOMPARE(c.first().tagId, m_museum.id);          // the suggestion first
    QCOMPARE(c.at(1).tagId, m_nordvik.id);           // then the most frequent
    QVERIFY(c.at(1).profileLine.contains(QStringLiteral("Kovács Anna")));
    QVERIFY(c.at(1).profileLine.contains(QStringLiteral("Nordvik ütemterv egyeztetés")));

    const QString msg = processing::LlmTagSuggester::buildUserMessage(QStringLiteral("A Nordvik raktár ütemterve."), c);
    QVERIFY(msg.startsWith(QStringLiteral("MEETING SUMMARY\nA Nordvik raktár ütemterve.")));
    QVERIFY(msg.contains(QStringLiteral("1. MuseumPlus")));
    QVERIFY(msg.contains(QStringLiteral("2. Nordvik — ")));
    QVERIFY(msg.contains(QStringLiteral("3. Belső")));
    QVERIFY(msg.contains(QStringLiteral("pick:")));
    QVERIFY(processing::LlmTagSuggester::buildUserMessage("x", {}).contains(QStringLiteral("(none yet)")));
}

void LlmTagSuggesterTest::lenientParsing()
{
    auto p = processing::LlmTagSuggester::parse(QStringLiteral("pick: 3, 7\nnew: Raktárköltözés; Q4 audit"));
    QCOMPARE(p.picks, (QVector<int>{3, 7}));
    QCOMPARE(p.newNames, (QStringList{"Raktárköltözés", "Q4 audit"}));

    p = processing::LlmTagSuggester::parse(QStringLiteral("Sure!\n**Pick:** 2 and 2, 5.\n- **New:** \"#Raktár\", none\n"));
    QCOMPARE(p.picks, (QVector<int>{2, 5}));
    QCOMPARE(p.newNames, QStringList{"Raktár"});

    p = processing::LlmTagSuggester::parse(QStringLiteral("PICK = none\nNEW: none"));
    QVERIFY(p.picks.isEmpty());
    QVERIFY(p.newNames.isEmpty());

    p = processing::LlmTagSuggester::parse(QStringLiteral("new: Alfa; alfa; Béta;  "));
    QCOMPARE(p.newNames, (QStringList{"Alfa", "Béta"}));
    QVERIFY(processing::LlmTagSuggester::parse(QStringLiteral("Nincs megfelelő címke.")).picks.isEmpty());
}

void LlmTagSuggesterTest::toSuggestions()
{
    processing::LlmTagSuggester s(m_tags.get());
    const QVector<processing::LlmTagCandidate> c = s.candidates({});   // Nordvik, MuseumPlus/Internal (by frequency)
    QCOMPARE(c.first().tagId, m_nordvik.id);
    processing::LlmTagSuggester::Parsed p;
    p.picks = { 1, 2, 99 };
    p.newNames = { QStringLiteral("Nordvk"), QStringLiteral("Raktárköltözés"), QStringLiteral("Audit"),
                   QStringLiteral("Harmadik") };
    // On museum2: Nordvik (1) and the 2nd candidate are existing; "Nordvk" is close to Nordvik, so not new.
    QVector<TagSuggestion> out = s.toSuggestions(p, c, m_lib.museum2);
    QStringList names;
    for (const TagSuggestion& x : out) {
        names << x.name;
        QCOMPARE(x.source, SuggestionSource::Llm);
    }
    QCOMPARE(names.count(QStringLiteral("Nordvik")), 1);
    QVERIFY(names.contains(QStringLiteral("Raktárköltözés")));
    QVERIFY(names.contains(QStringLiteral("Audit")));
    QVERIFY(!names.contains(QStringLiteral("Harmadik")));     // at most 2 new
    int newCount = 0;
    for (const TagSuggestion& x : out) if (x.isNew) { ++newCount; QVERIFY(x.tagId.isEmpty()); }
    QCOMPARE(newCount, 2);

    // The one already on the meeting and the rejected one are left out.
    TagSuggestion rej; rej.name = QStringLiteral("Audit"); rej.isNew = true;
    m_tags->reject(m_lib.nordvik1, rej);
    out = s.toSuggestions(p, c, m_lib.nordvik1);
    for (const TagSuggestion& x : out) {
        QVERIFY(x.tagId != m_nordvik.id);
        QVERIFY(x.name != QStringLiteral("Audit"));
    }
}

void LlmTagSuggesterTest::fullRunAgainstFakeServer()
{
    fakehttp::Server http;
    http.handler = [](const fakehttp::Request& r) -> fakehttp::Reply {
        if (!r.path.endsWith("/chat/completions")) return { 404, "{}" };
        const QJsonObject reply{ { "choices", QJsonArray{ QJsonObject{
            { "message", QJsonObject{ { "role", "assistant" }, { "content", "pick: 1\nnew: Raktárköltözés" } } },
            { "finish_reason", "stop" } } } } };
        return { 200, QJsonDocument(reply).toJson(QJsonDocument::Compact) };
    };
    providers::ProviderConfig cfg;
    cfg.baseUrl = http.base();
    cfg.model = QStringLiteral("google/gemma-4-12b");
    providers::OpenAiCompatibleProvider provider(cfg);
    processing::LlmTagSuggester s(m_tags.get());
    QSignalSpy done(&s, &processing::LlmTagSuggester::finished);
    s.start(&provider, cfg.model, processing::promptBuiltin(QStringLiteral("tags")), m_lib.museum2,
            QStringLiteral("A raktár költözéséről volt szó."), {});
    QVERIFY(s.isRunning());
    QVERIFY(done.wait(5000));
    QCOMPARE(done.first().at(0).toString(), m_lib.museum2);
    const auto list = done.first().at(1).value<QVector<TagSuggestion>>();
    QCOMPARE(list.size(), 2);
    QCOMPARE(list.at(0).tagId, m_nordvik.id);
    QVERIFY(list.at(1).isNew);
    // The request: thinking off (Gemma: reasoning_effort none), two messages.
    const QJsonObject body = http.lastBody("/chat/completions");
    QCOMPARE(body.value("reasoning_effort").toString(), QStringLiteral("none"));
    QCOMPARE(body.value("messages").toArray().size(), 2);
    QVERIFY(body.value("messages").toArray().at(1).toObject().value("content").toString()
                .contains(QStringLiteral("CANDIDATE TAGS")));
}

QTEST_GUILESS_MAIN(LlmTagSuggesterTest)
#include "test_llm_tag_suggester.moc"
