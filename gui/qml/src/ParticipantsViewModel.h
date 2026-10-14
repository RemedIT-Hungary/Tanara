#pragma once
//
// ParticipantsViewModel: view model of the "Who was there?" dialog (ParticipantsDialog.qml).
// It splits the meeting's participant candidates (IParticipantsFacade::state) into groups
// (Sure / Doubtful / Invited but not heard + the manually added, not yet heard), and keeps
// the checkmarks on a WORKING COPY: checking, adding and naming reach the meeting only on
// "Continue" (approve -> IParticipantsFacade::approve); "Skip" (skip -> skip)
// decides without binding.
//
// Default checks: if there was an approval before (not a skip), that decision; otherwise
// participants::defaultChecked (has a name and no contradicting evidence); an invited but
// not heard candidate is unchecked by default.
//
// "They were not there" (from the transcript editor): unbind(participantId) -> unbindParticipant.
//
// Learning question: after "Continue", if for one of the approved participants a tag of the
// meeting just reached c(p,t) = 2 (or a new person on a tagged meeting), and the tag is not
// on them / not rejected, `learnQuestion` gives a question (the largest group; the others are
// in the "One by one..." list). Asks only once (TagService::markLearningAsked). "Yes" / "No"
// are one undo step each on the tag stack.
//
// On the fictional facade (demo, screenshot: tanara_demo), FICTIONAL sample data; demoState chooses: allSure |
// doubts | invitedNotHeard | addPerson | running | noTranscriptYet | learnQuestion (question
// after approval, several pairs) | learnNewPerson (new person).
//
#include "UiServices.h"

#include "tanara/domain/Participant.h"
#include "tanara/domain/Tag.h"
#include "tanara/domain/Person.h"
#include "tanara/editing/session/SpeakerEditTypes.h"
#include "tanara/library/tags/PersonTags.h"

#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace tanara::editing { class IParticipantsFacade; }

namespace tanara_qml {

class ParticipantsViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString meetingId READ meetingId WRITE setMeetingId NOTIFY meetingIdChanged)
    Q_PROPERTY(QString demoState READ demoState WRITE setDemoState NOTIFY demoStateChanged)

    // Whether the audio analysis is running (the dialog's "running" state).
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(bool hasTranscript READ hasTranscript NOTIFY changed)
    // Whether it awaits approval (the condition for the banner / automatic opening).
    Q_PROPERTY(bool approvalPending READ approvalPending NOTIFY changed)
    // Mono meta on the title's right: "voiceprint analysis · CAM++ + WeSpeaker · 3 min ago".
    Q_PROPERTY(QString metaText READ metaText NOTIFY changed)
    // [{ key: sure|doubt|invited|manual, label, hint, count, rows: [row] }]; no empty group.
    // row: { id, name, displayName, colorIndex, checked, checkable, anonymous, added,
    //        evidence: [{ kind, polarity, text, side }], mapping, mappingPending, mappingDoubt }
    Q_PROPERTY(QVariantList groups READ groups NOTIFY changed)
    Q_PROPERTY(int rowCount READ rowCount NOTIFY changed)
    Q_PROPERTY(int checkedCount READ checkedCount NOTIFY changed)
    // The footer's info line (unbound raw speakers stay anonymous).
    Q_PROPERTY(QString infoText READ infoText NOTIFY changed)
    // The footer's constant rule sentence: "Tags only set the order; the voice decides."
    Q_PROPERTY(QString footerText READ footerText CONSTANT)
    // Tag-based suggestion: "Based on #Nordvik:" + [{ name, countText }] (at most 3).
    Q_PROPERTY(QString tagSuggestionLabel READ tagSuggestionLabel NOTIFY changed)
    Q_PROPERTY(QVariantList tagSuggestions READ tagSuggestions NOTIFY changed)
    // The learning question after approval: { text, tagName, people, newPeople, pairs: [{ index,
    // name, tagName, text }] }; empty map: no question. pairs is non-empty only with more than
    // one pair ("One by one...").
    Q_PROPERTY(QVariantMap learnQuestion READ learnQuestion NOTIFY learnChanged)

public:
    // QML: the services of the moment (follows App.controller / App.demo).
    explicit ParticipantsViewModel(QObject* parent = nullptr);
    // The participants facade of the given services.
    explicit ParticipantsViewModel(const UiServices& services, QObject* parent = nullptr);

    QString meetingId() const { return m_meetingId; }
    void setMeetingId(const QString& id);
    QString demoState() const { return m_demoState; }
    void setDemoState(const QString& state);
    bool demo() const;

    bool running() const { return m_running; }
    bool hasTranscript() const { return m_hasTranscript; }
    bool approvalPending() const { return m_pending; }
    QString metaText() const { return m_metaText; }
    QVariantList groups() const;
    int rowCount() const { return int(m_rows.size()); }
    int checkedCount() const;
    QString infoText() const;
    QString footerText() const { return tr("A címkék csak sorrendet adnak; a hang dönt."); }
    QString tagSuggestionLabel() const { return m_tagLabel; }
    QVariantList tagSuggestions() const;

    // For the person picker (PersonListModel.participants): known people, and whether someone
    // is already in the dialog.
    QVector<tanara::editing::PersonInfo> people() const;
    bool isMeetingPerson(const QString& name) const;

    // Re-read from the facade; the working copy (checks, additions, names) is discarded.
    // Called when the dialog opens.
    Q_INVOKABLE void reload();
    Q_INVOKABLE void setChecked(const QString& id, bool checked);
    // A new participant from the dialog (not saved yet; "Continue" takes it to the meeting). If
    // already present, it checks that one. Returns the row's id.
    Q_INVOKABLE QString addPerson(const QString& name);
    // Accepts the tag-based suggestion (index into tagSuggestions).
    Q_INVOKABLE void acceptTagSuggestion(int index);
    // Names an anonymous (unknown-voice) row; checks it.
    Q_INVOKABLE void nameRow(const QString& id, const QString& name);
    // Discards a row added in the dialog.
    Q_INVOKABLE void removeAdded(const QString& id);
    // "Continue": approves the checked ones. False: no meeting / the core rejected it.
    Q_INVOKABLE bool approve();
    QVariantMap learnQuestion() const;
    // Answers the question (Yes: the tag onto the people; No: lasting rejection). Returns the
    // feedback text (toast with "Undo"); empty = nothing happened. Afterwards there is no question.
    Q_INVOKABLE QString answerLearn(bool yes);
    // One pair from the "One by one..." list (index into pairs); the question lives on with the rest.
    Q_INVOKABLE QString answerLearnPair(int index, bool yes);
    // Closing / expiry of the bar: not a rejection (the suggestion stays in the People window).
    Q_INVOKABLE void dismissLearn();
    // "Skip": decision without binding.
    Q_INVOKABLE bool skip();
    // "They were not there": the participant's rows become anonymous, their check is cleared.
    Q_INVOKABLE bool unbind(const QString& participantId);

signals:
    void meetingIdChanged();
    void demoStateChanged();
    void changed();
    void learnChanged();

private:
    struct Row {
        QString id;
        tanara::PersonId personId;      // the person (null: anonymous, or a name typed in the dialog)
        QString name;                   // display name; empty = anonymous (unknown voice)
        int group = 1;                  // 0 sure, 1 doubtful, 2 invited, 3 added
        bool checked = false;
        bool added = false;             // added in the dialog (not yet on the meeting)
        bool heard = false;             // heard (has a voice cluster / side)
        bool manual = false;            // added manually (in the dialog or earlier)
        QVector<tanara::Evidence> evidence;
        QStringList rawIds;
        QStringList sides;
        int colorIndex = 0;
    };

    void resolveServices();
    void useServices(const UiServices& services);
    void load();
    void reloadTagSuggestions();
    QVariantMap rowMap(const Row& r) const;
    bool rawBoundToChecked(const QString& rawLabel, const QString& exceptId) const;
    int indexOf(const QString& id) const;
    int indexOfName(const QString& name) const;
    QString nextAddedId();

    bool m_explicitServices = false;
    tanara::editing::IParticipantsFacade* m_facade = nullptr;   // shared (real) or ours (fictional)
    QList<QMetaObject::Connection> m_connections;

    QString m_meetingId;
    QString m_demoState;
    bool m_running = false;
    bool m_hasTranscript = false;
    bool m_pending = false;
    QString m_metaText;
    QString m_selfName;
    QStringList m_rawLabels;            // the transcript's raw speakers (empty without a transcript)
    QStringList m_tagIds;
    QVector<Row> m_rows;
    int m_addedSeq = 0;

    struct TagSuggestion { QString name; QString tagName; int shared = 0; int total = 0; tanara::PersonId personId; };
    QString m_tagLabel;
    QVector<TagSuggestion> m_tagSuggestions;
    void setLearn(const QVector<tanara::library::PersonTagQuestion>& questions);
    QString answer(const tanara::library::PersonTagQuestion& q, bool yes);
    QVector<tanara::library::PersonTagQuestion> m_learn;   // the question: the first; the others in "One by one..."
    QVector<tanara::library::PersonTagQuestion> m_learnPairs;
};

} // namespace tanara_qml
