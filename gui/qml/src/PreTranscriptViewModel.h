#pragma once
//
// Tanara QML — az „átirat előtti” nézet (PreTranscriptView.qml) nézetmodellje. Egy átirat
// nélküli meeting három állapotát szolgálja ki a feldolgozási állapotból:
//   "steps"   — M03 lépések: kontextus, előkészítés, átírás őszinte kapuzással (canRun);
//   "running" — M04: az átírás-feladat szakaszai VALÓS haladással (JobProgress);
//   "failed"  — M05: a megmaradt hiba emberi üzenete + technikai sora + javító művelet.
//
// Az átírást NEM ez indítja (az a héj dolga: shell.startTranscription — kapuzás + cloud-
// becslés); ez csak állapotot ad, és a meetinghez tartozó beállításokat menti (kontextus,
// azonosítás kapcsoló, Tanara Cloud szint).
//
// Controller nélkül vagy App.demo mellett beépített, KITALÁLT mintaadatot mutat; a
// demoState választja ki, melyik állapotot ("steps" | "ready" | "cloud" | "running" |
// "uploading" | "failed" | "note" — lépések sablon-javaslatokkal és észlelt hívással).
//
#include "MeetingNoteModel.h"

#include "tanara/jobs/JobTypes.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace tanara {
class AppController;
}

namespace tanara_qml {

class PreTranscriptViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    // Alapból az App-singleton controllere; tesztben injektálható.
    Q_PROPERTY(QObject* controller READ controllerObject WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(QString meetingId READ meetingId WRITE setMeetingId NOTIFY meetingIdChanged)
    Q_PROPERTY(QString demoState READ demoState WRITE setDemoState NOTIFY demoStateChanged)
    Q_PROPERTY(bool demo READ demo NOTIFY changed)

    // "none" (nincs meeting) | "steps" | "running" | "failed"
    Q_PROPERTY(QString state READ state NOTIFY changed)

    // ---- M03 ----
    // A megjegyzés szerkesztése (piszkozat, mentés, sablon-javaslatok, észlelt hívás) — a
    // MeetingNoteEditor ezt kapja. A contextNote ennek a mentett értéke (rövidítés).
    Q_PROPERTY(tanara_qml::MeetingNoteModel* note READ note CONSTANT)
    Q_PROPERTY(QString contextNote READ contextNote WRITE setContextNote NOTIFY contextNoteChanged)
    Q_PROPERTY(bool identifyEnabled READ identifyEnabled WRITE setIdentifyEnabled NOTIFY changed)
    Q_PROPERTY(bool identifyAvailable READ identifyAvailable NOTIFY changed)
    // "ready" | "missing" | "stale" | "running"
    Q_PROPERTY(QString mixdownState READ mixdownState NOTIFY changed)
    Q_PROPERTY(int mixdownPercent READ mixdownPercent NOTIFY mixdownPercentChanged)
    // Önálló lekeverés fut (megszakítható a shell.cancelJob(…, JobKinds.Mixdown)-nal).
    Q_PROPERTY(bool mixdownCancellable READ mixdownCancellable NOTIFY changed)
    Q_PROPERTY(bool canStart READ canStart NOTIFY changed)
    // Üres, ha indítható; különben { title, text, actionLabel, actionPage, reason, kind }.
    Q_PROPERTY(QVariantMap blocker READ blocker NOTIFY changed)
    Q_PROPERTY(QString providerLabel READ providerLabel NOTIFY changed)

    // ---- Tanara Cloud (csak ha a build tartalmazza és az átírás szolgáltatója az) ----
    Q_PROPERTY(bool cloudSelected READ cloudSelected NOTIFY changed)       // szint-választó látszik
    Q_PROPERTY(QString cloudTier READ cloudTier WRITE setCloudTier NOTIFY changed)   // fast | accurate
    Q_PROPERTY(QString cloudExpertName READ cloudExpertName NOTIFY changed)          // nem üres → Expert-modell
    Q_PROPERTY(QString cloudLanguageLabel READ cloudLanguageLabel NOTIFY changed)
    Q_PROPERTY(QString cloudWarning READ cloudWarning NOTIFY changed)
    Q_PROPERTY(bool cloudTeaser READ cloudTeaser NOTIFY changed)           // „hamarosan” sor

    // ---- M04 ----
    Q_PROPERTY(QString jobTitle READ jobTitle NOTIFY jobChanged)
    Q_PROPERTY(QString etaText READ etaText NOTIFY jobChanged)
    // [{ id, label, state, percent, detail }] — state: waiting | running | done | failed | skipped
    Q_PROPERTY(QVariantList stages READ stages NOTIFY jobChanged)
    Q_PROPERTY(bool cancellable READ cancellable NOTIFY jobChanged)
    Q_PROPERTY(bool cancelling READ cancelling NOTIFY jobChanged)
    Q_PROPERTY(QString footerLine READ footerLine NOTIFY changed)

    // ---- M05 ----
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(QString errorDetail READ errorDetail NOTIFY changed)
    Q_PROPERTY(QString fixActionLabel READ fixActionLabel NOTIFY changed)
    Q_PROPERTY(QString fixActionPage READ fixActionPage NOTIFY changed)

public:
    explicit PreTranscriptViewModel(QObject* parent = nullptr);

    QObject* controllerObject() const;
    void setController(QObject* controller);
    QString meetingId() const { return m_meetingId; }
    void setMeetingId(const QString& id);
    QString demoState() const { return m_demoState; }
    void setDemoState(const QString& state);
    bool demo() const;

    QString state() const { return m_state; }

    MeetingNoteModel* note() const { return m_note; }
    QString contextNote() const { return m_note->note(); }
    void setContextNote(const QString& note);
    // A mező gépelés közbeni tartalma: a megbeszéléssel EGYÜTT jegyezzük meg, amelyhez írták.
    // A késleltetett mentés (és a megbeszélés-váltás) ezt írja ki — mindig a saját
    // megbeszélésébe, akkor is, ha közben másik lett a kijelölt (pl. véget ért egy felvétel).
    // (A MeetingNoteModel draft / commitDraft műveletei.)
    Q_INVOKABLE void draftContextNote(const QString& note) { m_note->draft(note); }
    Q_INVOKABLE void commitContextDraft() { m_note->commitDraft(); }
    bool identifyEnabled() const { return m_identifyEnabled; }
    void setIdentifyEnabled(bool enabled);
    bool identifyAvailable() const { return m_identifyAvailable; }
    QString mixdownState() const { return m_mixdownState; }
    int mixdownPercent() const { return m_mixdownPercent; }
    bool mixdownCancellable() const { return m_mixdownCancellable; }
    bool canStart() const { return m_canStart; }
    QVariantMap blocker() const { return m_blocker; }
    QString providerLabel() const { return m_providerLabel; }

    bool cloudSelected() const { return m_cloudSelected; }
    QString cloudTier() const { return m_cloudTier; }
    void setCloudTier(const QString& tier);
    QString cloudExpertName() const { return m_cloudExpertName; }
    QString cloudLanguageLabel() const { return m_cloudLanguageLabel; }
    QString cloudWarning() const { return m_cloudWarning; }
    bool cloudTeaser() const { return m_cloudTeaser; }

    QString jobTitle() const { return m_jobTitle; }
    QString etaText() const;
    QVariantList stages() const { return m_stages; }
    bool cancellable() const { return m_cancellable; }
    bool cancelling() const { return m_cancelling; }
    QString footerLine() const;

    QString errorMessage() const { return m_errorMessage; }
    QString errorDetail() const { return m_errorDetail; }
    QString fixActionLabel() const { return m_fixActionLabel; }
    QString fixActionPage() const { return m_fixActionPage; }

    // A kapuzás és az állapot újraolvasása (pl. a Beállítások bezárása után).
    Q_INVOKABLE void refresh();
    // Lekeverés indítása most (nem kell megvárni az átírást).
    Q_INVOKABLE void startMixdown();
    // A megmaradt hiba elengedése → vissza a lépésekhez (kontextus / előkészítés módosítása).
    Q_INVOKABLE void clearError();

signals:
    void controllerChanged();
    void meetingIdChanged();
    void demoStateChanged();
    void contextNoteChanged();
    void mixdownPercentChanged();
    void jobChanged();
    void changed();

private:
    tanara::AppController* app() const;
    void connectController();
    void reload();
    void loadDemo();
    void applyJob(const tanara::JobProgress& job);

    QPointer<QObject> m_injected;
    QPointer<tanara::AppController> m_connected;
    QList<QMetaObject::Connection> m_connections;

    QString m_meetingId;
    QString m_demoState;
    QString m_state = QStringLiteral("none");

    MeetingNoteModel* m_note = nullptr;
    bool m_identifyEnabled = true;
    bool m_identifyAvailable = true;
    QString m_mixdownState = QStringLiteral("ready");
    int m_mixdownPercent = -1;
    bool m_mixdownCancellable = false;
    bool m_canStart = false;
    QVariantMap m_blocker;
    QString m_providerLabel;

    bool m_cloudSelected = false;
    QString m_cloudTier = QStringLiteral("accurate");
    QString m_cloudExpertName;
    QString m_cloudLanguageLabel;
    QString m_cloudWarning;
    bool m_cloudTeaser = false;

    QString m_jobTitle;
    QVariantList m_stages;
    bool m_cancellable = true;
    bool m_cancelling = false;
    tanara::JobProgress m_job;      // az ETA-hoz (startedAt + becslés)
    QString m_demoEta;
    QTimer m_etaTimer;

    QString m_errorMessage;
    QString m_errorDetail;
    QString m_fixActionLabel;
    QString m_fixActionPage;
};

} // namespace tanara_qml
