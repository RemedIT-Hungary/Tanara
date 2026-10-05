#pragma once
//
// RecorderViewModel — a lebegő felvevő (RecorderView.qml / RecorderWindow.qml) nézetmodellje.
//
// Egyetlen objektum adja a felvevő teljes állapotát: cím (automatikus név + átnevezés),
// eszköz-lista (három csoport, kapcsolók, barátságos + nyers név, élő szint csúcstartással,
// jel-tippek), felvétel-állapot és eltelt idő, a „Vége a megbeszélésnek?” kérdés, a kész
// állapot. A core-t a tanara::AppController-en át éri el.
//
// Controller nélkül (--qml-shot / --demo / --gallery, tesztek) KITALÁLT eszközökkel és
// szintekkel dolgozik; a `demoState` ("R01" … "R10") a design-állapotokat állítja be.
//
#include "tanara/Types.h"

#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace tanara {
class AppController;
class PlaybackRouteMonitor;
}

namespace tanara_qml {

class RecorderViewModel;

// Az eszköz-sorok modellje (a nézetmodell `devices` property-je).
class RecorderDeviceModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS
public:
    enum Role {
        KeyRole = Qt::UserRole + 1,   // a nyers OS-eszköznév (stabil kulcs)
        NameRole,                     // barátságos név
        RawNameRole,                  // nyers OS-név (második sor, buborék)
        GroupRole,                    // 0 mikrofon · 1 hangkimenet · 2 egyéb bemenet
        GroupFirstRole,               // a csoport első sora (csoportfej kell fölé)
        IconRole,                     // Lucide ikonnév az összecsukott sorhoz
        SelectedRole,                 // sávra kerül / kerülne
        LockedRole,                   // épp rögzített sáv (felvétel közben nem kapcsolható ki)
        DefaultRole,                  // a rendszer alapértelmezett eszköze
        AppRole,                      // a kimenetre épp játszó alkalmazás ("" ha nincs)
        LevelRole,                    // 0..14 szegmens
        PeakRole,                     // csúcstartás szegmens-indexe (-1 = nincs)
        StatusRole,                   // "" | "noSignal" | "signalUnrecorded" | "silentWarn" | "disconnected"
        StatusTextRole,               // a státusz felirata
        ToggleableRole,               // most átkapcsolható-e
    };
    explicit RecorderDeviceModel(RecorderViewModel* vm);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    friend class RecorderViewModel;
    RecorderViewModel* m_vm;
};

class RecorderViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    // "idle" | "recording" | "stopping" | "done" | "noDevice"
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY titleChanged)
    // Igaz, amíg a cím az automatikus név (halványan jelenik meg, alatta a tipp).
    Q_PROPERTY(bool titleAutomatic READ titleAutomatic NOTIFY titleChanged)
    Q_PROPERTY(qint64 elapsedMs READ elapsedMs NOTIFY elapsedChanged)
    Q_PROPERTY(QString elapsedText READ elapsedText NOTIFY elapsedChanged)
    Q_PROPERTY(QAbstractItemModel* devices READ devices CONSTANT)
    Q_PROPERTY(int deviceCount READ deviceCount NOTIFY countsChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY countsChanged)
    Q_PROPERTY(int trackCount READ trackCount NOTIFY countsChanged)   // a futó felvétel sávjai
    Q_PROPERTY(bool canStart READ canStart NOTIFY countsChanged)
    // „Vége a megbeszélésnek?” (R06) — a callEnded / silenceDetected jelből.
    Q_PROPERTY(bool askVisible READ askVisible NOTIFY askChanged)
    Q_PROPERTY(QString askText READ askText NOTIFY askChanged)
    // Kész állapot (R09).
    Q_PROPERTY(QString doneSummary READ doneSummary NOTIFY doneChanged)   // "30:34 · 3 sáv"
    Q_PROPERTY(QString doneMeetingId READ doneMeetingId NOTIFY doneChanged)
    Q_PROPERTY(QString doneProblem READ doneProblem NOTIFY doneChanged)   // nem üres → NEM „Elmentve”
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorChanged)
    // A sávonkénti csend-figyelmeztetés küszöbe (perc).
    Q_PROPERTY(int silenceWarnMinutes READ silenceWarnMinutes CONSTANT)
    // A felvétel címkéi (C06): [{ id, name }]. Indítás előtt és felvétel közben szerkeszthető;
    // a meeting csak a felvétel végén jön létre, ezért a core a végén rakja rá őket
    // (AppController::setRecordingTags).
    Q_PROPERTY(QVariantList tags READ tags NOTIFY tagsChanged)
    Q_PROPERTY(QStringList tagIds READ tagIds NOTIFY tagsChanged)
    Q_PROPERTY(bool tagsEditable READ tagsEditable NOTIFY stateChanged)
    // Design-állapot ("R01" … "R10"); csak controller nélkül hat. "" = nincs demó.
    Q_PROPERTY(QString demoState READ demoState WRITE setDemoState NOTIFY demoStateChanged)
    // A core; alapból az App.controller. Tesztben / beágyazáskor felülírható.
    Q_PROPERTY(QObject* controller READ controllerObject WRITE setControllerObject NOTIFY controllerChanged)

public:
    explicit RecorderViewModel(QObject* parent = nullptr);
    ~RecorderViewModel() override;

    QString state() const { return m_state; }
    QString title() const { return m_title; }
    void setTitle(const QString& title);
    bool titleAutomatic() const { return m_titleAuto; }
    qint64 elapsedMs() const { return m_elapsedMs; }
    QString elapsedText() const;
    QAbstractItemModel* devices() { return &m_model; }
    int deviceCount() const { return m_rows.size(); }
    int selectedCount() const;
    int trackCount() const;
    bool canStart() const;
    bool askVisible() const { return m_askVisible; }
    QString askText() const { return m_askText; }
    QString doneSummary() const { return m_doneSummary; }
    QString doneMeetingId() const { return m_doneMeetingId; }
    QString doneProblem() const { return m_doneProblem; }
    QString errorText() const { return m_errorText; }
    int silenceWarnMinutes() const { return 3; }
    QString demoState() const { return m_demoState; }
    void setDemoState(const QString& state);

    QObject* controllerObject() const;
    void setControllerObject(QObject* controller);
    tanara::AppController* controller() const;
    void setController(tanara::AppController* controller);

    // Az automatikus név: „<App>-hívás · okt. 3. 14:02”, app nélkül „Megbeszélés · …”.
    static QString automaticTitle(const QString& appName, const QDateTime& when);
    // RMS (0..1) → 0..14 szegmens (dB-skála: −60 dB = 0, 0 dB = 14).
    static int levelSegments(float rms);

    // Külső kérés (parancssor / figyelő / főablak): cím vagy app-név, eszköz-indexek
    // (a capture-lista sorszámai; üres = a mentett kijelölés), kontextus-megjegyzés.
    Q_INVOKABLE void applyRequest(const QString& title, const QString& appName,
                                  const QString& context, const QList<int>& deviceIndexes = {});

    QVariantList tags() const;
    QStringList tagIds() const;
    bool tagsEditable() const;
    // Címke a felvételre név szerint (szükség szerint létrehozza a készletben). false: nem
    // szerkeszthető most, üres a név, vagy már rajta van.
    Q_INVOKABLE bool addTag(const QString& name);
    Q_INVOKABLE void removeTag(const QString& id);
    // Ctrl+T: a címke-mező megnyitása (a nézet a tagInputRequested jelre nyitja).
    Q_INVOKABLE bool openTagInput();
    // Csak a címkék bekötése a core-ra (eszköz-felsorolás, szintfigyelés NÉLKÜL) — tesztekhez,
    // ahol hangeszközhöz nyúlni nem szabad. A teljes bekötés a setController.
    void attachTagsOnly(tanara::AppController* controller);

public slots:
    void start();              // Ctrl+R
    void stop();               // Ctrl+. — megerősítés nélkül (a gomb explicit)
    void toggleDevice(int row);
    void rescan();             // R10 „Újrakeresés”
    void newRecording();       // R09 „Új felvétel” (ugyanazokkal a forrásokkal)
    void openInAnalyzer();     // R09 „Megnyitás az elemzőben”
    void openSettings();       // R10 „Rögzítés beállításai”
    void continueRecording();  // R06 „Folytatom”
    void clearError();

signals:
    void stateChanged();
    void titleChanged();
    void elapsedChanged();
    void countsChanged();
    void askChanged();
    void doneChanged();
    void errorChanged();
    void demoStateChanged();
    void controllerChanged();
    void tagsChanged();
    void tagInputRequested();
    // A felvevő-ablaknak / a befoglaló folyamatnak:
    void askRaised(QString title, QString text);        // jelenjen meg + rendszerértesítés
    void openAnalyzerRequested(QString meetingId);
    void settingsRequested();
    void recordingStarted(QString meetingFolder);
    void recordingFinished(QString meetingId);

private:
    friend class RecorderDeviceModel;
    struct Row {
        tanara::AudioDeviceInfo info;
        QString friendly;
        int group = 0;
        bool selected = false;
        bool recorded = false;       // a futó felvétel élő sávja
        bool disconnected = false;   // rögzített volt, de leválasztották
        QString app;
        float rms = 0.f, peak = 0.f; // legutóbbi nyers értékek
        int level = 0;
        int peakSeg = -1;
        qint64 peakAt = 0;           // mikor állt be a csúcstartás (ms, monoton)
        qint64 lastSignalAt = 0;     // utoljára mikor volt jel (ms, monoton); 0 = még soha
        qint64 silentSince = 0;      // a rögzített sáv csendjének kezdete (ms, monoton)
        QString status, statusText;
        bool demoFixed = false;      // demó: a státuszt/szintet nem számoljuk újra
    };

    void attach();                       // jelek a controllerre
    void rebuildDevices();               // eszköz-újrafelsorolás → sorok (kijelölés megőrizve)
    void updateRoutes();
    void onLevel(const QString& device, float rms, float peak);
    void tick();                         // 30 Hz: csúcstartás, státuszok, modell-frissítés
    void refreshRow(int row, bool levelOnly);
    void computeStatus(Row& r, qint64 now) const;
    void setState(const QString& state);
    void onRecordingState(tanara::RecordingState st);
    void onFinished(const tanara::Meeting& m);
    void persistSelection();
    void raiseAsk(const QString& text);
    void loadDemo();
    QString iconFor(const Row& r) const;
    void syncRecordingTags();            // a címkék átadása a core-nak (felvétel közben él)
    void refreshTagNames();              // átnevezett / törölt címke a készletben
    int rowOf(const QString& deviceName) const;
    QVector<tanara::AudioDeviceInfo> selectedDevices() const;

    RecorderDeviceModel m_model{this};
    QVector<Row> m_rows;
    QPointer<tanara::AppController> m_controller;
    QPointer<tanara::AppController> m_tagCtl;    // a címkék core-ja (= m_controller, vagy attachTagsOnly)
    QMetaObject::Connection m_tagConn;
    bool m_controllerSet = false;
    tanara::PlaybackRouteMonitor* m_routes = nullptr;
    QTimer m_tick;
    QTimer m_fallbackScan;               // ahol nincs útvonal-figyelő: időzített újrafelsorolás
    QElapsedTimer m_clock;

    QString m_state = QStringLiteral("idle");
    QString m_title;
    bool m_titleAuto = true;
    QString m_appName;                   // az észlelt hívás-app (a figyelőtől): automatikus név + a meeting detectedCallApp mezője
    QString m_context;                   // kifejezett --context megjegyzés (a figyelő már nem küld ilyet)
    qint64 m_elapsedMs = 0;
    bool m_askVisible = false;
    QString m_askText;
    QString m_doneSummary, m_doneMeetingId, m_doneProblem;
    QString m_errorText;
    QString m_demoState;
    struct TagRef { QString id, name; };
    QVector<TagRef> m_tags;
    QStringList m_knownDevices;          // amit már láttunk (az „új eszköz” felismeréséhez)
    bool m_selectionLoaded = false;
};

} // namespace tanara_qml
