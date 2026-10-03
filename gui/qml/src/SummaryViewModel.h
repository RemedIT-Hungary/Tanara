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
// Controller nélkül vagy App.demo mellett kitalált mintaadat; demoState: "stale" (alap) |
// "done" | "topicsDoc" | "empty" | "emptyBlocked" | "emptyRunning" | "emptyError" | "topics".
//
#include "TopicListModel.h"

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

    // ---- megmaradt hiba (az utolsó összefoglaló-kísérlet elbukott) ----
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(QString errorDetail READ errorDetail NOTIFY changed)
    Q_PROPERTY(QString fixActionLabel READ fixActionLabel NOTIFY changed)
    Q_PROPERTY(QString fixActionPage READ fixActionPage NOTIFY changed)

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
    // [{ text, owner, ownerIndex (-1: nem a meeting beszélője), owners: [{ name, index }], due }]
    Q_PROPERTY(QVariantList actions READ actions NOTIFY changed)
    // [{ name, colorIndex, percent (-1: nincs adat) }]
    Q_PROPERTY(QVariantList participants READ participants NOTIFY participantsChanged)
    // Témánkénti összefoglaló témaszekciói: [{ title, detail, decisions, actions }]
    Q_PROPERTY(QVariantList topicSections READ topicSections NOTIFY changed)

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

    QString errorMessage() const { return m_errorMessage; }
    QString errorDetail() const { return m_errorDetail; }
    QString fixActionLabel() const { return m_fixActionLabel; }
    QString fixActionPage() const { return m_fixActionPage; }

    bool stale() const { return m_stale; }
    int staleCount() const { return m_staleCount; }
    QString mode() const { return m_mode; }
    QString modeLabel() const;
    QString metaLine() const { return m_metaLine; }
    QString modelLine() const { return m_modelLine; }
    QString execSummary() const { return m_execSummary; }
    QVariantList decisions() const { return m_decisions; }
    QVariantList actions() const { return m_actions; }
    QVariantList participants() const { return m_participants; }
    QVariantList topicSections() const { return m_topicSections; }

    Q_INVOKABLE void refresh();
    // Az összefoglaló markdownja a vágólapra. true, ha volt mit másolni.
    Q_INVOKABLE bool copyToClipboard();
    // „Rendben így”: az elavult-jelző elengedése újragenerálás nélkül.
    Q_INVOKABLE void dismissStale();
    // A megmaradt hiba elvetése.
    Q_INVOKABLE void clearError();

    // Egy döntés elejéről az időbélyeg leválasztása: „[12:52] szöveg” → ms + szöveg.
    // Nincs időbélyeg → -1 és a szöveg változatlan. (Publikus: a teszt is hívja.)
    static qint64 splitTimestamp(const QString& text, QString* rest);

signals:
    void controllerChanged();
    void meetingIdChanged();
    void demoStateChanged();
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
    void watchEditor(tanara::SpeakerEditor* editor);
    int speakerIndexFor(const QString& name) const;
    QVariantList ownerList(const QString& owner) const;

    QPointer<QObject> m_injected;
    QPointer<tanara::AppController> m_connected;
    QList<QMetaObject::Connection> m_connections;
    QPointer<tanara::SpeakerEditor> m_editor;
    QMetaObject::Connection m_editorConn;

    TopicListModel* m_topics = nullptr;
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

    QString m_errorMessage;
    QString m_errorDetail;
    QString m_fixActionLabel;
    QString m_fixActionPage;

    bool m_stale = false;
    int m_staleCount = 0;
    QString m_mode = QStringLiteral("unknown");
    QString m_metaLine;
    QString m_modelLine;
    QString m_execSummary;
    QString m_markdown;
    QVariantList m_decisions;
    QVariantList m_actions;
    QVariantList m_participants;
    QVariantList m_topicSections;
    QStringList m_summaryParticipants;   // az összefoglaló szerinti résztvevő-nevek

    // A meeting beszélői (név → szín-index, arány) a felelős-chipekhez és a résztvevőkhöz.
    struct SpeakerRef { QString name; QString personName; int colorIndex = 0; double share = 0.0; };
    QVector<SpeakerRef> m_speakers;
    qint64 m_durationMs = 0;
};

} // namespace tanara_qml
