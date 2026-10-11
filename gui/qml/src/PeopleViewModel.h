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
// Címkék a személyen (vault UX-terv 2., 3.2, 7.): a kijelölt személy kézi címkéi
// (`personTags`), a „Ezeken szokott ott lenni” javaslatok (`tagSuggestions`, mind; a nézet
// hármat mutat + „+N”), a TagBackend varraton át (TagService). A saját személynél és a
// „Névtelen N” mintatartónál nincs címke-szakasz (`tagsMode`). A keresés címkére is talál
// (a találat-sor metája: „címke: #Nordvik”). A címke-műveletek a TagService vermében
// vonhatók vissza; a Ctrl+Z a két verem közül a frissebbet vonja vissza.
//
// Minták modellenként: egy minta (testvér-csoport) egy sor; a sor `modelText`-je („2/2
// modell”) és `modelTip`-je mutatja, melyik használt hangmodellel van lenyomata. Amíg a
// lenyomat-pótlás fut (`backfillRunning`), a fejléc ezt jelzi.
//
// Controller nélkül (demó, képernyőkép) kitalált személyeket szolgál ki (`demoState`:
// P01 … P06 és a segéd-állapotok — lásd PeopleWindow.qml); ilyenkor a személy-műveletek nem
// tesznek semmit, a címke-műveletek a kitalált címkekészleten futnak.
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
class TagBackend;

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

    // ---- címkék a személyen ----
    // "edit" (szerkeszthető) | "self" (saját személy: muted magyarázó sor) | "none" (névtelen)
    Q_PROPERTY(QString tagsMode READ tagsMode NOTIFY personTagsChanged)
    // [{ id, name }] a felrakás sorrendjében
    Q_PROPERTY(QVariantList personTags READ personTags NOTIFY personTagsChanged)
    // [{ id, name, countText, toolTip }] — mind; a nézet legfeljebb hármat mutat + „+N”
    Q_PROPERTY(QVariantList tagSuggestions READ tagSuggestions NOTIFY personTagsChanged)
    // A javaslat-sor helyén álló muted szöveg („Megbeszélések számolása…” / „A javaslathoz
    // legalább két megbeszélés kell.”); üres: javaslatok vagy semmi.
    Q_PROPERTY(QString tagHintText READ tagHintText NOTIFY personTagsChanged)

    // ---- tanult sáv-oldal (brief E02 3.) ----
    // "local" | "remote" | "" (nincs adat) — a „Jellemzően: …” szegmens értéke.
    Q_PROPERTY(QString defaultSide READ defaultSide NOTIFY detailChanged)
    // A forrás a szegmens mellett: „tanult” | „kézzel beállítva” | "" (nincs adat).
    Q_PROPERTY(QString defaultSideNote READ defaultSideNote NOTIFY detailChanged)
    // Látszik-e a sor (nem a saját személy — az ő hangja a mikrofon — és nem a mintatartó).
    Q_PROPERTY(bool defaultSideVisible READ defaultSideVisible NOTIFY detailChanged)

    // ---- hangmodellek ----
    Q_PROPERTY(bool backfillRunning READ backfillRunning NOTIFY backfillChanged)

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

    // ---- címkék a kijelölt személyen (mind egy-egy visszavonható lépés, toasttal) ----
    QString tagsMode() const { return m_tagsMode; }
    QVariantList personTags() const { return m_personTags; }
    QVariantList tagSuggestions() const { return m_tagSuggestions; }
    QString tagHintText() const { return m_tagHint; }
    // A mezőből: név (új címke is) vagy id.
    Q_INVOKABLE void addPersonTag(const QString& nameOrId);
    Q_INVOKABLE void removePersonTag(const QString& tagId);
    Q_INVOKABLE void acceptTagSuggestion(const QString& tagId);
    Q_INVOKABLE void acceptAllTagSuggestions();
    Q_INVOKABLE void rejectTagSuggestion(const QString& tagId);

    QString defaultSide() const { return m_detail.defaultSide; }
    QString defaultSideNote() const;
    bool defaultSideVisible() const;
    // A sáv-oldal kézi átírása ("local" | "remote" | "" = nincs adat / felejtés).
    Q_INVOKABLE void setDefaultSide(const QString& side);
    // Mély hivatkozás egy szakaszra ("tags": a Címkék szakasz) — a nézet odagörget
    // (sectionRequested). A „Címkéi” javító link így nyitja az ablakot (C4).
    Q_INVOKABLE void requestSection(const QString& section);

    bool backfillRunning() const { return m_backfillRunning; }
    // Tesztekhez: a címke-adatforrás (nem veszi át); nullptr → a sajátja.
    void setTagBackend(TagBackend* backend);

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
    void personTagsChanged();
    void backfillChanged();
    // Értesítés az ablak alján. undoable: „Visszavonás” gombbal.
    void toast(const QString& text, bool undoable);
    // A nézet görgessen a szakaszhoz ("tags").
    void sectionRequested(const QString& section);

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
        QString defaultSide;            // "local" | "remote" | ""
        bool defaultSideManual = false;
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
    void attachTags(TagBackend* backend);
    void refreshTags();
    void refreshBackfill();
    QString tagNameOf(const QString& id) const;
    void tagStep(const QString& label);
    bool isPlaceholderName(const QString& name) const;
    void decorateSamples();

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

    TagBackend* m_tagBackend = nullptr;
    TagBackend* m_ownTagBackend = nullptr;
    QList<QMetaObject::Connection> m_tagConnections;
    QString m_tagsMode = QStringLiteral("none");
    QVariantList m_personTags;
    QVariantList m_tagSuggestions;
    QString m_tagHint;
    // A visszavonási sorrend a két verem között ("people" | "tags"), legújabb a végén.
    QStringList m_undoOrder;

    bool m_backfillRunning = false;
    QTimer m_backfillPoll;

    PlayerBackend* m_backend = nullptr;
    QTimer m_playTick;
    QString m_playingId;
    qint64 m_playEndMs = 0;
};

} // namespace tanara_qml
