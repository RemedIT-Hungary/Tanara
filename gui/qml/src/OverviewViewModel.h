#pragma once
//
// OverviewViewModel: data of the Overview tab for one meeting: what it is about (manual
// description), who was there (participants with source badges, speaking share, tag-based
// suggestions), the approval banner, the processing steps and the finished meeting's numbers
// (DATA). The track list belongs to TrackListModel.
//
// Sources: AppController::participants / participantApprovalPending /
// participantAnalysisRunning, the SpeakerEditor (speakers, utterances, to-review), the
// MeetingJobTracker (running transcription / mixdown / summary), TagService::suggestPeopleForMeeting.
// It recomputes, delayed by one turn, on the core's signals (participantsChanged, tracksChanged,
// transcriptReady, summaryReady, jobProgressChanged ...).
//
// Without a controller (App.demo, screenshot) fictional data: demoState = overviewProcessing |
// overviewDone | overviewEmpty | overviewNobody | overviewRecorded (after recording, before
// transcription) | overviewRecordedError (the transcription failed).
//
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace tanara {
class AppController;
}

namespace tanara_qml {

class OverviewViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QObject* controller READ controllerObject WRITE setController NOTIFY controllerChanged)
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
    //          anonymous (anonymous, can be named), sources: [{ icon, text, negative, accent, detail }] }
    // The badge's detail is the tooltip (the evidence's explanation). After the transcript, after
    // the speakers' rows come, dimmed, the participants not heard / added manually (unheard, "not
    // heard" badge, AppController::unheardParticipants), then at most 3 tag-based suggestions.
    Q_PROPERTY(QVariantList participants READ participants NOTIFY changed)
    // The raw speakers of the "nobody" state: [{ name, share (0..1), colorIndex }]
    Q_PROPERTY(QVariantList anonymousSpeakers READ anonymousSpeakers NOTIFY changed)
    // The dim line next to "WHO WAS THERE": sources, or "approved Oct 1, 4:40 PM".
    Q_PROPERTY(QString peopleHint READ peopleHint NOTIFY changed)
    Q_PROPERTY(bool approved READ approved NOTIFY changed)
    Q_PROPERTY(bool approvalPending READ approvalPending NOTIFY changed)
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
    // DATA tiles: [{ label, value, sub }]
    Q_PROPERTY(QVariantList stats READ stats NOTIFY changed)
    // Subtext of the "All steps done" row ("recording · mixdown · transcript · summary").
    Q_PROPERTY(QString stepsSummary READ stepsSummary NOTIFY changed)

public:
    explicit OverviewViewModel(QObject* parent = nullptr);

    QObject* controllerObject() const;
    void setController(QObject* controller);
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
    QVariantList anonymousSpeakers() const { return m_anonymous; }
    QString peopleHint() const { return m_peopleHint; }
    bool approved() const { return m_approved; }
    bool approvalPending() const { return m_approvalPending; }
    bool analysisRunning() const { return m_analysisRunning; }
    int candidateCount() const { return m_candidateCount; }
    QVariantMap sideWho() const { return m_sideWho; }
    QVariantList steps() const { return m_steps; }
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
    // In the demo, naming an anonymous row in place (true); on real data false: the editor's
    // reassignSpeaker does the work there (one undo step, change bar).
    Q_INVOKABLE bool nameSpeakerInDemo(const QString& speakerKey, const QString& name);
    // Saves the manual description (Meeting::contextNote).
    Q_INVOKABLE void setContextNote(const QString& note);
    Q_INVOKABLE void refresh();

    // Monogram from a name ("Varga Árpád" -> "VÁ", "Remote 1" -> "R1"). Static: testable.
    static QString monogramOf(const QString& name);

signals:
    void controllerChanged();
    void meetingIdChanged();
    void demoStateChanged();
    void changed();

private:
    tanara::AppController* app() const;
    void connectController();
    void scheduleReload();
    void reload();
    void loadDemo();

    QPointer<QObject> m_injected;
    QPointer<tanara::AppController> m_connected;
    QList<QMetaObject::Connection> m_connections;
    QTimer m_reloadTimer;

    QString m_meetingId;
    QString m_demoState;
    QString m_demoLoadedFor;

    // The audio analysis progress (participantAnalysisProgress): track k / N and percent.
    struct AnalysisProgress {
        int done = 0;
        int total = 0;
        int percent = -1;
    };
    AnalysisProgress m_analysisProgress;

    bool m_hasTranscript = false;
    QString m_title;
    bool m_canReanalyze = false;
    bool m_transcribing = false;
    bool m_allDone = false;
    QString m_contextNote;
    QString m_peopleState = QStringLiteral("empty");
    QVariantList m_participants;
    QVariantList m_anonymous;
    QString m_peopleHint;
    bool m_approved = false;
    bool m_approvalPending = false;
    bool m_analysisRunning = false;
    int m_candidateCount = 0;
    QVariantMap m_sideWho;
    QVariantList m_steps;
    QVariantList m_stats;
    QString m_stepsSummary;
};

} // namespace tanara_qml
