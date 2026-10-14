// TagControllerBackend: the TagBackend seam over a real AppController + TagService.
//  - set, recents, apply / remove, forwarding core's signals;
//  - suggestions: Computing to Ready (computing flag), items from pendingTagSuggestions
//    (reason, similar meeting), co-occurring (baseTagId), rejection;
//  - profile (meetings with their length), rename / merge / delete, undo group;
//  - MeetingTagsModel with the controller (the factory returns this backend);
//  - draft suggestion by title.
// Isolated TANARA_HOME (QTemporaryDir), made-up test library; no network / LM Studio.
#include "AppContext.h"
#include "MeetingTagsModel.h"
#include "TagBackend.h"
#include "TagControllerBackend.h"
#include "TagsViewModel.h"

#include "tanara/app/AppController.h"
#include "tanara/library/store/PeopleStore.h"
#include "tanara/library/tags/MeetingProfiles.h"
#include "tanara/app/SettingsManager.h"
#include "tanara/library/store/MeetingRepository.h"
#include "tanara/library/tags/TagService.h"

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
        m_app = std::make_unique<tanara::app::AppController>();
        QVERIFY(m_app->settings()->paths().primaryRecordingRoot().startsWith(m_home->path()));
        m_lib = tagsfixture::buildLibrary(*m_app->store(), m_app->people());
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

        // By name it gets the existing one (no other is created).
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
        b->undo();   // the removal undone
        QCOMPARE(b->tagsOf(m_lib.nordvik2), QStringList{id});

        // Empty meetingId: does nothing.
        QVERIFY(b->addTag(QString(), QStringLiteral("X"), TagAddSource::Manual).isEmpty());
        QVERIFY(b->tagsOf(QString()).isEmpty());
    }

    void suggestionsComputingThenReady()
    {
        TagBackend* b = m_backend.get();
        const QString nordvik = b->addTag(m_lib.nordvik1, QStringLiteral("Nordvik"), TagAddSource::Manual);
        const QString log = b->addTag(m_lib.nordvik1, QStringLiteral("Logisztika"), TagAddSource::Manual);

        QStringList seen;   // computing state at the moment of the signals
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

        // Reject: isRejected becomes true; the backend does not take over core's filtered re-emit
        // (without Computing), the filtering belongs to the view model, so after undo it comes back.
        const TagSuggestionItem first = st.items.first();
        b->reject(m_lib.nordvik2, first);
        QVERIFY(b->isRejected(m_lib.nordvik2, first.tagId));
        QCOMPARE(m_app->pendingTagSuggestions(m_lib.nordvik2).size(), 1);
        QCOMPARE(b->suggestions(m_lib.nordvik2).items.size(), 2);
        b->undo();   // the rejection undone
        QVERIFY(!b->isRejected(m_lib.nordvik2, first.tagId));

        // A fresh request (Computing to Ready) overwrites; a deleted tag's item is left out.
        b->remove(log);
        QVERIFY(b->suggestions(m_lib.nordvik2).items.size() == 1);
        b->undo();

        // Another meeting's state is empty.
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
        QCOMPARE(p.meetings.first().meetingId, m_lib.museum2);   // newest first
        QCOMPARE(p.meetings.first().durationMs, qint64(30 * 60 * 1000));
        QVERIFY(!p.participants.isEmpty());

        // The manager view model sees it the same way (the length as text).
        TagsViewModel vm;
        vm.setController(m_app.get());
        vm.setSelectedId(a);
        const QVariantList meetings = vm.detail().value("meetings").toList();
        QCOMPARE(meetings.size(), 2);
        QCOMPARE(meetings.first().toMap().value("durationText").toString(), QStringLiteral("30 p"));

        QVERIFY(b->rename(a, QStringLiteral("MuseumPlus bevezetés")));
        QVERIFY(!b->rename(a, QStringLiteral("museum plusz")));   // the name belongs to another
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
        // With a controller there is no demo meeting: without a meetingId it is empty.
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
        QVERIFY(toasts.last().at(1).toBool());   // undoable
        QVERIFY(m.canUndo());
        m.undo();
        QVERIFY(m.tags().isEmpty());
        QCOMPARE(m.suggestions().size(), 2);   // comes back after the accepted one is undone
        m.reject(0);
        QCOMPARE(m.suggestions().size(), 1);
        m.undo();
        QCOMPARE(m.suggestions().size(), 2);
    }

    // A suggestion from the participants' manual tags ("Based on the participants"), before the transcript too.
    void participantTagSuggestionsAndLabel()
    {
        tanara::library::TagService* tags = m_app->tags();
        const QString nord = tags->create(QStringLiteral("Nordvik")).id;
        tags->setPersonTags(m_app->people()->ensure(QStringLiteral("Fehér Gábor")), {nord});
        tags->setPersonTags(m_app->people()->ensure(QStringLiteral("Varga Árpád")), {nord});
        const QString mid = m_app->store()->createMeeting(QStringLiteral("Friss felvétel")).id;
        m_app->addParticipant(mid, m_app->people()->choiceFor(QStringLiteral("Fehér Gábor")));
        m_app->addParticipant(mid, m_app->people()->choiceFor(QStringLiteral("Varga Árpád")));

        MeetingTagsModel m;
        m.setController(m_app.get());
        m.setMeetingId(mid);
        m.requestSuggestions();
        QTRY_VERIFY_WITH_TIMEOUT(!m.suggestions().isEmpty() && !m.computing(), 10000);
        TagSuggestionState st = m.backend()->suggestions(mid);
        QCOMPARE(st.source, QStringLiteral("people"));
        QCOMPARE(st.items.first().tagId, nord);
        QCOMPARE(st.items.first().reasons.first().kind, QStringLiteral("peopleTags"));
        QCOMPARE(m.suggestionSource(), QStringLiteral("people"));
        QCOMPARE(m.suggestionLabel(), QStringLiteral("A résztvevők alapján"));
        const QVariantMap reason = m.whyData().first().toMap().value("reasons").toList().first().toMap();
        QCOMPARE(reason.value("label").toString(), QStringLiteral("Résztvevők címkéje"));
        QCOMPARE(reason.value("text").toString(), QStringLiteral("Fehér Gábor, Varga Árpád (mindkettőjükön)"));

        // After applying it disappears; a participant change does not bring it back.
        m.accept(0);
        QVERIFY(m.suggestions().isEmpty());
        m_app->addParticipant(mid, m_app->people()->choiceFor(QStringLiteral("Szabó Bence")));
        QVERIFY(m.suggestions().isEmpty());
    }

    void draftSuggestions()
    {
        TagBackend* b = m_backend.get();
        const QString nordvik = b->addTag(m_lib.nordvik1, QStringLiteral("Nordvik"), TagAddSource::Manual);
        // The profiles are built on a background thread (the main thread does not load synchronously): we wait for the round.
        m_app->profiles()->ensureBuilt();
        QTRY_VERIFY_WITH_TIMEOUT(m_app->profiles()->isIdle(), 10000);
        const QVector<TagSuggestionItem> draft = b->draftSuggestions(QStringLiteral("Nordvik heti egyeztetés"));
        QVERIFY(!draft.isEmpty());
        QCOMPARE(draft.first().tagId, nordvik);
        // The demo backend returns the made-up suggestion.
        std::unique_ptr<TagBackend> demo(createTagBackend(nullptr, nullptr));
        QCOMPARE(demo->draftSuggestions(QStringLiteral("Nordvik")).value(0).name, QStringLiteral("Ügyféltámogatás"));
    }

private:
    std::unique_ptr<QTemporaryDir> m_home;
    std::unique_ptr<tanara::app::AppController> m_app;
    std::unique_ptr<TagBackend> m_backend;
    tagsfixture::Library m_lib;
};

QTEST_MAIN(TestTagControllerBackend)
#include "test_tag_controller_backend.moc"
