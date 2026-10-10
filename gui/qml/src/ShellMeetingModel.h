#pragma once
//
// ShellMeetingModel — a kijelölt megbeszélés héj-szintű állapota: fejléc (cím, meta-sor),
// van-e átirata (→ fülek vagy az átirat előtti nézet), elavult-e az összefoglalója (→ pirula
// a fülön), és a rajta futó megszakítható feladatok a feladat-sávhoz (MeetingJobTracker).
// Controller nélkül (App.demo) a kitalált mintakönyvtárból dolgozik.
//
// A fejléc címkesora (TagRow) a `tags` modellt kapja: a kijelölt megbeszélés címkéi és
// javaslatai. Javaslatot kér, amikor egy átírt megbeszélés megjelenik és amikor az átirata
// (újra) elkészül; az együtt járókat a címke felrakása után a MeetingTagsModel kéri, az LLM-
// javaslat az összefoglaló után magától jön. Demóban a kitalált címke-készlet (demoState a QML-ből).
//
#include "MeetingTagsModel.h"

#include "tanara/jobs/JobTypes.h"

#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace tanara {
class AppController;
}

namespace tanara_qml {

class ShellMeetingModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString meetingId READ meetingId WRITE setMeetingId NOTIFY meetingIdChanged)
    Q_PROPERTY(bool exists READ exists NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    // „2026. okt. 1. · 1:16:04 · 6 beszélő” (átirat nélkül a sávok száma).
    Q_PROPERTY(QString metaText READ metaText NOTIFY changed)
    Q_PROPERTY(bool hasTranscript READ hasTranscript NOTIFY changed)
    Q_PROPERTY(bool summaryStale READ summaryStale NOTIFY changed)
    // Van-e (gyors vagy témánkénti) összefoglalója — a Vezetői összefoglaló / Memó fül elérhető-e.
    Q_PROPERTY(bool hasSummary READ hasSummary NOTIFY changed)
    // Futó átírás / összefoglaló a fülek pirulájához: -1 = nem fut; különben 0…100
    // (határozatlan haladásnál 0).
    Q_PROPERTY(int transcribePercent READ transcribePercent NOTIFY tasksChanged)
    Q_PROPERTY(int summarizePercent READ summarizePercent NOTIFY tasksChanged)
    // Az összefoglaló szolgáltatója („LM Studio · saját kulcs”) — a fül tooltip-panelje mondja.
    Q_PROPERTY(QString summaryProviderLabel READ summaryProviderLabel NOTIFY changed)
    // Van-e legalább egy aktív hangsáv (az azonosítás előfeltétele).
    Q_PROPERTY(bool canIdentify READ canIdentify NOTIFY changed)
    Q_PROPERTY(bool identifyRunning READ identifyRunning NOTIFY tasksChanged)
    // A feladat-sáv sorai: [{ kind, title, detail, eta, iconName, percent (0…100 | -1),
    //                        cancellable, cancelling }]
    Q_PROPERTY(QVariantList tasks READ tasks NOTIFY tasksChanged)
    // A fejléc címkesorának modellje (a kijelölt megbeszéléshez kötve).
    Q_PROPERTY(tanara_qml::MeetingTagsModel* tags READ tags CONSTANT)
    // Csak demó / képernyőkép: egy minta-feladat a sávban.
    Q_PROPERTY(bool demoTask READ demoTask WRITE setDemoTask NOTIFY tasksChanged)

public:
    explicit ShellMeetingModel(QObject* parent = nullptr);

    void setController(tanara::AppController* controller);

    QString meetingId() const { return m_meetingId; }
    void setMeetingId(const QString& id);
    bool exists() const { return m_exists; }
    QString title() const { return m_title; }
    QString metaText() const { return m_meta; }
    bool hasTranscript() const { return m_hasTranscript; }
    bool summaryStale() const { return m_summaryStale; }
    bool hasSummary() const { return m_hasSummary; }
    int transcribePercent() const { return m_transcribePercent; }
    int summarizePercent() const { return m_summarizePercent; }
    QString summaryProviderLabel() const { return m_summaryProvider; }
    bool canIdentify() const { return m_canIdentify; }
    bool identifyRunning() const { return m_identifyRunning; }
    QVariantList tasks() const { return m_tasks; }
    bool demoTask() const { return m_demoTask; }
    void setDemoTask(bool on);
    MeetingTagsModel* tags() const { return m_tags; }

    // Egy futó feladat sávbeli leírása (statikus: tesztelhető).
    static QVariantMap describeJob(const tanara::JobProgress& job);

signals:
    void meetingIdChanged();
    void changed();
    void tasksChanged();

private:
    void attach();
    void reload();
    void reloadTasks();
    void onMeetingTouched(const QString& id);
    void syncTags();

    QPointer<tanara::AppController> m_controller;
    QPointer<tanara::AppController> m_attached;
    bool m_controllerInjected = false;

    QString m_meetingId;
    bool m_exists = false;
    QString m_title;
    QString m_meta;
    bool m_hasTranscript = false;
    bool m_summaryStale = false;
    bool m_hasSummary = false;
    int m_transcribePercent = -1;
    int m_summarizePercent = -1;
    QString m_summaryProvider;
    bool m_canIdentify = false;
    bool m_identifyRunning = false;
    bool m_demoTask = false;
    QVariantList m_tasks;

    MeetingTagsModel* m_tags = nullptr;
    QString m_tagsRequestedFor;   // erre a megbeszélésre már kértünk javaslatot (kijelölésenként egyszer)
};

} // namespace tanara_qml
