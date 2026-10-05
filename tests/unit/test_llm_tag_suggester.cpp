//
// LlmTagSuggester — a jelöltlista és a felhasználói üzenet összeállítása, a „pick: / new:”
// sorok engedékeny feldolgozása, a javaslatokká alakítás (meglévő / nagyon hasonló / új név,
// legfeljebb 2), és egy teljes futás egy ál-HTTP-szerver ellen (gondolkodás kikapcsolva).
//
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QJsonArray>

#include "tanara/PromptLibrary.h"
#include "tanara/llm/OpenAiCompatibleProvider.h"
#include "tanara/store/MeetingStore.h"
#include "tanara/tags/LlmTagSuggester.h"
#include "tanara/tags/TagService.h"
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
    std::unique_ptr<MeetingStore> m_store;
    std::unique_ptr<TagService> m_tags;
    tagsfixture::Library m_lib;
    Tag m_nordvik, m_museum, m_belso;
};

void LlmTagSuggesterTest::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    m_store = std::make_unique<MeetingStore>(m_dir->filePath("rec"), m_dir->filePath("meta"));
    m_lib = tagsfixture::buildLibrary(*m_store);
    m_tags = std::make_unique<TagService>(m_store.get(), m_dir->filePath("meta/tags.json"));
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
    const QString p = promptBuiltin(QStringLiteral("tags"));
    QVERIFY(p.contains(QStringLiteral("pick:")));
    QVERIFY(p.contains(QStringLiteral("new:")));
    QVERIFY(!p.contains(QStringLiteral("json_schema")));
    QCOMPARE(promptOutputFormat(QStringLiteral("tags")).kind, QStringLiteral("text"));
}

void LlmTagSuggesterTest::candidatesAndMessage()
{
    LlmTagSuggester s(m_tags.get());
    TagSuggestion sug;
    sug.tagId = m_museum.id;
    sug.name = m_museum.name;
    const QVector<LlmTagCandidate> c = s.candidates({ sug });
    QCOMPARE(c.size(), 3);
    QCOMPARE(c.first().tagId, m_museum.id);          // a javaslat elöl
    QCOMPARE(c.at(1).tagId, m_nordvik.id);           // aztán a leggyakoribb
    QVERIFY(c.at(1).profileLine.contains(QStringLiteral("Kovács Anna")));
    QVERIFY(c.at(1).profileLine.contains(QStringLiteral("Nordvik ütemterv egyeztetés")));

    const QString msg = LlmTagSuggester::buildUserMessage(QStringLiteral("A Nordvik raktár ütemterve."), c);
    QVERIFY(msg.startsWith(QStringLiteral("MEETING SUMMARY\nA Nordvik raktár ütemterve.")));
    QVERIFY(msg.contains(QStringLiteral("1. MuseumPlus")));
    QVERIFY(msg.contains(QStringLiteral("2. Nordvik — ")));
    QVERIFY(msg.contains(QStringLiteral("3. Belső")));
    QVERIFY(msg.contains(QStringLiteral("pick:")));
    QVERIFY(LlmTagSuggester::buildUserMessage("x", {}).contains(QStringLiteral("(none yet)")));
}

void LlmTagSuggesterTest::lenientParsing()
{
    auto p = LlmTagSuggester::parse(QStringLiteral("pick: 3, 7\nnew: Raktárköltözés; Q4 audit"));
    QCOMPARE(p.picks, (QVector<int>{3, 7}));
    QCOMPARE(p.newNames, (QStringList{"Raktárköltözés", "Q4 audit"}));

    p = LlmTagSuggester::parse(QStringLiteral("Sure!\n**Pick:** 2 and 2, 5.\n- **New:** \"#Raktár\", none\n"));
    QCOMPARE(p.picks, (QVector<int>{2, 5}));
    QCOMPARE(p.newNames, QStringList{"Raktár"});

    p = LlmTagSuggester::parse(QStringLiteral("PICK = none\nNEW: none"));
    QVERIFY(p.picks.isEmpty());
    QVERIFY(p.newNames.isEmpty());

    p = LlmTagSuggester::parse(QStringLiteral("new: Alfa; alfa; Béta;  "));
    QCOMPARE(p.newNames, (QStringList{"Alfa", "Béta"}));
    QVERIFY(LlmTagSuggester::parse(QStringLiteral("Nincs megfelelő címke.")).picks.isEmpty());
}

void LlmTagSuggesterTest::toSuggestions()
{
    LlmTagSuggester s(m_tags.get());
    const QVector<LlmTagCandidate> c = s.candidates({});   // Nordvik, MuseumPlus/Belső (gyakoriság)
    QCOMPARE(c.first().tagId, m_nordvik.id);
    LlmTagSuggester::Parsed p;
    p.picks = { 1, 2, 99 };
    p.newNames = { QStringLiteral("Nordvk"), QStringLiteral("Raktárköltözés"), QStringLiteral("Audit"),
                   QStringLiteral("Harmadik") };
    // A museum2-n: a Nordvik (1) és a 2. jelölt meglévő; „Nordvk” ≈ Nordvik → nem új.
    QVector<TagSuggestion> out = s.toSuggestions(p, c, m_lib.museum2);
    QStringList names;
    for (const TagSuggestion& x : out) {
        names << x.name;
        QCOMPARE(x.source, SuggestionSource::Llm);
    }
    QCOMPARE(names.count(QStringLiteral("Nordvik")), 1);
    QVERIFY(names.contains(QStringLiteral("Raktárköltözés")));
    QVERIFY(names.contains(QStringLiteral("Audit")));
    QVERIFY(!names.contains(QStringLiteral("Harmadik")));     // legfeljebb 2 új
    int newCount = 0;
    for (const TagSuggestion& x : out) if (x.isNew) { ++newCount; QVERIFY(x.tagId.isEmpty()); }
    QCOMPARE(newCount, 2);

    // A meetingen lévő és az elutasított kimarad.
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
    ProviderConfig cfg;
    cfg.baseUrl = http.base();
    cfg.model = QStringLiteral("google/gemma-4-12b");
    OpenAiCompatibleProvider provider(cfg);
    LlmTagSuggester s(m_tags.get());
    QSignalSpy done(&s, &LlmTagSuggester::finished);
    s.start(&provider, cfg.model, promptBuiltin(QStringLiteral("tags")), m_lib.museum2,
            QStringLiteral("A raktár költözéséről volt szó."), {});
    QVERIFY(s.isRunning());
    QVERIFY(done.wait(5000));
    QCOMPARE(done.first().at(0).toString(), m_lib.museum2);
    const auto list = done.first().at(1).value<QVector<TagSuggestion>>();
    QCOMPARE(list.size(), 2);
    QCOMPARE(list.at(0).tagId, m_nordvik.id);
    QVERIFY(list.at(1).isNew);
    // A kérés: gondolkodás kikapcsolva (Gemma: reasoning_effort none), két üzenet.
    const QJsonObject body = http.lastBody("/chat/completions");
    QCOMPARE(body.value("reasoning_effort").toString(), QStringLiteral("none"));
    QCOMPARE(body.value("messages").toArray().size(), 2);
    QVERIFY(body.value("messages").toArray().at(1).toObject().value("content").toString()
                .contains(QStringLiteral("CANDIDATE TAGS")));
}

QTEST_GUILESS_MAIN(LlmTagSuggesterTest)
#include "test_llm_tag_suggester.moc"
