#pragma once
//
// Tanara QML — az Összefoglaló fül (SummaryTab.qml) nézetmodellje.
//   view "empty"   — M06: még nincs összefoglaló (két választó kártya, kapuzás, futó állapot);
//   view "summary" — M07: kész összefoglaló strukturáltan + elavult-jelzés + metaadat;
//   view "topics"  — M08: témánkénti elemzés (TopicListModel).
//
// Az összefoglalót / téma-javaslatot / elemzést NEM ez indítja (az a héj dolga: shell.start…
// — kapuzás + Tanara Cloud becslés); ez az állapotot adja, és a nézethez tartozó apró
// műveleteket végzi (vágólap, elavult-jelző elengedése, megmaradt hiba elvetése).
//
// A résztvevők beszédidő-aránya és a felelős-chipek színe a beszélő-szerkesztő
// (SpeakerEditor) beszélő-listájából jön. Időbélyeg-hivatkozás csak ott van, ahol a döntés
// szövege tényleg időbélyeggel kezdődik (a mai összefoglalók nem tartalmaznak ilyet).
//
// A kész gyors összefoglaló KÉT formája: a vezetői összefoglaló (rövid: áttekintés, döntések,
// nyitott kérdések, teendők) és a memó (időrendi, szakaszonkénti jegyzet időkerettel). A nézet
// (section) alapból a vezetői összefoglaló; a memó egy kattintásra van. Régi (memó előtti)
// összefoglalónál memoState "missing"; témánkénti módban "none" (ott a témák a hosszú forma).
//
// Controller nélkül vagy App.demo mellett kitalált mintaadat; demoState: "stale" (alap) |
// "done" | "memo" (sok szakasz) | "memoShort" | "oldSummary" (memó nélkül) | "oldMemo" (ua., a
// memó helye látszik) | "running" (újragenerálás fut) | "topicsDoc" | "empty" | "emptyBlocked" |
// "emptyRunning" | "emptyRunningParts" | "emptyRunningMerge" | "emptyError" | "emptyErrorKept" |
// "emptyErrorContext" (kontextus-hiba, LM Studio-javítással) | "emptyNote" (üres, sablon-
// javaslatokkal a megjegyzéshez) | "noteOpen" (kész, a megjegyzés-blokk nyitva, javaslatokkal) |
// "noteChanged" (kész, a megjegyzés az összefoglaló óta változott) |
// "topics" | v3 (S1–S3, a handoff-v3 mintaadatával): "sourcesOn" (forrás-chipek, az első
// állítás kijelölve) | "sourcesOff" (a Források kapcsoló kikapcsolva) | "sourcePopover" (a
// „Honnan jön ez?” nyitva) | "memoSections" (memó tartalomjegyzékkel, 3. szakasz) |
// "staleTargeted" (célzott elavulás) | "noSources" (ugyanez állítások nélkül: a régi nézet).
//
// Forrás-hivatkozások (v3, S1–S3): a gyors összefoglaló állításai (AppController::
// summaryStatements) három StatementListModel-ben; a forrás-idézetek a beszélő-szerkesztő
// megszólalásaiból (sourceDetails). A térkép-dokk sorai: sourceMarks (az állítások forrásai,
// active = a kijelölt / az elavulás által érintett) és sectionBands (memó-szakaszok).
//
// A megbeszélés-megjegyzés (note, MeetingNoteModel) itt is szerkeszthető: az összefoglaló és
// egy újra-átírás is ezt kapja. Ha az összefoglaló óta változott (a summary.json rögzíti, mivel
// készült), noteChangedSinceSummary szelíden jelzi — a könyvtárban NEM jelöli elavultnak.
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
    // A témánkénti munkaterület nyitva van-e (a felhasználó váltja; új meetingnél: van téma-
    // lista, de még nincs összefoglaló → nyitva, hogy a félbemaradt elemzés folytatható legyen).
    Q_PROPERTY(bool topicsOpen READ topicsOpen WRITE setTopicsOpen NOTIFY changed)
    Q_PROPERTY(tanara_qml::TopicListModel* topics READ topics CONSTANT)
    Q_PROPERTY(bool hasTopics READ hasTopics NOTIFY changed)
    Q_PROPERTY(bool hasSummary READ hasSummary NOTIFY changed)

    // ---- kapuzás + szolgáltató (M06, és minden indító gomb) ----
    Q_PROPERTY(bool canRun READ canRun NOTIFY changed)
    Q_PROPERTY(QVariantMap blocker READ blocker NOTIFY changed)
    Q_PROPERTY(QString providerLabel READ providerLabel NOTIFY changed)
    Q_PROPERTY(bool cloudSelected READ cloudSelected NOTIFY changed)
    Q_PROPERTY(QString cloudTierLabel READ cloudTierLabel NOTIFY changed)
    Q_PROPERTY(bool cloudTeaser READ cloudTeaser NOTIFY changed)
    Q_PROPERTY(QString transcriptLine READ transcriptLine NOTIFY changed)

    // ---- futó feladat (összefoglaló / téma-javaslat / záró összegzés) ----
    Q_PROPERTY(bool jobRunning READ jobRunning NOTIFY jobChanged)
    Q_PROPERTY(int jobKind READ jobKind NOTIFY jobChanged)          // tanara::JobKind (JobKinds.*)
    Q_PROPERTY(QString jobTitle READ jobTitle NOTIFY jobChanged)
    Q_PROPERTY(QString jobMessage READ jobMessage NOTIFY jobChanged)
    Q_PROPERTY(bool jobCancelling READ jobCancelling NOTIFY jobChanged)
    // A téma-elemzés sor fut-e (M08 fejléc: „Megszakítás” a „Hiányzók elemzése” helyén).
    Q_PROPERTY(bool analyzing READ analyzing NOTIFY jobChanged)
    // Az összefoglaló szakaszai (SummaryProgress): "" | single | notes | merge; a futó szakasz
    // egy sorban; valós százalék csak a részenkénti jegyzetelésnél (-1: határozatlan).
    Q_PROPERTY(QString jobStage READ jobStage NOTIFY jobChanged)
    Q_PROPERTY(QString jobStageLabel READ jobStageLabel NOTIFY jobChanged)
    Q_PROPERTY(int jobPercent READ jobPercent NOTIFY jobChanged)
    // [{ id, label, state, percent, detail }] — több szakasznál a szakasz-lista
    Q_PROPERTY(QVariantList jobStages READ jobStages NOTIFY jobChanged)
    Q_PROPERTY(int jobReusedParts READ jobReusedParts NOTIFY jobChanged)
    Q_PROPERTY(QString jobReusedNote READ jobReusedNote NOTIFY jobChanged)

    // ---- megmaradt hiba (az utolsó összefoglaló-kísérlet elbukott) ----
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(QString errorDetail READ errorDetail NOTIFY changed)
    Q_PROPERTY(QString fixActionLabel READ fixActionLabel NOTIFY changed)
    Q_PROPERTY(QString fixActionPage READ fixActionPage NOTIFY changed)
    // Kontextus-hiba LM Studióval: „Betöltés nagyobb kontextussal” — ennyi tokennel töltődik
    // újra a modell (shell.requestLlmContext), majd a feladat újraindul; 0 → nincs ilyen gomb.
    Q_PROPERTY(int fixReloadContext READ fixReloadContext NOTIFY changed)
    // Az elbukott futás kész részjegyzetei megvannak (summary.notes.json): az újrapróbálás
    // onnan folytatja.
    Q_PROPERTY(bool errorKeptParts READ errorKeptParts NOTIFY changed)

    // ---- M07 ----
    Q_PROPERTY(bool stale READ stale NOTIFY changed)
    Q_PROPERTY(int staleCount READ staleCount NOTIFY changed)
    Q_PROPERTY(QString mode READ mode NOTIFY changed)               // quick | topics | unknown
    Q_PROPERTY(QString modeLabel READ modeLabel NOTIFY changed)
    Q_PROPERTY(QString metaLine READ metaLine NOTIFY changed)
    Q_PROPERTY(QString modelLine READ modelLine NOTIFY changed)
    Q_PROPERTY(QString execSummary READ execSummary NOTIFY changed)
    // [{ text, ms (-1: nincs időbélyeg), stamp }]
    Q_PROPERTY(QVariantList decisions READ decisions NOTIFY changed)
    // [{ text, ms (-1: nincs időbélyeg), stamp }] — mint a döntések
    Q_PROPERTY(QVariantList openQuestions READ openQuestions NOTIFY changed)
    // [{ text, owner, ownerIndex (-1: nem a meeting beszélője), owners: [{ name, index }], due }]
    Q_PROPERTY(QVariantList actions READ actions NOTIFY changed)
    // [{ name, colorIndex, percent (-1: nincs adat) }]
    Q_PROPERTY(QVariantList participants READ participants NOTIFY participantsChanged)
    // Témánkénti összefoglaló témaszekciói: [{ title, detail, decisions, openQuestions, actions }]
    Q_PROPERTY(QVariantList topicSections READ topicSections NOTIFY changed)
    // A memó szakaszai időrendben: [{ title, startMs (-1: ismeretlen), endMs, stamp („12:40”,
    // üres ha ismeretlen), range („12:40–15:55”), sourceRange („31:10–44:05”, perc:mp), points:
    // [string], speakers: [{ name, colorIndex }], speakersText („A · B · C”) }]
    Q_PROPERTY(QVariantList memo READ memo NOTIFY changed)
    // "none" (témánkénti mód: nincs memó-nézet) | "missing" (régi összefoglaló) | "ready"
    Q_PROPERTY(QString memoState READ memoState NOTIFY changed)
    // A kész összefoglaló látható része: "exec" (vezetői összefoglaló) | "memo"
    Q_PROPERTY(QString section READ section WRITE setSection NOTIFY sectionChanged)

    // ---- forrás-hivatkozások (v3: S1–S3) ----
    Q_PROPERTY(bool hasStatements READ hasStatements NOTIFY changed)
    Q_PROPERTY(tanara_qml::StatementListModel* sentences READ sentences CONSTANT)
    Q_PROPERTY(tanara_qml::StatementListModel* decisionItems READ decisionItems CONSTANT)
    Q_PROPERTY(tanara_qml::StatementListModel* todoItems READ todoItems CONSTANT)
    // A „Források” kapcsoló: idő-chipek és a térkép „Forrás” sora (alapból be).
    Q_PROPERTY(bool sourcesVisible READ sourcesVisible WRITE setSourcesVisible NOTIFY sourcesVisibleChanged)
    // A kijelölt (rámutatott / felugróban nyitott) állítás; "" = nincs.
    Q_PROPERTY(QString activeStatementId READ activeStatementId WRITE setActiveStatementId NOTIFY marksChanged)
    // A memó kiemelt szakasza (a tartalomjegyzék / görgetés szerint); -1 = nincs.
    Q_PROPERTY(int activeSection READ activeSection WRITE setActiveSection NOTIFY marksChanged)
    // Térkép-dokk: [{ startMs, endMs, active }] — az összes állítás forrás-tartománya
    // (a Források kapcsoló kikapcsolva: üres).
    Q_PROPERTY(QVariantList sourceMarks READ sourceMarks NOTIFY marksChanged)
    // Térkép-dokk: [{ startMs, endMs, active, label }] — a memó szakaszai (label: „3”).
    Q_PROPERTY(QVariantList sectionBands READ sectionBands NOTIFY marksChanged)
    // A dokk vezérlősorának súgója („kék: a kijelölt állítás forrása · …”).
    Q_PROPERTY(QString mapHint READ mapHint NOTIFY marksChanged)
    Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY changed)
    // Az eszköz-sor generálás-adata: „Gyors összefoglaló · okt. 1. 17:05 · LM Studio · 1298
    // megszólalásból” (memónál „Memó · 7 szakasz · …”; elavultnál „· azóta 3 beszélő-javítás”).
    Q_PROPERTY(QString generationLine READ generationLine NOTIFY generationLineChanged)
    // Célzott elavulás (staleTargeted: van állítás-szintű adat).
    Q_PROPERTY(bool staleTargeted READ staleTargeted NOTIFY changed)
    Q_PROPERTY(int affectedStatements READ affectedStatements NOTIFY changed)
    Q_PROPERTY(int affectedTodos READ affectedTodos NOTIFY changed)
    Q_PROPERTY(int ownerChanges READ ownerChanges NOTIFY changed)

    // ---- megbeszélés-megjegyzés ----
    Q_PROPERTY(tanara_qml::MeetingNoteModel* note READ note CONSTANT)
    // A kész összefoglaló melletti megjegyzés-blokk nyitva van-e (meeting-váltáskor bezárul).
    Q_PROPERTY(bool noteOpen READ noteOpen WRITE setNoteOpen NOTIFY noteOpenChanged)
    // A megjegyzés más, mint amivel a mostani összefoglaló készült (régi összefoglalónál,
    // ahol ez nem ismert, mindig false).
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
    // Az összefoglaló markdownja: part "exec" (vezetői összefoglaló + listák + résztvevők),
    // "memo" (csak a memó), "all" / üres (a teljes summary.md). Üres, ha nincs mit adni.
    Q_INVOKABLE QString markdownFor(const QString& part) const;
    // Ugyanez a vágólapra. true, ha volt mit másolni.
    Q_INVOKABLE bool copyToClipboard(const QString& part = QString());
    // „Rendben így”: az elavult-jelző elengedése újragenerálás nélkül.
    Q_INVOKABLE void dismissStale();
    // A megmaradt hiba elvetése.
    Q_INVOKABLE void clearError();

    // „Honnan jön ez?”: { statementId, text, kind, flagged, count, quotes: [{ utteranceId, name,
    // colorIndex (-1: ismeretlen), startMs, endMs, stamp, text }] } — idézetenként egy
    // megszólalás a forrás-tartományokból, időrendben. Ismeretlen állítás → üres map.
    Q_INVOKABLE QVariantMap sourceDetails(const QString& statementId) const;
    // „Nem így hangzott el? · Jelzem”: AppController::flagStatement (demóban memóriában).
    Q_INVOKABLE bool flagStatement(const QString& statementId);

    // Egy döntés elejéről az időbélyeg leválasztása: „[12:52] szöveg” → ms + szöveg.
    // Nincs időbélyeg → -1 és a szöveg változatlan. (Publikus: a teszt is hívja.)
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
    // Elkészült a meeting összefoglalója (a nézet visszavált az összefoglalóra).
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
    QVariantMap memoMapFor(const tanara::MemoSection& sec) const;
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
    QString m_summaryNote;              // a megjegyzés, amellyel az összefoglaló készült
    bool m_summaryNoteKnown = false;
    QString m_meetingId;
    QString m_demoState;
    bool m_topicsOpen = false;
    bool m_valid = false;          // van betöltött meeting (vagy demó)
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
    tanara::Summary m_summary;           // a strukturált forma (a részenkénti másoláshoz)
    QStringList m_summaryParticipants;   // az összefoglaló szerinti résztvevő-nevek
    QString m_metaDate;                  // „okt. 1. 17:05” (a generálás-sorhoz)
    QString m_metaProvider;              // „LM Studio · saját kulcs”

    // A meeting beszélői (név → szín-index, arány) a felelős-chipekhez és a résztvevőkhöz.
    struct SpeakerRef { QString name; QString personName; int colorIndex = 0; double share = 0.0; };
    QVector<SpeakerRef> m_speakers;
    qint64 m_durationMs = 0;

    // ---- forrás-hivatkozások ----
    StatementListModel* m_sentences = nullptr;
    StatementListModel* m_decisionItems = nullptr;
    StatementListModel* m_todoItems = nullptr;
    bool m_hasStatements = false;
    bool m_sourcesVisible = true;
    QString m_activeStatementId;
    int m_activeSection = -1;
    bool m_staleTargeted = false;
    int m_affectedStatements = 0;
    int m_affectedTodos = 0;
    int m_ownerChanges = 0;
    int m_sourceUtterances = 0;
    // A core állításai + az elavulás által érintett megszólalások (a felelős-színekhez a
    // beszélők betöltése után épülnek a modellek).
    QVector<tanara::SummaryStatement> m_rawStatements;
    QStringList m_affectedUtterances;          // a megbeszélés megszólalásai („1298 megszólalásból”)
    // Egy megszólalás az idézethez (demóban kitalált; élesben a beszélő-szerkesztőből).
    struct QuoteLine { qint64 startMs = 0; qint64 endMs = 0; QString text; QString name; int colorIndex = -1; };
    QHash<QString, QuoteLine> m_demoLines;
    QuoteLine quoteLine(const QString& utteranceId) const;
};

} // namespace tanara_qml
