#pragma once
//
// PeopleViewModel — a Személyek ablak (design/handoff-people, P01–P06) nézetmodellje.
//
// Bal oldal: `people` (PeopleListModel) — keresés névben ÉS becenévben (ékezet- és
// kisbetű-független, a névbeli találat kiemelve), rendezés (ABC / legutóbb / legtöbb
// megbeszélés), szakaszok. Jobb oldal: a kijelölt személy (`selectedName`) adatai:
// becenevek, megjegyzés, hangminták, megbeszélések.
//
// A lista AZONNAL megjelenik (nevek + mintaszám a tárolókból); a megbeszélés-szám, a
// beszédidő és az „utoljára” a háttérben számolódik (tanara::PeopleStats), és utólag töltődik
// be (`statsReady`). Minden művelet a tanara::PeopleService-en megy át; a visszavonható
// lépések után `toast(szöveg, visszavonható)` jelet ad (a QML ~8 mp-ig mutatja).
//
// Controller nélkül (demó, képernyőkép) kitalált személyeket szolgál ki (`demoState`:
// P01 … P06 és a segéd-állapotok — lásd PeopleWindow.qml); ilyenkor a műveletek nem tesznek
// semmit.
//
#include "PeopleListModel.h"

#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace tanara {
class AppController;
class PeopleService;
class PeopleStats;
}

namespace tanara_qml {

class PlayerBackend;

class PeopleViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QObject* controller READ controllerObject WRITE setControllerObject NOTIFY controllerChanged)
    Q_PROPERTY(QString demoState READ demoState WRITE setDemoState NOTIFY demoStateChanged)

    // ---- lista ----
    Q_PROPERTY(tanara_qml::PeopleListModel* people READ people CONSTANT)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(bool searching READ searching NOTIFY queryChanged)
    // "abc" | "recent" | "meetings"
    Q_PROPERTY(QString sort READ sort WRITE setSort NOTIFY sortChanged)
    Q_PROPERTY(QString sortLabel READ sortLabel NOTIFY sortChanged)
    Q_PROPERTY(QString countText READ countText NOTIFY listChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY listChanged)
    // Csak a saját személy létezik (P06: üres állapot).
    Q_PROPERTY(bool onlySelf READ onlySelf NOTIFY listChanged)
    // A megbeszélés-számok / beszédidők megvannak (előtte a sorok csak a mintaszámot mutatják).
    Q_PROPERTY(bool statsReady READ statsReady NOTIFY listChanged)

    // ---- a kijelölt személy ----
    Q_PROPERTY(QString selectedName READ selectedName WRITE setSelectedName NOTIFY selectionChanged)
    Q_PROPERTY(int selectedRow READ selectedRow NOTIFY listChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    Q_PROPERTY(bool selectedIsSelf READ selectedIsSelf NOTIFY detailChanged)
    Q_PROPERTY(QString selectedMonogram READ selectedMonogram NOTIFY detailChanged)
    Q_PROPERTY(QString detailMeta READ detailMeta NOTIFY detailChanged)
    Q_PROPERTY(QStringList aliases READ aliases NOTIFY detailChanged)
    Q_PROPERTY(QString note READ note NOTIFY detailChanged)
    // [{ id, kind, icon, label, meetingTitle, date, length, playable }], legújabb elöl
    Q_PROPERTY(QVariantList samples READ samples NOTIFY detailChanged)
    Q_PROPERTY(int sampleCount READ sampleCount NOTIFY detailChanged)
    Q_PROPERTY(QString samplesSubText READ samplesSubText NOTIFY detailChanged)
    // [{ title, date, talk }], legújabb elöl
    Q_PROPERTY(QVariantList meetings READ meetings NOTIFY detailChanged)
    Q_PROPERTY(int meetingCount READ meetingCount NOTIFY detailChanged)

    // ---- hanglenyomat nélküli személy (P03) ----
    // A terv elkészült-e (a megbeszélések átnézése rövid ideig tart).
    Q_PROPERTY(bool planReady READ planReady NOTIFY planChanged)
    Q_PROPERTY(bool planPossible READ planPossible NOTIFY planChanged)
    Q_PROPERTY(QString planText READ planText NOTIFY planChanged)
    Q_PROPERTY(QString planButtonText READ planButtonText NOTIFY planChanged)
    Q_PROPERTY(QString planNote READ planNote NOTIFY planChanged)
    Q_PROPERTY(bool creatingVoiceprint READ creatingVoiceprint NOTIFY planChanged)
    Q_PROPERTY(QString creatingText READ creatingText NOTIFY planChanged)

    // ---- lejátszás, visszavonás ----
    Q_PROPERTY(QString playingSampleId READ playingSampleId NOTIFY playingChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged)

public:
    explicit PeopleViewModel(QObject* parent = nullptr);
    ~PeopleViewModel() override;

    QObject* controllerObject() const;
    void setControllerObject(QObject* controller);
    void setController(tanara::AppController* controller);
    QString demoState() const { return m_demoState; }
    void setDemoState(const QString& state);

    PeopleListModel* people() { return &m_list; }
    QString query() const { return m_query; }
    void setQuery(const QString& query);
    bool searching() const { return !m_query.trimmed().isEmpty(); }
    QString sort() const { return m_sort; }
    void setSort(const QString& sort);
    QString sortLabel() const;
    QString countText() const;
    int totalCount() const { return int(m_persons.size()); }
    bool onlySelf() const;
    bool statsReady() const { return m_statsReady; }

    QString selectedName() const { return m_selected; }
    void setSelectedName(const QString& name);
    int selectedRow() const { return m_list.indexOfName(m_selected); }
    bool hasSelection() const { return !m_selected.isEmpty(); }
    bool selectedIsSelf() const { return m_detail.isSelf; }
    QString selectedMonogram() const;
    QString detailMeta() const { return m_detailMeta; }
    QStringList aliases() const { return m_detail.aliases; }
    QString note() const { return m_detail.note; }
    QVariantList samples() const { return m_samples; }
    int sampleCount() const { return int(m_samples.size()); }
    QString samplesSubText() const { return m_samplesSub; }
    QVariantList meetings() const { return m_meetings; }
    int meetingCount() const { return int(m_meetings.size()); }

    bool planReady() const { return m_planReady; }
    bool planPossible() const { return m_planPossible; }
    QString planText() const { return m_planText; }
    QString planButtonText() const { return m_planButton; }
    QString planNote() const { return m_planNote; }
    bool creatingVoiceprint() const { return m_creating; }
    QString creatingText() const { return m_creatingText; }

    QString playingSampleId() const { return m_playingId; }
    bool canUndo() const;

    // ---- műveletek (a visszatérő szöveg hibaüzenet; üres = sikerült) ----
    // Az ablak megnyitásakor: friss állapot a lemezről + a statisztika frissítése a háttérben.
    Q_INVOKABLE void refresh();
    // Kijelölés név szerint (mély hivatkozás): a keresőt is törli, ha a személy nem látszana.
    Q_INVOKABLE bool selectPerson(const QString& name);
    Q_INVOKABLE bool personExists(const QString& name) const;
    Q_INVOKABLE QString addPerson(const QString& name);
    Q_INVOKABLE QString rename(const QString& newName);
    Q_INVOKABLE QString addAlias(const QString& alias);
    Q_INVOKABLE void removeAlias(const QString& alias);
    Q_INVOKABLE void setNote(const QString& note);

    Q_INVOKABLE void toggleSample(const QString& sampleId);
    Q_INVOKABLE void stopSample();
    Q_INVOKABLE void removeSample(const QString& sampleId);
    // Áthelyezés létező személyhez, vagy — ha ilyen név nincs — új személy a mintából.
    Q_INVOKABLE QString moveSample(const QString& sampleId, const QString& toName);
    // A minta-áthelyező párbeszéd listája: [{ name, monogram, meta }] (a kijelölt nélkül).
    Q_INVOKABLE QVariantList personChoices(const QString& query) const;

    Q_INVOKABLE void createVoiceprint();

    // Összevonás: jelöltek hang-hasonlóság szerint: [{ name, monogram, meta, similarity }].
    Q_INVOKABLE QVariantList mergeCandidates(const QString& query) const;
    // Az eredmény-sor (rich text) a kiválasztott jelölttel; keepSelected: a kijelölt neve marad.
    Q_INVOKABLE QString mergeResultText(const QString& other, bool keepSelected) const;
    Q_INVOKABLE QString merge(const QString& other, bool keepSelected);

    // A törlés következményei szövegesen (a jelölőnégyzet állása szerint).
    Q_INVOKABLE QString deleteText(bool keepSamples) const;
    Q_INVOKABLE bool deleteHasSamples() const;
    Q_INVOKABLE QString deletePerson(bool keepSamples);

    Q_INVOKABLE void undo();

signals:
    void controllerChanged();
    void demoStateChanged();
    void queryChanged();
    void sortChanged();
    void listChanged();
    void selectionChanged();
    void detailChanged();
    void planChanged();
    void playingChanged();
    void undoChanged();
    // Értesítés az ablak alján. undoable: „Visszavonás” gombbal.
    void toast(const QString& text, bool undoable);

private:
    struct Person {
        QString name;
        bool isSelf = false;
        QStringList aliases;
        QString note;
        int sampleCount = 0;
        int meetingCount = 0;
        qint64 talkMs = 0;
        QDateTime lastSeen;
    };
    struct DemoSample { QString kind, label, meeting, date, length; };
    struct DemoMeeting { QString title, date, talk; };

    void attach();
    void scheduleRebuild();
    void rebuild();
    void rebuildList();
    void refreshDetail();
    void refreshPlan();
    void runNextVoiceprint();
    const Person* find(const QString& name) const;
    QString rowMeta(const Person& p, const QString& aliasHit) const;
    QString personMeta(const Person& p) const;
    void loadDemo();
    void ensureBackend();
    void finishPlayback();

    QPointer<tanara::AppController> m_controller;
    QPointer<tanara::AppController> m_attached;
    bool m_controllerInjected = false;
    tanara::PeopleService* m_service = nullptr;
    tanara::PeopleStats* m_stats = nullptr;
    QList<QMetaObject::Connection> m_connections;

    QString m_demoState;
    bool m_demo = false;
    QHash<QString, QVector<DemoSample>> m_demoSamples;
    QHash<QString, QVector<DemoMeeting>> m_demoMeetings;

    PeopleListModel m_list;
    QVector<Person> m_persons;
    QString m_query;
    QString m_sort = QStringLiteral("abc");
    bool m_statsReady = false;
    bool m_rebuildScheduled = false;

    QString m_selected;
    Person m_detail;
    QString m_detailMeta;
    QVariantList m_samples;
    QString m_samplesSub;
    QVariantList m_meetings;

    bool m_planReady = false;
    bool m_planPossible = false;
    QString m_planText, m_planButton, m_planNote;
    QString m_planFor;              // kinek a terve
    QStringList m_planMeetingIds;
    int m_planGeneration = 0;
    bool m_creating = false;
    QString m_creatingText;
    QString m_creatingFor;
    QStringList m_createQueue;
    int m_createTotal = 0, m_createOk = 0, m_createFailed = 0;
    QString m_createError;

    PlayerBackend* m_backend = nullptr;
    QTimer m_playTick;
    QString m_playingId;
    qint64 m_playEndMs = 0;
};

} // namespace tanara_qml
