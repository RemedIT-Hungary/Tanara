#pragma once
//
// PeopleViewModel: view model of the People window.
//
// Left side: `people` (PeopleListModel): search in name AND nickname (accent- and
// case-insensitive, the match in the name highlighted), sorting (alphabetical / recent / most
// meetings), sections. Right side: the selected person's (`selectedName`) data: nicknames,
// note, voice samples, meetings.
//
// The list appears IMMEDIATELY (names + sample count from the stores); the meeting count,
// speaking time and "last seen" are computed in the background (tanara::PeopleStats) and
// load afterwards (`statsReady`). Every action goes through tanara::PeopleService; after
// undoable steps it emits `toast(text, undoable)` (QML shows it for ~8 s).
//
// Tags on the person: the selected person's manual tags (`personTags`), the "Usually there at
// these" suggestions (`tagSuggestions`, all of them; the view shows three + "+N"), through
// the TagBackend seam (TagService). The own person and the "Anonymous N" sample holder have
// no tag section (`tagsMode`). Search also matches tags (the hit row's meta: "tag: #Nordvik").
// Tag actions can be undone on the TagService stack; Ctrl+Z undoes the more recent of the
// two stacks.
//
// Samples per model: one sample (sibling group) is one row; the row's `modelText` ("2/2
// models") and `modelTip` show which used voice model has a voiceprint of it. While the
// voiceprint backfill runs (`backfillRunning`), the header shows it.
//
// On the fictional facade (demo, screenshot: tanara_demo) it serves fictional people (`demoState`:
// P01 ... P06 and the helper states, see PeopleWindow.qml); then person actions do nothing,
// and tag actions run on the fictional tag pool.
//
#include "PeopleListModel.h"
#include "TagBackend.h"
#include "UiServices.h"
#include "tanara/domain/Person.h"

#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace tanara::library { class IPeopleFacade; }

namespace tanara_qml {

class PlayerBackend;

class PeopleViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString demoState READ demoState WRITE setDemoState NOTIFY demoStateChanged)

    // ---- list ----
    Q_PROPERTY(tanara_qml::PeopleListModel* people READ people CONSTANT)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(bool searching READ searching NOTIFY queryChanged)
    // "abc" | "recent" | "meetings"
    Q_PROPERTY(QString sort READ sort WRITE setSort NOTIFY sortChanged)
    Q_PROPERTY(QString sortLabel READ sortLabel NOTIFY sortChanged)
    Q_PROPERTY(QString countText READ countText NOTIFY listChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY listChanged)
    // Only the own person exists (empty state).
    Q_PROPERTY(bool onlySelf READ onlySelf NOTIFY listChanged)
    // Meeting counts / speaking times are available (before that, rows show only the sample count).
    Q_PROPERTY(bool statsReady READ statsReady NOTIFY listChanged)

    // ---- the selected person ----
    Q_PROPERTY(QString selectedName READ selectedName WRITE setSelectedName NOTIFY selectionChanged)
    Q_PROPERTY(int selectedRow READ selectedRow NOTIFY listChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    Q_PROPERTY(bool selectedIsSelf READ selectedIsSelf NOTIFY detailChanged)
    Q_PROPERTY(QString selectedMonogram READ selectedMonogram NOTIFY detailChanged)
    Q_PROPERTY(QString detailMeta READ detailMeta NOTIFY detailChanged)
    Q_PROPERTY(QStringList aliases READ aliases NOTIFY detailChanged)
    Q_PROPERTY(QString note READ note NOTIFY detailChanged)
    // [{ id, kind, icon, label, meetingTitle, date, length, playable }], newest first
    Q_PROPERTY(QVariantList samples READ samples NOTIFY detailChanged)
    Q_PROPERTY(int sampleCount READ sampleCount NOTIFY detailChanged)
    Q_PROPERTY(QString samplesSubText READ samplesSubText NOTIFY detailChanged)
    // [{ title, date, talk }], newest first
    Q_PROPERTY(QVariantList meetings READ meetings NOTIFY detailChanged)
    Q_PROPERTY(int meetingCount READ meetingCount NOTIFY detailChanged)

    // ---- person without a voiceprint ----
    // Whether the plan is ready (reviewing the meetings takes a short while).
    Q_PROPERTY(bool planReady READ planReady NOTIFY planChanged)
    Q_PROPERTY(bool planPossible READ planPossible NOTIFY planChanged)
    Q_PROPERTY(QString planText READ planText NOTIFY planChanged)
    Q_PROPERTY(QString planButtonText READ planButtonText NOTIFY planChanged)
    Q_PROPERTY(QString planNote READ planNote NOTIFY planChanged)
    Q_PROPERTY(bool creatingVoiceprint READ creatingVoiceprint NOTIFY planChanged)
    Q_PROPERTY(QString creatingText READ creatingText NOTIFY planChanged)

    // ---- playback, undo ----
    Q_PROPERTY(QString playingSampleId READ playingSampleId NOTIFY playingChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged)

    // ---- tags on the person ----
    // "edit" (editable) | "self" (own person: muted explanatory line) | "none" (anonymous)
    Q_PROPERTY(QString tagsMode READ tagsMode NOTIFY personTagsChanged)
    // [{ id, name }] in the order they were added
    Q_PROPERTY(QVariantList personTags READ personTags NOTIFY personTagsChanged)
    // [{ id, name, countText, toolTip }]: all of them; the view shows at most three + "+N"
    Q_PROPERTY(QVariantList tagSuggestions READ tagSuggestions NOTIFY personTagsChanged)
    // Muted text in place of the suggestion row ("Counting meetings..." / "A suggestion needs at
    // least two meetings."); empty: suggestions or nothing.
    Q_PROPERTY(QString tagHintText READ tagHintText NOTIFY personTagsChanged)

    // ---- learned track side ----
    // "local" | "remote" | "" (no data): the value of the "Usually: ..." segment.
    Q_PROPERTY(QString defaultSide READ defaultSide NOTIFY detailChanged)
    // The source next to the segment: "learned" | "set manually" | "" (no data).
    Q_PROPERTY(QString defaultSideNote READ defaultSideNote NOTIFY detailChanged)
    // Whether the row is visible (not the own person, whose voice is the mic, and not the sample holder).
    Q_PROPERTY(bool defaultSideVisible READ defaultSideVisible NOTIFY detailChanged)

    // ---- voice models ----
    Q_PROPERTY(bool backfillRunning READ backfillRunning NOTIFY backfillChanged)

public:
    // QML: the services of the moment (follows App.controller / App.demo).
    explicit PeopleViewModel(QObject* parent = nullptr);
    // The people facade and the tag backend of the given services.
    explicit PeopleViewModel(const UiServices& services, QObject* parent = nullptr);
    ~PeopleViewModel() override;

    QString demoState() const { return m_demoState; }
    void setDemoState(const QString& state);

    PeopleListModel* people() { return &m_list; }
    QString query() const { return m_query; }
    void setQuery(const QString& query);
    bool searching() const { return !m_query.trimmed().isEmpty(); }
    QString sort() const { return m_sort; }
    void setSort(const QString& sort);
    QString sortLabel() const;
    QString countText() const;
    int totalCount() const { return int(m_persons.size()); }
    bool onlySelf() const;
    bool statsReady() const { return m_statsReady; }

    QString selectedName() const { return m_selected; }
    void setSelectedName(const QString& name);
    int selectedRow() const { return m_list.indexOfName(m_selected); }
    bool hasSelection() const { return !m_selected.isEmpty(); }
    bool selectedIsSelf() const { return m_detail.isSelf; }
    QString selectedMonogram() const;
    QString detailMeta() const { return m_detailMeta; }
    QStringList aliases() const { return m_detail.aliases; }
    QString note() const { return m_detail.note; }
    QVariantList samples() const { return m_samples; }
    int sampleCount() const { return int(m_samples.size()); }
    QString samplesSubText() const { return m_samplesSub; }
    QVariantList meetings() const { return m_meetings; }
    int meetingCount() const { return int(m_meetings.size()); }

    bool planReady() const { return m_planReady; }
    bool planPossible() const { return m_planPossible; }
    QString planText() const { return m_planText; }
    QString planButtonText() const { return m_planButton; }
    QString planNote() const { return m_planNote; }
    bool creatingVoiceprint() const { return m_creating; }
    QString creatingText() const { return m_creatingText; }

    QString playingSampleId() const { return m_playingId; }
    bool canUndo() const;

    // ---- actions (the returned text is an error message; empty = succeeded) ----
    // When the window opens: fresh state from disk + the statistics refresh in the background.
    Q_INVOKABLE void refresh();
    // Select by name (deep link): also clears the search if the person would not be visible.
    Q_INVOKABLE bool selectPerson(const QString& name);
    Q_INVOKABLE bool personExists(const QString& name) const;
    Q_INVOKABLE QString addPerson(const QString& name);
    Q_INVOKABLE QString rename(const QString& newName);
    Q_INVOKABLE QString addAlias(const QString& alias);
    Q_INVOKABLE void removeAlias(const QString& alias);
    Q_INVOKABLE void setNote(const QString& note);

    Q_INVOKABLE void toggleSample(const QString& sampleId);
    Q_INVOKABLE void stopSample();
    Q_INVOKABLE void removeSample(const QString& sampleId);
    // Move to an existing person, or, if no such name exists, a new person from the sample.
    Q_INVOKABLE QString moveSample(const QString& sampleId, const QString& toName);
    // The sample-move dialog's list: [{ name, monogram, meta }] (without the selected one).
    Q_INVOKABLE QVariantList personChoices(const QString& query) const;

    Q_INVOKABLE void createVoiceprint();

    // Merge: candidates by voice similarity: [{ name, monogram, meta, similarity }].
    Q_INVOKABLE QVariantList mergeCandidates(const QString& query) const;
    // The result row (rich text) with the chosen candidate; keepSelected: the selected one's name stays.
    Q_INVOKABLE QString mergeResultText(const QString& other, bool keepSelected) const;
    Q_INVOKABLE QString merge(const QString& other, bool keepSelected);

    // The consequences of deleting, as text (per the checkbox state).
    Q_INVOKABLE QString deleteText(bool keepSamples) const;
    Q_INVOKABLE bool deleteHasSamples() const;
    Q_INVOKABLE QString deletePerson(bool keepSamples);

    Q_INVOKABLE void undo();

    // ---- tags on the selected person (each one an undoable step, with a toast) ----
    QString tagsMode() const { return m_tagsMode; }
    QVariantList personTags() const { return m_personTags; }
    QVariantList tagSuggestions() const { return m_tagSuggestions; }
    QString tagHintText() const { return m_tagHint; }
    // From the field: name (a new tag too) or id.
    Q_INVOKABLE void addPersonTag(const QString& nameOrId);
    Q_INVOKABLE void removePersonTag(const QString& tagId);
    Q_INVOKABLE void acceptTagSuggestion(const QString& tagId);
    Q_INVOKABLE void acceptAllTagSuggestions();
    Q_INVOKABLE void rejectTagSuggestion(const QString& tagId);

    QString defaultSide() const { return m_detail.defaultSide; }
    QString defaultSideNote() const;
    bool defaultSideVisible() const;
    // Manual override of the track side ("local" | "remote" | "" = no data / forget).
    Q_INVOKABLE void setDefaultSide(const QString& side);
    // Deep link to a section ("tags": the Tags section): the view scrolls there
    // (sectionRequested). The "Their tags" fix link opens the window this way.
    Q_INVOKABLE void requestSection(const QString& section);

    bool backfillRunning() const { return m_backfillRunning; }
    // For tests: the tag data source (not taken over); nullptr -> its own.
    void setTagBackend(TagBackend* backend);

signals:
    void demoStateChanged();
    void queryChanged();
    void sortChanged();
    void listChanged();
    void selectionChanged();
    void detailChanged();
    void planChanged();
    void playingChanged();
    void undoChanged();
    void personTagsChanged();
    void backfillChanged();
    // Notice at the bottom of the window. undoable: with an "Undo" button.
    void toast(const QString& text, bool undoable);
    // The view should scroll to the section ("tags").
    void sectionRequested(const QString& section);

private:
    struct Person {
        tanara::PersonId id;            // null in the demo (fictional people)
        QString name;
        bool isSelf = false;
        QStringList aliases;
        QString note;
        int sampleCount = 0;
        int meetingCount = 0;
        qint64 talkMs = 0;
        QDateTime lastSeen;
        QString defaultSide;            // "local" | "remote" | ""
        bool defaultSideManual = false;
    };
    void initTimers();
    void resolveServices();
    void useServices(const UiServices& services);
    void scheduleRebuild();
    void rebuild();
    void rebuildList();
    void refreshDetail();
    void refreshPlan();
    void runNextVoiceprint();
    const Person* find(const QString& name) const;
    // The person behind a displayed name (the QML side selects / hands over names; the People window
    // keeps them unique). Null if unknown.
    tanara::PersonId idOf(const QString& name) const;
    QString rowMeta(const Person& p, const QString& aliasHit) const;
    QString personMeta(const Person& p) const;
    void ensureBackend();
    void finishPlayback();
    void attachTags(TagBackend* backend);
    void refreshTags();
    void refreshBackfill();
    QString tagNameOf(const QString& id) const;
    void tagStep(const QString& label);
    bool isPlaceholderName(const QString& name) const;
    void decorateSamples();

    bool m_explicitServices = false;
    tanara::library::IPeopleFacade* m_facade = nullptr;   // shared (real) or ours (fictional)
    QList<QMetaObject::Connection> m_connections;

    QString m_demoState;
    bool m_demo = false;                // the facade is fictional (read-only)

    PeopleListModel m_list;
    QVector<Person> m_persons;
    QString m_query;
    QString m_sort = QStringLiteral("abc");
    bool m_statsReady = false;
    bool m_rebuildScheduled = false;

    QString m_selected;
    Person m_detail;
    QString m_detailMeta;
    QVariantList m_samples;
    QString m_samplesSub;
    QVariantList m_meetings;

    bool m_planReady = false;
    bool m_planPossible = false;
    QString m_planText, m_planButton, m_planNote;
    QString m_planFor;              // whose plan it is
    QStringList m_planMeetingIds;
    int m_planGeneration = 0;
    bool m_creating = false;
    QString m_creatingText;
    QString m_creatingFor;
    QStringList m_createQueue;
    int m_createTotal = 0, m_createOk = 0, m_createFailed = 0;
    QString m_createError;

    TagBackend* m_tagBackend = nullptr;
    TagBackend* m_ownTagBackend = nullptr;
    QList<QMetaObject::Connection> m_tagConnections;
    QString m_tagsMode = QStringLiteral("none");
    QVariantList m_personTags;
    QVariantList m_tagSuggestions;
    QString m_tagHint;
    // The undo order between the two stacks ("people" | "tags"), newest last.
    QStringList m_undoOrder;

    bool m_backfillRunning = false;
    QTimer m_backfillPoll;

    PlayerBackend* m_backend = nullptr;
    QTimer m_playTick;
    QString m_playingId;
    qint64 m_playEndMs = 0;
};

} // namespace tanara_qml
