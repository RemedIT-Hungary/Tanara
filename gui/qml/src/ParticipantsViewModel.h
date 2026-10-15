#pragma once
//
// ParticipantsViewModel: the decision side of the Overview's "Who was there?" card
// (ParticipantsCard.qml; the card's rows come from OverviewViewModel). It keeps the candidates of
// the meeting (IParticipantsFacade::state) with their checkmarks (a working copy, restored by id
// when the facade reports a change) and does the card's approve / skip, the row operations
// (naming, same voice, confirm, "this is me", "they were not there") and the tag-learning
// question after an approval.
//
// Default checks: if there was an approval before (not a skip), that decision; otherwise
// participants::defaultChecked (has a name and no contradicting evidence); an invited but
// not heard candidate is unchecked by default.
//
// Learning question: after "Approve", if for one of the approved participants a tag of the
// meeting just reached c(p,t) = 2 (or a new person on a tagged meeting), and the tag is not
// on them / not rejected, `learnQuestion` gives a question (the largest group; the others are
// in the "One by one..." list). Asks only once (TagService::markLearningAsked). "Yes" / "No"
// are one undo step each on the tag stack.
//
// On the fictional facade (demo, screenshot: tanara_demo), FICTIONAL sample data.
//
#include "UiServices.h"

#include "tanara/domain/Person.h"
#include "tanara/editing/participants/IParticipantsFacade.h"
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

    // Whether the audio analysis is running.
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(bool hasTranscript READ hasTranscript NOTIFY changed)
    // Whether it awaits approval (the decision is open).
    Q_PROPERTY(bool approvalPending READ approvalPending NOTIFY changed)
    // Mono meta on the title's right: "voiceprint analysis · CAM++ + WeSpeaker · 3 min ago".
    Q_PROPERTY(QString metaText READ metaText NOTIFY changed)
    Q_PROPERTY(int checkedCount READ checkedCount NOTIFY changed)
    // The analysis' candidates (not the manually added ones): "N candidates await a decision".
    Q_PROPERTY(int candidateCount READ candidateCount NOTIFY changed)
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
    int checkedCount() const;
    int candidateCount() const { return m_candidateCount; }
    // The id of the candidate row with this name (case-insensitive); empty = none.
    Q_INVOKABLE QString participantIdOf(const QString& name) const;

    // Re-read from the facade; the working copy (the checks) is discarded.
    Q_INVOKABLE void reload();
    // "Approve": approves the checked ones. False: no meeting / the core rejected it.
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

    // ---- the card's row operations (immediate, one undo step each; the core's ParticipantService) ----
    // Names the row (a known person by id or name, else a new one); before the transcript too.
    Q_INVOKABLE bool nameParticipant(const QString& id, const QString& personIdOrName);
    // "Same voice as ...": the row `fromId` joins `intoId` (only in this meeting).
    Q_INVOKABLE bool mergeParticipantInto(const QString& fromId, const QString& intoId);
    // "Was there" (true) / its undo (false) on a doubtful row.
    Q_INVOKABLE bool confirmParticipant(const QString& id, bool confirmed);
    // "This is me": {ok: bool, reason: string (why not, e.g. no own name is set)}.
    Q_INVOKABLE QVariantMap setSelfParticipant(const QString& id);

signals:
    void meetingIdChanged();
    void demoStateChanged();
    void changed();
    void learnChanged();

private:
    struct Row {
        QString id;
        tanara::PersonId personId;      // the person (null: anonymous)
        QString name;                   // display name; empty = anonymous (unknown voice)
        bool checked = false;
        QStringList rawIds;
        QStringList sides;
    };

    void resolveServices();
    void useServices(const UiServices& services);
    void load();
    int indexOf(const QString& id) const;
    // A picker's text as a person choice: a known person (id or name), else a new one.
    tanara::PersonChoice choiceFor(const QString& personIdOrName) const;

    bool m_explicitServices = false;
    tanara::editing::IParticipantsFacade* m_facade = nullptr;   // shared (real) or ours (fictional)
    QList<QMetaObject::Connection> m_connections;

    QString m_meetingId;
    QString m_demoState;
    bool m_running = false;
    bool m_hasTranscript = false;
    bool m_pending = false;
    int m_candidateCount = 0;
    QString m_metaText;
    QVector<Row> m_rows;

    void setLearn(const QVector<tanara::library::PersonTagQuestion>& questions);
    QString answer(const tanara::library::PersonTagQuestion& q, bool yes);
    QVector<tanara::library::PersonTagQuestion> m_learn;   // the question: the first; the others in "One by one..."
    QVector<tanara::library::PersonTagQuestion> m_learnPairs;
};

} // namespace tanara_qml
