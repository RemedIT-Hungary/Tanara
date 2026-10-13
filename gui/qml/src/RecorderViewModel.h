#pragma once
//
// RecorderViewModel: view model of the floating recorder (RecorderView.qml / RecorderWindow.qml).
//
// One object holds the recorder's whole state: title (automatic name + rename), device list
// (three groups, toggles, friendly + raw name, live level with peak hold, signal hints),
// recording state and elapsed time, the "End of the meeting?" question, the done state.
// It reaches the core through tanara::AppController.
//
// Without a controller (--qml-shot / --demo / --gallery, tests) it works with FICTIONAL
// devices and levels; `demoState` ("R01" ... "R10") sets the design states.
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

// Model of the device rows (the view model's `devices` property).
class RecorderDeviceModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS
public:
    enum Role {
        KeyRole = Qt::UserRole + 1,   // the raw OS device name (stable key)
        NameRole,                     // friendly name
        RawNameRole,                  // raw OS name (second line, bubble)
        GroupRole,                    // 0 microphone, 1 audio output, 2 other input
        GroupFirstRole,               // first row of the group (needs a group header above)
        IconRole,                     // Lucide icon name for the collapsed row
        SelectedRole,                 // goes / would go onto a track
        LockedRole,                   // currently recorded track (cannot be switched off during recording)
        DefaultRole,                  // the system's default device
        AppRole,                      // the application currently playing to the output ("" if none)
        LevelRole,                    // 0..14 segments
        PeakRole,                     // peak-hold segment index (-1 = none)
        StatusRole,                   // "" | "noSignal" | "signalUnrecorded" | "silentWarn" | "disconnected"
        StatusTextRole,               // the status label
        ToggleableRole,               // can be toggled now
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
    // True while the title is the automatic name (shown dimmed, with the hint below).
    Q_PROPERTY(bool titleAutomatic READ titleAutomatic NOTIFY titleChanged)
    Q_PROPERTY(qint64 elapsedMs READ elapsedMs NOTIFY elapsedChanged)
    Q_PROPERTY(QString elapsedText READ elapsedText NOTIFY elapsedChanged)
    Q_PROPERTY(QAbstractItemModel* devices READ devices CONSTANT)
    Q_PROPERTY(int deviceCount READ deviceCount NOTIFY countsChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY countsChanged)
    Q_PROPERTY(int trackCount READ trackCount NOTIFY countsChanged)   // the running recording's tracks
    Q_PROPERTY(bool canStart READ canStart NOTIFY countsChanged)
    // "End of the meeting?": from the callEnded / silenceDetected signal.
    Q_PROPERTY(bool askVisible READ askVisible NOTIFY askChanged)
    Q_PROPERTY(QString askText READ askText NOTIFY askChanged)
    // Done state.
    Q_PROPERTY(QString doneSummary READ doneSummary NOTIFY doneChanged)   // "30:34 · 3 tracks"
    Q_PROPERTY(QString doneMeetingId READ doneMeetingId NOTIFY doneChanged)
    Q_PROPERTY(QString doneProblem READ doneProblem NOTIFY doneChanged)   // non-empty -> NOT "Saved"
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorChanged)
    // Threshold of the per-track silence warning (minutes).
    Q_PROPERTY(int silenceWarnMinutes READ silenceWarnMinutes CONSTANT)
    // The recording's tags: [{ id, name }]. Editable before starting and during recording; the
    // meeting is only created at the end of the recording, so the core attaches them at the end
    // (AppController::setRecordingTags).
    Q_PROPERTY(QVariantList tags READ tags NOTIFY tagsChanged)
    Q_PROPERTY(QStringList tagIds READ tagIds NOTIFY tagsChanged)
    Q_PROPERTY(bool tagsEditable READ tagsEditable NOTIFY stateChanged)
    // Design state ("R01" ... "R10"); only effective without a controller. "" = no demo.
    Q_PROPERTY(QString demoState READ demoState WRITE setDemoState NOTIFY demoStateChanged)
    // The core; defaults to App.controller. Can be overridden in tests / embedding.
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

    // The automatic name: "<App> call · Oct 3, 2:02 PM", without an app "Meeting · ...".
    static QString automaticTitle(const QString& appName, const QDateTime& when);
    // RMS (0..1) -> 0..14 segments (dB scale: -60 dB = 0, 0 dB = 14).
    static int levelSegments(float rms);

    // External request (command line / watcher / main window): title or app name, device indices
    // (the capture list's row numbers; empty = the saved selection), context note.
    Q_INVOKABLE void applyRequest(const QString& title, const QString& appName,
                                  const QString& context, const QList<int>& deviceIndexes = {});

    QVariantList tags() const;
    QStringList tagIds() const;
    bool tagsEditable() const;
    // Adds a tag to the recording by name (creating it in the pool if needed). False: not
    // editable now, the name is empty, or it is already on it.
    Q_INVOKABLE bool addTag(const QString& name);
    Q_INVOKABLE void removeTag(const QString& id);
    // Ctrl+T: opens the tag field (the view opens it on the tagInputRequested signal).
    Q_INVOKABLE bool openTagInput();
    // Wires only the tags to the core (WITHOUT device enumeration or level monitoring), for
    // tests where touching audio devices is forbidden. The full wiring is setController.
    void attachTagsOnly(tanara::AppController* controller);

public slots:
    void start();              // Ctrl+R
    void stop();               // Ctrl+. : without confirmation (the button is explicit)
    void toggleDevice(int row);
    void rescan();             // "Rescan"
    void newRecording();       // "New recording" (with the same sources)
    void openInAnalyzer();     // "Open in analyzer"
    void openSettings();       // "Recording settings"
    void continueRecording();  // R06 "Continue"
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
    // For the recorder window / the enclosing process:
    void askRaised(QString title, QString text);        // show up + system notification
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
        bool recorded = false;       // the running recording's live track
        bool disconnected = false;   // was recorded, but was unplugged
        QString app;
        float rms = 0.f, peak = 0.f; // latest raw values
        int level = 0;
        int peakSeg = -1;
        qint64 peakAt = 0;           // when the peak hold started (ms, monotonic)
        qint64 lastSignalAt = 0;     // when there was last a signal (ms, monotonic); 0 = never
        qint64 silentSince = 0;      // start of the recorded track's silence (ms, monotonic)
        QString status, statusText;
        bool demoFixed = false;      // demo: status / level are not recomputed
    };

    void attach();                       // signals to the controller
    void rebuildDevices();               // device re-enumeration -> rows (selection preserved)
    void updateRoutes();
    void onLevel(const QString& device, float rms, float peak);
    void tick();                         // 30 Hz: peak hold, statuses, model refresh
    void refreshRow(int row, bool levelOnly);
    void computeStatus(Row& r, qint64 now) const;
    void setState(const QString& state);
    void onRecordingState(tanara::RecordingState st);
    void onFinished(const tanara::Meeting& m);
    void persistSelection();
    void raiseAsk(const QString& text);
    void loadDemo();
    QString iconFor(const Row& r) const;
    void syncRecordingTags();            // handing the tags to the core (live during recording)
    void refreshTagNames();              // renamed / deleted tag in the pool
    int rowOf(const QString& deviceName) const;
    QVector<tanara::AudioDeviceInfo> selectedDevices() const;

    RecorderDeviceModel m_model{this};
    QVector<Row> m_rows;
    QPointer<tanara::AppController> m_controller;
    QPointer<tanara::AppController> m_tagCtl;    // the core of the tags (= m_controller, or attachTagsOnly)
    QMetaObject::Connection m_tagConn;
    bool m_controllerSet = false;
    tanara::PlaybackRouteMonitor* m_routes = nullptr;
    QTimer m_tick;
    QTimer m_fallbackScan;               // where there is no route watcher: timed re-enumeration
    QElapsedTimer m_clock;

    QString m_state = QStringLiteral("idle");
    QString m_title;
    bool m_titleAuto = true;
    QString m_appName;                   // the detected call app (from the watcher): automatic name + the meeting's detectedCallApp field
    QString m_context;                   // explicit --context note (the watcher no longer sends one)
    qint64 m_elapsedMs = 0;
    bool m_askVisible = false;
    QString m_askText;
    QString m_doneSummary, m_doneMeetingId, m_doneProblem;
    QString m_errorText;
    QString m_demoState;
    struct TagRef { QString id, name; };
    QVector<TagRef> m_tags;
    QStringList m_knownDevices;          // what we have already seen (to recognize a "new device")
    bool m_selectionLoaded = false;
};

} // namespace tanara_qml
