#pragma once
//
// OverviewViewModel — az Áttekintés fül (design/handoff-v3 V1/V3/V5/V6) adatai egy
// megbeszéléshez: miről szól (kézi leírás), kik voltak ott (résztvevők forrás-jelvényekkel,
// beszédaránnyal, címke alapú javaslatokkal), a jóváhagyás-banner, a feldolgozás lépései és
// a kész megbeszélés számai (ADATOK). A sávok listája a TrackListModel-é.
//
// Források: AppController::participants / participantApprovalPending /
// participantAnalysisRunning, a SpeakerEditor (beszélők, megszólalások, átnézendők), a
// MeetingJobTracker (futó átírás / lekeverés / összefoglaló), TagService::suggestPeopleForTag.
// A core jeleire (participantsChanged, tracksChanged, transcriptReady, summaryReady,
// jobProgressChanged …) késleltetve (egy körrel) újraszámol.
//
// Controller nélkül (App.demo, képernyőkép) kitalált adat: demoState = overviewProcessing (V1)
// | overviewDone (V3) | overviewEmpty (V5) | overviewNobody (V6).
//
#include <QObject>
#include <QPointer>
#include <QSet>
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
    // Fut-e az (első) átírás — átirat előtt ilyenkor az Áttekintés látszik a lépések helyett.
    Q_PROPERTY(bool transcribing READ transcribing NOTIFY changed)
    // Minden lépés kész (felvétel … összefoglaló): a lépések és a sávok egy-egy sorba csukva.
    Q_PROPERTY(bool allDone READ allDone NOTIFY changed)
    // A megbeszélés kézi leírása (Meeting::contextNote) — a „Miről szól” forrás-kártya.
    Q_PROPERTY(QString contextNote READ contextNote NOTIFY changed)

    // ---- kik voltak ott ----
    // "list" (sorok) | "empty" (még senki: V5) | "nobody" (átirat van, de mindenki névtelen: V6)
    Q_PROPERTY(QString peopleState READ peopleState NOTIFY changed)
    // Sorok: { id, name, monogram, colorIndex, sub, self, suggested, dimmed, removable,
    //          talkShare (0..1, -1 = nincs), sources: [{ icon, text, negative, accent }] }
    Q_PROPERTY(QVariantList participants READ participants NOTIFY changed)
    // A V6 nyers beszélői: [{ name, share (0..1), colorIndex }]
    Q_PROPERTY(QVariantList anonymousSpeakers READ anonymousSpeakers NOTIFY changed)
    // A „KIK VOLTAK OTT” melletti halvány sor: források, ill. „jóváhagyva okt. 1. 16:40-kor”.
    Q_PROPERTY(QString peopleHint READ peopleHint NOTIFY changed)
    Q_PROPERTY(bool approved READ approved NOTIFY changed)
    Q_PROPERTY(bool approvalPending READ approvalPending NOTIFY changed)
    Q_PROPERTY(bool analysisRunning READ analysisRunning NOTIFY changed)
    Q_PROPERTY(int candidateCount READ candidateCount NOTIFY changed)
    // Sávonként, ki beszél rajta: { mic: "Kovács Lilla", loopback: "4 résztvevő" }
    Q_PROPERTY(QVariantMap sideWho READ sideWho NOTIFY changed)

    // ---- feldolgozás ----
    // [{ key, title, sub, state: done|attention|running|waiting, parallel, progress }]
    // progress: 0..100, -1 = nincs csík, -2 = határozatlan csík.
    Q_PROPERTY(QVariantList steps READ steps NOTIFY changed)
    // ADATOK csempék: [{ label, value, sub }]
    Q_PROPERTY(QVariantList stats READ stats NOTIFY changed)
    // „Minden lépés kész” sor alszövege („felvétel · lekeverés · átirat · összefoglaló”).
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

    // „+ Résztvevő” / „Hozzáadás” (címke-javaslat): kézi résztvevő. Vissza: az id ("" = hiba).
    Q_INVOKABLE QString addParticipant(const QString& name);
    // ×: a résztvevő jelölése törlődik (a sorai névtelenek lesznek) — unbindParticipant.
    // Címke-javaslatnál (id "suggest:<név>") csak ebben a munkamenetben rejtjük el.
    Q_INVOKABLE void removeParticipant(const QString& id);
    // „Csak én beszéltem” (V5): setSoloMeeting. false: nincs saját név beállítva.
    Q_INVOKABLE bool setSolo();
    // A kézi leírás mentése (Meeting::contextNote).
    Q_INVOKABLE void setContextNote(const QString& note);
    Q_INVOKABLE void refresh();

    // Monogram egy névből („Varga Árpád” → „VÁ”, „Távoli 1” → „T1”). Statikus: tesztelhető.
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
    QSet<QString> m_hiddenSuggestions;
    QSet<QString> m_removedIds;          // ×-szel kivett résztvevők (a core nem törli őket)   // munkamenetben elutasított címke-javaslatok (név)

    bool m_hasTranscript = false;
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
