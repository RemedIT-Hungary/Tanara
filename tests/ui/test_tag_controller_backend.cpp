// TagControllerBackend — a TagBackend varrat valódi AppController + TagService fölött.
//  - készlet, legutóbbiak, felrakás / levétel, a core jeleinek továbbítása;
//  - javaslatok: Computing → Ready (computing jelző), elemek a pendingTagSuggestions-ből
//    (indok, hasonló megbeszélés), együtt járó (baseTagId), elutasítás;
//  - profil (megbeszélések a hosszal), átnevezés / összevonás / törlés, visszavonás-csoport;
//  - a MeetingTagsModel a controllerrel (a gyár ezt a backendet adja);
//  - vázlat-javaslat cím alapján.
// Izolált TANARA_HOME (QTemporaryDir), kitalált tesztkönyvtár; hálózat / LM Studio nincs.
#include "AppContext.h"
#include "MeetingTagsModel.h"
#include "TagBackend.h"
#include "TagControllerBackend.h"
#include "TagsViewModel.h"

#include "tanara/AppController.h"
#include "tanara/tags/MeetingProfiles.h"
#include "tanara/SettingsManager.h"
#include "tanara/store/MeetingStore.h"
#include "tanara/tags/TagService.h"

#include "../unit/tags_fixture.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace tanara_qml;

class TestTagControllerBackend : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { qputenv("TANARA_CLOUD", "off"); }
    void init()
    {
        m_home = std::make_unique<QTemporaryDir>();
        QVERIFY(m_home->isValid());
        qputenv("TANARA_HOME", m_home->path().toUtf8());
        m_app = std::make_unique<tanara::AppController>();
        QVERIFY(m_app->settings()->settings().audioDir.startsWith(m_home->path()));
        m_lib = tagsfixture::buildLibrary(*m_app->store());
        m_backend.reset(createTagBackend(m_app.get(), nullptr));
        QVERIFY(dynamic_cast<TagControllerBackend*>(m_backend.get()));
    }
    void cleanup()
    {
        m_backend.reset();
        m_app.reset();
        m_home.reset();
    }

    void setAndSignals()
    {
        TagBackend* b = m_backend.get();
        QSignalSpy tagsSpy(b, &TagBackend::tagsChanged);
        QSignalSpy meetingSpy(b, &TagBackend::meetingTagsChanged);
        QSignalSpy undoSpy(b, &TagBackend::undoChanged);

        b->beginGroup(QStringLiteral("Címke hozzáadva: #Nordvik"));
        const QString id = b->addTag(m_lib.nordvik1, QStringLiteral("Nordvik"), TagAddSource::Manual);
        b->endGroup();
        QVERIFY(!id.isEmpty());
        QCOMPARE(b->tagsOf(m_lib.nordvik1), QStringList{id});
        QVERIFY(tagsSpy.count() >= 1);
        QVERIFY(meetingSpy.count() >= 1);
        QCOMPARE(meetingSpy.last().at(0).toString(), m_lib.nordvik1);
        QVERIFY(undoSpy.count() >= 1);
        QVERIFY(b->canUndo());
        QCOMPARE(b->undoLabel(), QStringLiteral("Címke hozzáadva: #Nordvik"));

        // Név szerint a meglévőt kapja (nem jön létre másik).
        QCOMPARE(b->addTag(m_lib.nordvik2, QStringLiteral("nordvik"), TagAddSource::Suggestion), id);
        const QVector<TagItem> all = b->tags();
        QCOMPARE(all.size(), 1);
        QCOMPARE(all.first().name, QStringLiteral("Nordvik"));
        QCOMPARE(all.first().meetingCount, 2);
        QVERIFY(all.first().lastUsedAt.isValid());
        const QVector<TagItem> recent = b->recent(5);
        QCOMPARE(recent.size(), 1);
        QCOMPARE(recent.first().meetingCount, 2);

        b->removeTag(m_lib.nordvik2, id);
        QVERIFY(b->tagsOf(m_lib.nordvik2).isEmpty());
        b->undo();   // a levétel visszavonva
        QCOMPARE(b->tagsOf(m_lib.nordvik2), QStringList{id});

        // Üres meetingId: nem csinál semmit.
        QVERIFY(b->addTag(QString(), QStringLiteral("X"), TagAddSource::Manual).isEmpty());
        QVERIFY(b->tagsOf(QString()).isEmpty());
    }

    void suggestionsComputingThenReady()
    {
        TagBackend* b = m_backend.get();
        const QString nordvik = b->addTag(m_lib.nordvik1, QStringLiteral("Nordvik"), TagAddSource::Manual);
        const QString log = b->addTag(m_lib.nordvik1, QStringLiteral("Logisztika"), TagAddSource::Manual);

        QStringList seen;   // computing-állapot a jelek pillanatában
        connect(b, &TagBackend::suggestionsChanged, this, [&](const QString& id) {
            if (id == m_lib.nordvik2) seen << (b->suggestions(id).computing ? "computing" : "ready");
        });
        b->requestSuggestions(m_lib.nordvik2);
        QTRY_VERIFY_WITH_TIMEOUT(seen.contains("ready"), 10000);
        QCOMPARE(seen.first(), QStringLiteral("computing"));
        QCOMPARE(seen.last(), QStringLiteral("ready"));

        TagSuggestionState st = b->suggestions(m_lib.nordvik2);
        QVERIFY(!st.computing);
        QCOMPARE(st.source, QStringLiteral("similar"));
        QCOMPARE(st.items.size(), 2);
        QStringList ids;
        for (const TagSuggestionItem& s : st.items) {
            ids << s.tagId;
            QCOMPARE(s.source, QStringLiteral("similar"));
            QVERIFY(!s.isNew);
            QVERIFY(!s.reasons.isEmpty());
            QVERIFY(!s.similarMeetings.isEmpty());
            QCOMPARE(s.similarMeetings.first().meetingId, m_lib.nordvik1);
            QCOMPARE(s.similarMeetings.first().title, QStringLiteral("Nordvik ütemterv egyeztetés"));
            QVERIFY(s.similarMeetings.first().startedAt.isValid());
            for (const TagReasonItem& r : s.reasons)
                QVERIFY(r.kind == "participant" || r.kind == "terms" || r.kind == "title");
        }
        QVERIFY(ids.contains(nordvik) && ids.contains(log));

        // Elutasítás → isRejected igaz; a core szűrt újrakiadását (Computing nélkül) a backend
        // nem veszi át, a szűrés a nézetmodellé — így a visszavonás után visszajön.
        const TagSuggestionItem first = st.items.first();
        b->reject(m_lib.nordvik2, first);
        QVERIFY(b->isRejected(m_lib.nordvik2, first.tagId));
        QCOMPARE(m_app->pendingTagSuggestions(m_lib.nordvik2).size(), 1);
        QCOMPARE(b->suggestions(m_lib.nordvik2).items.size(), 2);
        b->undo();   // az elutasítás visszavonva
        QVERIFY(!b->isRejected(m_lib.nordvik2, first.tagId));

        // Friss kérés (Computing → Ready) felülírja; törölt címke eleme kimarad.
        b->remove(log);
        QVERIFY(b->suggestions(m_lib.nordvik2).items.size() == 1);
        b->undo();

        // Más meeting állapota üres.
        QVERIFY(b->suggestions(m_lib.museum1).items.isEmpty());
        QVERIFY(!b->suggestions(m_lib.museum1).computing);
    }

    void cooccur()
    {
        TagBackend* b = m_backend.get();
        const QString a = b->addTag(m_lib.nordvik1, QStringLiteral("Partnerek"), TagAddSource::Manual);
        const QString c = b->addTag(m_lib.nordvik1, QStringLiteral("Szerződés"), TagAddSource::Manual);
        b->addTag(m_lib.budget1, a, TagAddSource::Manual);
        b->addTag(m_lib.budget1, c, TagAddSource::Manual);
        b->addTag(m_lib.museum1, a, TagAddSource::Manual);
        QSignalSpy spy(b, &TagBackend::suggestionsChanged);
        b->requestCooccur(m_lib.museum1, a);
        QVERIFY(spy.count() >= 2);   // computing, ready
        const TagSuggestionState st = b->suggestions(m_lib.museum1);
        QCOMPARE(st.source, QStringLiteral("cooccur"));
        QCOMPARE(st.baseTagId, a);
        QCOMPARE(st.items.size(), 1);
        QCOMPARE(st.items.first().tagId, c);
    }

    void profileAndManager()
    {
        TagBackend* b = m_backend.get();
        const QString a = b->addTag(m_lib.museum1, QStringLiteral("MuseumPlus"), TagAddSource::Manual);
        b->addTag(m_lib.museum2, a, TagAddSource::Manual);
        const QString dup = b->addTag(m_lib.museum2, QStringLiteral("Museum Plusz"), TagAddSource::Manual);

        const TagProfileItem p = b->profile(a);
        QCOMPARE(p.tagId, a);
        QCOMPARE(p.meetingCount, 2);
        QCOMPARE(p.meetings.size(), 2);
        QCOMPARE(p.meetings.first().meetingId, m_lib.museum2);   // legújabb elöl
        QCOMPARE(p.meetings.first().durationMs, qint64(30 * 60 * 1000));
        QVERIFY(!p.participants.isEmpty());

        // A kezelő nézetmodellje ugyanígy látja (a hossz szövegként).
        TagsViewModel vm;
        vm.setController(m_app.get());
        vm.setSelectedId(a);
        const QVariantList meetings = vm.detail().value("meetings").toList();
        QCOMPARE(meetings.size(), 2);
        QCOMPARE(meetings.first().toMap().value("durationText").toString(), QStringLiteral("30 p"));

        QVERIFY(b->rename(a, QStringLiteral("MuseumPlus bevezetés")));
        QVERIFY(!b->rename(a, QStringLiteral("museum plusz")));   // a név másé
        b->merge(dup, a);
        QCOMPARE(b->tagsOf(m_lib.museum2), QStringList{a});
        QCOMPARE(b->tags().size(), 1);
        b->remove(a);
        QVERIFY(b->tags().isEmpty());
        QVERIFY(b->tagsOf(m_lib.museum1).isEmpty());
        b->undo();
        QCOMPARE(b->tags().size(), 1);
    }

    void meetingTagsModelWithController()
    {
        MeetingTagsModel m;
        m.setController(m_app.get());
        QVERIFY(dynamic_cast<TagControllerBackend*>(m.backend()));
        // Controllerrel nincs demó-meeting: meetingId nélkül üres.
        QVERIFY(m.tags().isEmpty());
        m.setMeetingId(m_lib.nordvik1);
        QSignalSpy toasts(&m, &MeetingTagsModel::toast);
        const QString id = m.add(QStringLiteral("Nordvik"));
        QVERIFY(!id.isEmpty());
        QCOMPARE(m.tagIds(), QStringList{id});
        m.add(QStringLiteral("Logisztika"));

        m.setMeetingId(m_lib.nordvik2);
        m.requestSuggestions();
        QTRY_VERIFY_WITH_TIMEOUT(m.suggestions().size() == 2, 10000);
        QVERIFY(!m.computing());
        QCOMPARE(m.suggestionLabel(), QStringLiteral("Javasolt"));
        const QVariantList why = m.whyData();
        QCOMPARE(why.size(), 2);
        QVERIFY(!why.first().toMap().value("similarMeetings").toList().isEmpty());
        QVERIFY(!why.first().toMap().value("similarMeetings").toList().first().toMap().value("dateText").toString().isEmpty());

        m.accept(0);
        QCOMPARE(m.tags().size(), 1);
        QCOMPARE(m.suggestions().size(), 1);
        QVERIFY(toasts.count() >= 1);
        QVERIFY(toasts.last().at(1).toBool());   // visszavonható
        QVERIFY(m.canUndo());
        m.undo();
        QVERIFY(m.tags().isEmpty());
        QCOMPARE(m.suggestions().size(), 2);   // a visszavont elfogadás után visszajön
        m.reject(0);
        QCOMPARE(m.suggestions().size(), 1);
        m.undo();
        QCOMPARE(m.suggestions().size(), 2);
    }

    void draftSuggestions()
    {
        TagBackend* b = m_backend.get();
        const QString nordvik = b->addTag(m_lib.nordvik1, QStringLiteral("Nordvik"), TagAddSource::Manual);
        // A profilok háttérszálon épülnek (a fő szál nem tölt szinkron): megvárjuk a kört.
        m_app->profiles()->ensureBuilt();
        QTRY_VERIFY_WITH_TIMEOUT(m_app->profiles()->isIdle(), 10000);
        const QVector<TagSuggestionItem> draft = b->draftSuggestions(QStringLiteral("Nordvik heti egyeztetés"));
        QVERIFY(!draft.isEmpty());
        QCOMPARE(draft.first().tagId, nordvik);
        // A demó-backend a T07 kitalált javaslatát adja.
        std::unique_ptr<TagBackend> demo(createTagBackend(nullptr, nullptr));
        QCOMPARE(demo->draftSuggestions(QStringLiteral("Nordvik")).value(0).name, QStringLiteral("Ügyféltámogatás"));
    }

private:
    std::unique_ptr<QTemporaryDir> m_home;
    std::unique_ptr<tanara::AppController> m_app;
    std::unique_ptr<TagBackend> m_backend;
    tagsfixture::Library m_lib;
};

QTEST_MAIN(TestTagControllerBackend)
#include "test_tag_controller_backend.moc"
