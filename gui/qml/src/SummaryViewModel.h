#pragma once
//
// Tanara QML: view model of the Summary tab (SummaryTab.qml).
//   view "empty"   : no summary yet (two chooser cards, gating, running state);
//   view "summary" : finished summary, structured + stale marker + metadata;
//   view "topics"  : per-topic analysis (TopicListModel).
//
// It does NOT start the summary / topic suggestion / analysis (that is the shell's job:
// shell.start...: gating + Tanara Cloud estimate); it provides the state and does the view's
// small actions (clipboard, dismissing the stale marker, dismissing a leftover error).
//
// Participants' speaking-time share and the owner chips' colors come from the speaker
// editor's (SpeakerEditor) speaker list. A timestamp link exists only where the decision's
// text really starts with a timestamp (today's summaries contain none).
//
// A finished quick summary has TWO forms: the executive summary (short: overview, decisions,
// open questions, action items) and the memo (chronological, per-section notes with time
// ranges). The view (section) defaults to the executive summary; the memo is one click away.
// For an old (pre-memo) summary memoState is "missing"; in per-topic mode "none" (there the
// topics are the long form).
//
// Without a controller or with App.demo, fictional sample data; demoState: "stale" (default) |
// "done" | "memo" (many sections) | "memoShort" | "oldSummary" (no memo) | "oldMemo" (same,
// the memo's place is visible) | "running" (regeneration running) | "topicsDoc" | "empty" |
// "emptyBlocked" | "emptyRunning" | "emptyRunningParts" | "emptyRunningMerge" | "emptyError" |
// "emptyErrorKept" | "emptyErrorContext" (context error, with the LM Studio fix) |
// "emptyNote" (empty, with template suggestions for the note) | "noteOpen" (done, note block
// open, with suggestions) | "noteChanged" (done, the note changed since the summary) |
// "topics" | with source links: "sourcesOn" (source chips, first statement selected) |
// "sourcesOff" (the Sources switch off) | "sourcePopover" ("Where does this come from?" open) |
// "memoSections" (memo with table of contents, 3rd section) | "staleTargeted" (targeted
// staleness) | "noSources" (the same without statements: the old view).
//
// Source links: the quick summary's statements (AppController::summaryStatements) in five
// StatementListModels (sentences, decisions, open questions, action items, memo); the source
// quotes come from the speaker editor's utterances (sourceDetails). The map dock's rows:
// sourceMarks (the statements' sources, active = selected / affected by staleness) and
// sectionBands (memo sections).
//
// The meeting note (note, MeetingNoteModel) is editable here too: the summary and a
// re-transcription both receive it. If it changed since the summary (summary.json records
// what it was made with), noteChangedSinceSummary signals it gently; the library does NOT
// mark it stale.
//
#include "MeetingNoteModel.h"
#include "StatementListModel.h"
#include "TopicListModel.h"

#include "tanara/Types.h"
#include "tanara/jobs/JobTypes.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace tanara {
class AppController;
class SpeakerEditor;
}

namespace tanara_qml {

class SummaryViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QObject* controller READ controllerObject WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(QString meetingId READ meetingId WRITE setMeetingId NOTIFY meetingIdChanged)
    Q_PROPERTY(QString demoState READ demoState WRITE setDemoState NOTIFY demoStateChanged)
    Q_PROPERTY(bool demo READ demo NOTIFY changed)

    // "none" | "empty" | "summary" | "topics"
    Q_PROPERTY(QString view READ view NOTIFY changed)
    // Whether the per-topic workspace is open (the user toggles it; for a new meeting: there is a
    // topic list but no summary yet -> open, so an interrupted analysis can be resumed).
    Q_PROPERTY(bool topicsOpen READ topicsOpen WRITE setTopicsOpen NOTIFY changed)
    Q_PROPERTY(tanara_qml::TopicListModel* topics READ topics CONSTANT)
    Q_PROPERTY(bool hasTopics READ hasTopics NOTIFY changed)
    Q_PROPERTY(bool hasSummary READ hasSummary NOTIFY changed)

    // ---- gating + provider (empty view, and every start button) ----
    Q_PROPERTY(bool canRun READ canRun NOTIFY changed)
    Q_PROPERTY(QVariantMap blocker READ blocker NOTIFY changed)
    Q_PROPERTY(QString providerLabel READ providerLabel NOTIFY changed)
    Q_PROPERTY(bool cloudSelected READ cloudSelected NOTIFY changed)
    Q_PROPERTY(QString cloudTierLabel READ cloudTierLabel NOTIFY changed)
    Q_PROPERTY(bool cloudTeaser READ cloudTeaser NOTIFY changed)
    Q_PROPERTY(QString transcriptLine READ transcriptLine NOTIFY changed)

    // ---- running task (summary / topic suggestion / closing summary) ----
    Q_PROPERTY(bool jobRunning READ jobRunning NOTIFY jobChanged)
    Q_PROPERTY(int jobKind READ jobKind NOTIFY jobChanged)          // tanara::JobKind (JobKinds.*)
    Q_PROPERTY(QString jobTitle READ jobTitle NOTIFY jobChanged)
    Q_PROPERTY(QString jobMessage READ jobMessage NOTIFY jobChanged)
    Q_PROPERTY(bool jobCancelling READ jobCancelling NOTIFY jobChanged)
    // Whether the topic analysis queue is running (header: "Cancel" in place of "Analyze missing").
    Q_PROPERTY(bool analyzing READ analyzing NOTIFY jobChanged)
    // The summary's phases (SummaryProgress): "" | single | notes | merge; the running phase on
    // one line; a real percentage only for per-part note taking (-1: indeterminate).
    Q_PROPERTY(QString jobStage READ jobStage NOTIFY jobChanged)
    Q_PROPERTY(QString jobStageLabel READ jobStageLabel NOTIFY jobChanged)
    Q_PROPERTY(int jobPercent READ jobPercent NOTIFY jobChanged)
    // [{ id, label, state, percent, detail }]: the phase list for several phases
    Q_PROPERTY(QVariantList jobStages READ jobStages NOTIFY jobChanged)
    Q_PROPERTY(int jobReusedParts READ jobReusedParts NOTIFY jobChanged)
    Q_PROPERTY(QString jobReusedNote READ jobReusedNote NOTIFY jobChanged)

    // ---- leftover error (the last summary attempt failed) ----
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(QString errorDetail READ errorDetail NOTIFY changed)
    Q_PROPERTY(QString fixActionLabel READ fixActionLabel NOTIFY changed)
    Q_PROPERTY(QString fixActionPage READ fixActionPage NOTIFY changed)
    // Context error with LM Studio: "Load with larger context": the model reloads with this many
    // tokens (shell.requestLlmContext), then the task restarts; 0 -> no such button.
    Q_PROPERTY(int fixReloadContext READ fixReloadContext NOTIFY changed)
    // The failed run's finished partial notes exist (summary.notes.json): a retry resumes from there.
    Q_PROPERTY(bool errorKeptParts READ errorKeptParts NOTIFY changed)

    // ---- M07 ----
    Q_PROPERTY(bool stale READ stale NOTIFY changed)
    Q_PROPERTY(int staleCount READ staleCount NOTIFY changed)
    Q_PROPERTY(QString mode READ mode NOTIFY changed)               // quick | topics | unknown
    Q_PROPERTY(QString modeLabel READ modeLabel NOTIFY changed)
    Q_PROPERTY(QString metaLine READ metaLine NOTIFY changed)
    Q_PROPERTY(QString modelLine READ modelLine NOTIFY changed)
    Q_PROPERTY(QString execSummary READ execSummary NOTIFY changed)
    // [{ text, ms (-1: no timestamp), stamp }]
    Q_PROPERTY(QVariantList decisions READ decisions NOTIFY changed)
    // [{ text, ms (-1: no timestamp), stamp }], like the decisions
    Q_PROPERTY(QVariantList openQuestions READ openQuestions NOTIFY changed)
    // [{ text, owner, ownerIndex (-1: not the meeting's speaker), owners: [{ name, index }], due }]
    Q_PROPERTY(QVariantList actions READ actions NOTIFY changed)
    // [{ name, colorIndex, percent (-1: no data) }]
    Q_PROPERTY(QVariantList participants READ participants NOTIFY participantsChanged)
    // Topic sections of the per-topic summary: [{ title, detail, decisions, openQuestions, actions }]
    Q_PROPERTY(QVariantList topicSections READ topicSections NOTIFY changed)
    // The memo's sections in time order: [{ title, startMs (-1: unknown), endMs, stamp ("12:40",
    // empty if unknown), range ("12:40-15:55"), sourceRange ("31:10-44:05", min:sec), points:
    // [string], speakers: [{ name, colorIndex }], speakersText ("A · B · C"), introRows: [row]
    // (the intro's sentences), blocks: [{ key ("" | arguments | open | own), title (empty for the
    // body), rows: [row] }] }]; a row has the shape of StatementListModel::rowMap (without a
    // statement: statementId "" and no source).
    Q_PROPERTY(QVariantList memo READ memo NOTIFY changed)
    // "none" (per-topic mode: no memo view) | "missing" (old summary) | "ready"
    Q_PROPERTY(QString memoState READ memoState NOTIFY changed)
    // The visible part of the finished summary: "exec" (executive summary) | "memo"
    Q_PROPERTY(QString section READ section WRITE setSection NOTIFY sectionChanged)

    // ---- source links ----
    Q_PROPERTY(bool hasStatements READ hasStatements NOTIFY changed)
    Q_PROPERTY(tanara_qml::StatementListModel* sentences READ sentences CONSTANT)
    Q_PROPERTY(tanara_qml::StatementListModel* decisionItems READ decisionItems CONSTANT)
    Q_PROPERTY(tanara_qml::StatementListModel* todoItems READ todoItems CONSTANT)
    // The open questions as statements (with a source chip); empty: old summary (openQuestions stays).
    Q_PROPERTY(tanara_qml::StatementListModel* questionItems READ questionItems CONSTANT)
    // The memo's statements (section intro sentences "i<n>.<k>", points "m<n>.<k>") in one list;
    // the memo[] sections get their introRows / blocks rows from these.
    Q_PROPERTY(tanara_qml::StatementListModel* memoItems READ memoItems CONSTANT)
    // The "Sources" switch: time chips and the map's "Source" row (on by default).
    Q_PROPERTY(bool sourcesVisible READ sourcesVisible WRITE setSourcesVisible NOTIFY sourcesVisibleChanged)
    // The selected (hovered / open in the popover) statement; "" = none.
    Q_PROPERTY(QString activeStatementId READ activeStatementId WRITE setActiveStatementId NOTIFY marksChanged)
    // The memo's highlighted section (per table of contents / scroll); -1 = none.
    Q_PROPERTY(int activeSection READ activeSection WRITE setActiveSection NOTIFY marksChanged)
    // Map dock: [{ startMs, endMs, active }]: the source ranges of all statements
    // (Sources switch off: empty).
    Q_PROPERTY(QVariantList sourceMarks READ sourceMarks NOTIFY marksChanged)
    // Map dock: [{ startMs, endMs, active, label }]: the memo's sections (label: "3").
    Q_PROPERTY(QVariantList sectionBands READ sectionBands NOTIFY marksChanged)
    // Help text of the dock's control row ("blue: the selected statement's source, ...").
    Q_PROPERTY(QString mapHint READ mapHint NOTIFY marksChanged)
    Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY changed)
    // The toolbar's generation info: "Quick summary · Oct 1, 5:05 PM · LM Studio · from 1298
    // utterances" (for the memo "Memo · 7 sections · ..."; when stale "· 3 speaker fixes since").
    Q_PROPERTY(QString generationLine READ generationLine NOTIFY generationLineChanged)
    // Targeted staleness (staleTargeted: statement-level data exists).
    Q_PROPERTY(bool staleTargeted READ staleTargeted NOTIFY changed)
    Q_PROPERTY(int affectedStatements READ affectedStatements NOTIFY changed)
    Q_PROPERTY(int affectedTodos READ affectedTodos NOTIFY changed)
    Q_PROPERTY(int ownerChanges READ ownerChanges NOTIFY changed)

    // ---- meeting note ----
    Q_PROPERTY(tanara_qml::MeetingNoteModel* note READ note CONSTANT)
    // Whether the note block beside the finished summary is open (closes on meeting switch).
    Q_PROPERTY(bool noteOpen READ noteOpen WRITE setNoteOpen NOTIFY noteOpenChanged)
    // The note differs from the one the current summary was made with (for an old summary,
    // where this is unknown, always false).
    Q_PROPERTY(bool noteChangedSinceSummary READ noteChangedSinceSummary NOTIFY noteHintChanged)

public:
    explicit SummaryViewModel(QObject* parent = nullptr);

    QObject* controllerObject() const;
    void setController(QObject* controller);
    QString meetingId() const { return m_meetingId; }
    void setMeetingId(const QString& id);
    QString demoState() const { return m_demoState; }
    void setDemoState(const QString& state);
    bool demo() const;

    QString view() const;
    bool topicsOpen() const { return m_topicsOpen; }
    void setTopicsOpen(bool open);
    TopicListModel* topics() const { return m_topics; }
    bool hasTopics() const { return m_topics->count() > 0; }
    bool hasSummary() const { return m_hasSummary; }

    bool canRun() const { return m_canRun; }
    QVariantMap blocker() const { return m_blocker; }
    QString providerLabel() const { return m_providerLabel; }
    bool cloudSelected() const { return m_cloudSelected; }
    QString cloudTierLabel() const { return m_cloudTierLabel; }
    bool cloudTeaser() const { return m_cloudTeaser; }
    QString transcriptLine() const { return m_transcriptLine; }

    bool jobRunning() const { return m_jobKind >= 0; }
    int jobKind() const { return m_jobKind; }
    QString jobTitle() const { return m_jobTitle; }
    QString jobMessage() const { return m_jobMessage; }
    bool jobCancelling() const { return m_jobCancelling; }
    bool analyzing() const { return m_analyzing; }
    QString jobStage() const { return m_jobStage; }
    QString jobStageLabel() const { return m_jobStageLabel; }
    int jobPercent() const { return m_jobPercent; }
    QVariantList jobStages() const { return m_jobStages; }
    int jobReusedParts() const { return m_jobReusedParts; }
    QString jobReusedNote() const { return m_jobReusedNote; }

    QString errorMessage() const { return m_errorMessage; }
    QString errorDetail() const { return m_errorDetail; }
    QString fixActionLabel() const { return m_fixActionLabel; }
    QString fixActionPage() const { return m_fixActionPage; }
    int fixReloadContext() const { return m_fixReloadContext; }
    bool errorKeptParts() const { return m_errorKeptParts; }

    bool stale() const { return m_stale; }
    int staleCount() const { return m_staleCount; }
    QString mode() const { return m_mode; }
    QString modeLabel() const;
    QString metaLine() const { return m_metaLine; }
    QString modelLine() const { return m_modelLine; }
    QString execSummary() const { return m_execSummary; }
    QVariantList decisions() const { return m_decisions; }
    QVariantList openQuestions() const { return m_openQuestions; }
    QVariantList actions() const { return m_actions; }
    QVariantList participants() const { return m_participants; }
    QVariantList topicSections() const { return m_topicSections; }
    QVariantList memo() const { return m_memo; }
    QString memoState() const;
    QString section() const { return m_section; }
    void setSection(const QString& section);

    bool hasStatements() const { return m_hasStatements; }
    StatementListModel* sentences() const { return m_sentences; }
    StatementListModel* decisionItems() const { return m_decisionItems; }
    StatementListModel* todoItems() const { return m_todoItems; }
    StatementListModel* questionItems() const { return m_questionItems; }
    StatementListModel* memoItems() const { return m_memoItems; }
    bool sourcesVisible() const { return m_sourcesVisible; }
    void setSourcesVisible(bool on);
    QString activeStatementId() const { return m_activeStatementId; }
    void setActiveStatementId(const QString& id);
    int activeSection() const { return m_activeSection; }
    void setActiveSection(int index);
    QVariantList sourceMarks() const;
    QVariantList sectionBands() const;
    QString mapHint() const;
    qint64 durationMs() const { return m_durationMs; }
    QString generationLine() const;
    bool staleTargeted() const { return m_staleTargeted; }
    int affectedStatements() const { return m_affectedStatements; }
    int affectedTodos() const { return m_affectedTodos; }
    int ownerChanges() const { return m_ownerChanges; }

    MeetingNoteModel* note() const { return m_note; }
    bool noteOpen() const { return m_noteOpen; }
    void setNoteOpen(bool open);
    bool noteChangedSinceSummary() const;

    Q_INVOKABLE void refresh();
    // The summary's markdown: part "exec" (executive summary + lists + participants),
    // "memo" (memo only), "all" / empty (the whole summary.md). Empty if there is nothing to give.
    Q_INVOKABLE QString markdownFor(const QString& part) const;
    // The same to the clipboard. True if there was something to copy.
    Q_INVOKABLE bool copyToClipboard(const QString& part = QString());
    // "Fine as it is": dismisses the stale marker without regenerating.
    Q_INVOKABLE void dismissStale();
    // Dismisses the leftover error.
    Q_INVOKABLE void clearError();

    // "Where does this come from?": { statementId, text, kind, flagged, count, quotes: [{ utteranceId,
    // name, colorIndex (-1: unknown), startMs, endMs, stamp, text }] }: one utterance per quote
    // from the source ranges, in time order. Unknown statement -> empty map.
    Q_INVOKABLE QVariantMap sourceDetails(const QString& statementId) const;
    // "Not how it was said? Report": AppController::flagStatement (in memory in the demo).
    Q_INVOKABLE bool flagStatement(const QString& statementId);

    // Splits the timestamp off the start of a decision: "[12:52] text" -> ms + text.
    // No timestamp -> -1 and the text unchanged. (Public: the test calls it too.)
    static qint64 splitTimestamp(const QString& text, QString* rest);

signals:
    void controllerChanged();
    void meetingIdChanged();
    void demoStateChanged();
    void sectionChanged();
    void sourcesVisibleChanged();
    void marksChanged();
    void generationLineChanged();
    void noteOpenChanged();
    void noteHintChanged();
    void participantsChanged();
    void jobChanged();
    void changed();
    // The meeting's summary is done (the view switches back to the summary).
    void summaryArrived();

private:
    tanara::AppController* app() const;
    void connectController();
    void reload();
    void reloadJobs();
    void reloadParticipants();
    void loadDemo();
    void loadDemoMemo(bool longForm);
    void loadDemoSources(const QString& state);
    void reloadStatements();
    void buildStatements(const QVector<tanara::SummaryStatement>& statements,
                         const QStringList& affectedUtterances);
    QVariantMap memoMapFor(const tanara::MemoSection& sec, int index) const;
    void rebuildMemo();
    QList<StatementListModel*> statementModels() const;
    QVariantList speakerChips(const QStringList& names) const;
    void applyJob(const tanara::JobProgress& job);
    void watchEditor(tanara::SpeakerEditor* editor);
    int speakerIndexFor(const QString& name) const;
    QVariantList ownerList(const QString& owner) const;

    QPointer<QObject> m_injected;
    QPointer<tanara::AppController> m_connected;
    QList<QMetaObject::Connection> m_connections;
    QPointer<tanara::SpeakerEditor> m_editor;
    QMetaObject::Connection m_editorConn;

    TopicListModel* m_topics = nullptr;
    MeetingNoteModel* m_note = nullptr;
    bool m_noteOpen = false;
    QString m_summaryNote;              // the note the summary was made with
    bool m_summaryNoteKnown = false;
    QString m_meetingId;
    QString m_demoState;
    bool m_topicsOpen = false;
    bool m_valid = false;          // a meeting is loaded (or demo)
    bool m_hasSummary = false;

    bool m_canRun = false;
    QVariantMap m_blocker;
    QString m_providerLabel;
    bool m_cloudSelected = false;
    QString m_cloudTierLabel;
    bool m_cloudTeaser = false;
    QString m_transcriptLine;

    int m_jobKind = -1;
    QString m_jobTitle;
    QString m_jobMessage;
    bool m_jobCancelling = false;
    bool m_analyzing = false;
    QString m_jobStage;
    QString m_jobStageLabel;
    int m_jobPercent = -1;
    QVariantList m_jobStages;
    int m_jobReusedParts = 0;
    QString m_jobReusedNote;

    QString m_errorMessage;
    QString m_errorDetail;
    QString m_fixActionLabel;
    QString m_fixActionPage;
    int m_fixReloadContext = 0;
    bool m_errorKeptParts = false;

    bool m_stale = false;
    int m_staleCount = 0;
    QString m_mode = QStringLiteral("unknown");
    QString m_metaLine;
    QString m_modelLine;
    QString m_execSummary;
    QString m_markdown;
    QVariantList m_decisions;
    QVariantList m_openQuestions;
    QVariantList m_actions;
    QVariantList m_participants;
    QVariantList m_topicSections;
    QVariantList m_memo;
    QString m_section = QStringLiteral("exec");
    tanara::Summary m_summary;           // the structured form (for per-part copying)
    QStringList m_summaryParticipants;   // participant names per the summary
    QString m_metaDate;                  // "Oct 1, 5:05 PM" (for the generation line)
    QString m_metaProvider;              // "LM Studio · own key"

    // The meeting's speakers (name -> color index, share) for the owner chips and participants.
    struct SpeakerRef { QString name; QString personName; int colorIndex = 0; double share = 0.0; };
    QVector<SpeakerRef> m_speakers;
    qint64 m_durationMs = 0;

    // ---- source links ----
    StatementListModel* m_sentences = nullptr;
    StatementListModel* m_decisionItems = nullptr;
    StatementListModel* m_todoItems = nullptr;
    StatementListModel* m_questionItems = nullptr;
    StatementListModel* m_memoItems = nullptr;
    bool m_hasStatements = false;
    bool m_sourcesVisible = true;
    QString m_activeStatementId;
    int m_activeSection = -1;
    bool m_staleTargeted = false;
    int m_affectedStatements = 0;
    int m_affectedTodos = 0;
    int m_ownerChanges = 0;
    int m_sourceUtterances = 0;
    // The core's statements + the utterances affected by staleness (the models are built after
    // the speakers load, for the owner colors).
    QVector<tanara::SummaryStatement> m_rawStatements;
    QStringList m_affectedUtterances;          // the meeting's utterances ("from 1298 utterances")
    // An utterance for the quote (fictional in the demo; live from the speaker editor).
    struct QuoteLine { qint64 startMs = 0; qint64 endMs = 0; QString text; QString name; int colorIndex = -1; };
    QHash<QString, QuoteLine> m_demoLines;
    QuoteLine quoteLine(const QString& utteranceId) const;
};

} // namespace tanara_qml
