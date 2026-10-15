#pragma once
//
// OverviewViewModel: data of the Overview tab for one meeting: what it is about (manual
// description), who was there (participants with source badges, speaking share, tag-based
// suggestions), the approval banner, the processing steps and the finished meeting's numbers
// (DATA). The track list belongs to TrackListModel.
//
// It shows the snapshot of a tanara::library::IOverviewFacade (the real one builds it from the
// participants, the speaker editor, the job tracker and the tags; the fictional one serves a
// design state) and recomputes, delayed by one turn, when the facade says it changed. The view
// model never branches on which facade it has. demoState (the fictional facade's scenario):
// overviewProcessing | overviewDone | overviewEmpty | overviewNobody | overviewRecorded (after
// recording, before transcription) | overviewRecordedError (the transcription failed) |
// overviewTranscribing.
//
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace tanara::library { class IOverviewFacade; }

namespace tanara_qml {

class OverviewViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString meetingId READ meetingId WRITE setMeetingId NOTIFY meetingIdChanged)
    Q_PROPERTY(QString demoState READ demoState WRITE setDemoState NOTIFY demoStateChanged)

    Q_PROPERTY(bool hasTranscript READ hasTranscript NOTIFY changed)
    // Whether the (first) transcription runs: before a transcript, the Overview shows instead of the steps.
    Q_PROPERTY(bool transcribing READ transcribing NOTIFY changed)
    // Every step is done (recording ... summary): the steps and tracks collapse to one row each.
    Q_PROPERTY(bool allDone READ allDone NOTIFY changed)
    // The meeting's manual description (Meeting::contextNote): the "What it is about" source card.
    Q_PROPERTY(QString contextNote READ contextNote NOTIFY changed)
    // The meeting's title (before the transcript, for the title-based tag suggestion: MeetingTagsModel.draftTitle).
    Q_PROPERTY(QString title READ title NOTIFY changed)

    // ---- who was there ----
    // "list" (rows) | "empty" (nobody yet) | "nobody" (there is a transcript but everyone is anonymous)
    Q_PROPERTY(QString peopleState READ peopleState NOTIFY changed)
    // Rows: { id, name, monogram, colorIndex, sub, self, suggested, dimmed, removable, unheard,
    //          talkShare (0..1, -1 = none), speakerKey (the speaker's key after the transcript, else ""),
    //          anonymous (anonymous, can be named), approved (the record is approved / confirmed: "Was there"),
    //          doubtful (named and heard, but the approval leaves it out unless confirmed: "Was there"),
    //          counted (the approval takes it: "Approve · N"), voiceless (no voice of theirs: added by
    //          hand / not heard), lineCount (after the transcript: the speaker's lines, else 0),
    //          sampleTrackId / sampleStartMs / sampleEndMs (the sample to listen to, -1 = none),
    //          sources: [{ icon, text, negative, accent, detail }] }
    // The badge's detail is the tooltip (the evidence's explanation). After the transcript, after
    // the speakers' rows come, dimmed, the participants not heard / added manually (unheard, "not
    // heard" badge, AppController::unheardParticipants), then at most 3 tag-based suggestions.
    Q_PROPERTY(QVariantList participants READ participants NOTIFY changed)
    // The dim line next to "WHO WAS THERE": sources, or "approved Oct 1, 4:40 PM".
    Q_PROPERTY(QString peopleHint READ peopleHint NOTIFY changed)
    Q_PROPERTY(bool approved READ approved NOTIFY changed)
    Q_PROPERTY(bool approvalPending READ approvalPending NOTIFY changed)
    // "" | "pending" | "approved" | "skipped" | "solo" (OverviewSnapshot::decision).
    Q_PROPERTY(QString decision READ decision NOTIFY changed)
    // "Approve · N": the named rows the approval takes; after an approval the approved ones.
    Q_PROPERTY(int approveCount READ approveCount NOTIFY changed)
    // After the transcript: anonymous speakers without a line any more (not among the rows).
    Q_PROPERTY(int hiddenSpeakers READ hiddenSpeakers NOTIFY changed)
    // The user's own name ("This is me — <name>"); empty: not set.
    Q_PROPERTY(QString selfName READ selfName NOTIFY changed)
    Q_PROPERTY(bool analysisRunning READ analysisRunning NOTIFY changed)
    Q_PROPERTY(int candidateCount READ candidateCount NOTIFY changed)
    // Whether the voiceprint step's "Re-analyze" button may show: there is audio and a model, it
    // is not running, not "only I spoke".
    Q_PROPERTY(bool canReanalyze READ canReanalyze NOTIFY changed)
    // Per track, who speaks on it: { mic: "Kovács Lilla", loopback: "4 participants" }
    Q_PROPERTY(QVariantMap sideWho READ sideWho NOTIFY changed)

    // ---- processing ----
    // [{ key, title, sub, state: done|attention|running|waiting, parallel, progress }]
    // progress: 0..100, -1 = no bar, -2 = indeterminate bar.
    Q_PROPERTY(QVariantList steps READ steps NOTIFY changed)
    // The speakers' source and the manual "Beszélők felismerése" (D5): { source: provider | local | none, line,
    // fixTarget, canDiarize, hasNames, speakerCount, running, failed, problem }; empty without a transcript.
    Q_PROPERTY(QVariantMap speakers READ speakers NOTIFY changed)
    // DATA tiles: [{ label, value, sub }]
    Q_PROPERTY(QVariantList stats READ stats NOTIFY changed)
    // Subtext of the "All steps done" row ("recording · mixdown · transcript · summary").
    Q_PROPERTY(QString stepsSummary READ stepsSummary NOTIFY changed)

public:
    // From QML: follows the App singleton's services (AppContext::services().overview).
    explicit OverviewViewModel(QObject* parent = nullptr);
    // With a given facade (tests, hosts); it is not taken over.
    explicit OverviewViewModel(tanara::library::IOverviewFacade* facade, QObject* parent = nullptr);
    QString meetingId() const { return m_meetingId; }
    void setMeetingId(const QString& id);
    QString demoState() const { return m_demoState; }
    void setDemoState(const QString& state);

    bool hasTranscript() const { return m_hasTranscript; }
    bool transcribing() const { return m_transcribing; }
    bool allDone() const { return m_allDone; }
    QString contextNote() const { return m_contextNote; }
    QString title() const { return m_title; }
    bool canReanalyze() const { return m_canReanalyze; }
    QString peopleState() const { return m_peopleState; }
    QVariantList participants() const { return m_participants; }
    QString peopleHint() const { return m_peopleHint; }
    bool approved() const { return m_approved; }
    bool approvalPending() const { return m_approvalPending; }
    QString decision() const { return m_decision; }
    int approveCount() const { return m_approveCount; }
    int hiddenSpeakers() const { return m_hiddenSpeakers; }
    QString selfName() const { return m_selfName; }
    bool analysisRunning() const { return m_analysisRunning; }
    int candidateCount() const { return m_candidateCount; }
    QVariantMap sideWho() const { return m_sideWho; }
    QVariantList steps() const { return m_steps; }
    QVariantMap speakers() const { return m_speakers; }
    QVariantList stats() const { return m_stats; }
    QString stepsSummary() const { return m_stepsSummary; }

    // "+ Participant" / "Add" (tag suggestion): a manual participant. Returns the id ("" = error).
    Q_INVOKABLE QString addParticipant(const QString& name);
    // x: PERMANENT removal of the participant (AppController::removeParticipant: their rows
    // become anonymous, one step in the meeting's shared undo). For a tag suggestion (id
    // "suggest:<name>") a permanent rejection for this meeting (TagService::rejectPersonForMeeting, one step).
    Q_INVOKABLE void removeParticipant(const QString& id);
    // "Only I spoke": setSoloMeeting. False: no own name is set.
    Q_INVOKABLE bool setSolo();
    // "Re-analyze" before a transcript: the audio analysis again (AppController::analyzeParticipants).
    // After a transcript the shell's identifyParticipants runs (same as the menu's "Voiceprint analysis again").
    Q_INVOKABLE bool reanalyze();
    // "Beszélők felismerése": the local diarization on the existing transcript; count > 0: "How many speakers?"
    // (0 = automatic). False: not started. The confirmation (names reset) is the view's.
    Q_INVOKABLE bool diarizeSpeakers(int count);
    // In the demo, the card's decision ("approved" | "skipped") in place (true); on real data false:
    // ParticipantsViewModel approve / skip does the work (with the learning question).
    Q_INVOKABLE bool decideInDemo(const QString& decision);
    // ---- the "Who was there?" rows' operations (immediate, one undo step each) ----
    // Names the row (a known person by id or name, else a new one), before the transcript too; merges with
    // the person's manual voiceless row.
    Q_INVOKABLE bool nameParticipant(const QString& id, const QString& personIdOrName);
    // "Same voice as ...": the row `fromId` joins `intoId` (only in this meeting).
    Q_INVOKABLE bool mergeParticipantInto(const QString& fromId, const QString& intoId);
    // "Was there" (true) / its undo (false) on a doubtful row.
    Q_INVOKABLE bool confirmParticipant(const QString& id, bool confirmed);
    // "This is me": {ok: bool, reason: string (why not, e.g. no own name is set)}.
    Q_INVOKABLE QVariantMap setSelfParticipant(const QString& id);
    // Saves the manual description (Meeting::contextNote).
    Q_INVOKABLE void setContextNote(const QString& note);
    Q_INVOKABLE void refresh();

    // Monogram from a name ("Varga Árpád" -> "VÁ", "Remote 1" -> "R1"). Static: testable.
    static QString monogramOf(const QString& name);

signals:
    void meetingIdChanged();
    void demoStateChanged();
    void changed();

private:
    void init();
    void resolve();
    void attach(tanara::library::IOverviewFacade* facade);
    void scheduleReload();
    void reload();

    QPointer<tanara::library::IOverviewFacade> m_facade;
    bool m_injected = false;
    QTimer m_reloadTimer;

    QString m_meetingId;
    QString m_demoState;

    bool m_hasTranscript = false;
    QString m_title;
    bool m_canReanalyze = false;
    bool m_transcribing = false;
    bool m_allDone = false;
    QString m_contextNote;
    QString m_peopleState = QStringLiteral("empty");
    QVariantList m_participants;
    QString m_peopleHint;
    bool m_approved = false;
    bool m_approvalPending = false;
    QString m_decision;
    int m_approveCount = 0;
    int m_hiddenSpeakers = 0;
    QString m_selfName;
    bool m_analysisRunning = false;
    int m_candidateCount = 0;
    QVariantMap m_sideWho;
    QVariantList m_steps;
    QVariantMap m_speakers;
    QVariantList m_stats;
    QString m_stepsSummary;
};

} // namespace tanara_qml
