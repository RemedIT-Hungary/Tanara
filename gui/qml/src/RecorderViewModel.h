#pragma once
//
// RecorderViewModel: view model of the floating recorder (RecorderView.qml / RecorderWindow.qml).
//
// One object holds the recorder's whole state: title (automatic name + rename), device list
// (three groups, toggles, friendly + raw name, live level with peak hold, signal hints),
// recording state and elapsed time, the "End of the meeting?" question, the done state.
// It reaches the recording side through ONE injected facade (tanara::recording::IRecorderFacade):
// the real one (RecorderServicesFacade) or the fictional one of tanara_demo (--qml-shot / --demo /
// --gallery, tests: made-up devices and levels, the design states "R01" ... "R10"). The view
// model does not know which one it has.
//
#include "tanara/domain/Audio.h"
#include "tanara/recording/IRecorderFacade.h"

#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QHash>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVector>
#include <QtQml/qqmlregistration.h>

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
        StalledRole,                  // the recorded track delivers no data (warn dot)
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
    QML_UNCREATABLE("Created in C++ with a facade (RecorderWindowHost, App.createRecorderDemo).")

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
    // Keep-awake while recording: "held" | "off" | "unavailable" ("" = unknown). The view warns
    // only when it is not in effect ("off" / "unavailable") during a recording.
    Q_PROPERTY(QString keepAwakeState READ keepAwakeState NOTIFY keepAwakeChanged)
    // Live capture warning: a recorded track delivers NO data (IRecorderFacade::trackStalled), e.g. the
    // microphone was lost when the machine slept. Empty when every track delivers; the seconds update.
    Q_PROPERTY(QString stallWarning READ stallWarning NOTIFY stallChanged)
    Q_PROPERTY(bool stalled READ stalled NOTIFY stallChanged)
    // Resume after a sleep / a lost device: "The recording was interrupted (...), resuming… N s" while
    // the core re-opens the lost tracks, then "Resumed in a new segment" for a few seconds; empty
    // otherwise. resuming: re-opening is in progress.
    Q_PROPERTY(QString resumeText READ resumeText NOTIFY resumeChanged)
    Q_PROPERTY(bool resuming READ resuming NOTIFY resumeChanged)
    // Threshold of the per-track silence warning (minutes).
    Q_PROPERTY(int silenceWarnMinutes READ silenceWarnMinutes CONSTANT)
    // The recording's tags: [{ id, name }]. Editable before starting and during recording; the
    // meeting is only created at the end of the recording, so the core attaches them at the end
    // (IRecorderFacade::setRecordingTags).
    Q_PROPERTY(QVariantList tags READ tags NOTIFY tagsChanged)
    Q_PROPERTY(QStringList tagIds READ tagIds NOTIFY tagsChanged)
    Q_PROPERTY(bool tagsEditable READ tagsEditable NOTIFY stateChanged)
    // The tag input's data source (TagInputModel.backend): from the facade.
    Q_PROPERTY(QObject* tagBackend READ tagBackendObject CONSTANT)

public:
    // How much of the facade the view model wires.
    enum class Wiring {
        Full,       // devices, levels, recording, tags
        TagsOnly,   // only the tags (no device enumeration or level monitoring), for tests where
                    // touching audio devices is forbidden
    };
    // The facade is not taken over (the caller / the QML engine keeps it alive).
    explicit RecorderViewModel(tanara::recording::IRecorderFacade* facade, QObject* parent = nullptr);
    RecorderViewModel(tanara::recording::IRecorderFacade* facade, Wiring wiring, QObject* parent = nullptr);
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
    QString keepAwakeState() const { return m_facade ? m_facade->keepAwakeState() : QString(); }
    QString stallWarning() const { return m_stallWarning; }
    bool stalled() const { return !m_stalled.isEmpty(); }
    QString resumeText() const { return m_resumeText; }
    bool resuming() const { return !m_reopening.isEmpty(); }
    int silenceWarnMinutes() const { return 3; }
    QObject* tagBackendObject() const;

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
    void keepAwakeChanged();
    void stallChanged();
    // A track has just stalled (once per stall, not per tick): the host notifies a hidden window's user.
    void stallRaised(QString title, QString text);
    void resumeChanged();
    // An interruption began / the recording resumed / re-opening gave up (once per event): the host
    // notifies a hidden window's user.
    void interruptionRaised(QString title, QString text);
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
        bool pinned = false;         // the facade pins level / status (fixtures): not recomputed
    };

    void attach();                       // signals from the facade
    void attachTags();
    void reloadFromFacade();             // the fixture loaded another session: everything again
    void applySession(const tanara::recording::RecorderSession& s);
    void rebuildDevices();               // device re-enumeration -> rows (selection preserved)
    void applyPin(Row& r) const;         // a facade-pinned level / status
    void updateRoutes();
    void onLevel(const QString& device, float rms, float peak);
    void tick();                         // 30 Hz: peak hold, statuses, model refresh
    void refreshRow(int row, bool levelOnly);
    void computeStatus(Row& r, qint64 now) const;
    void setState(const QString& state);
    void onRecordingState(tanara::RecordingState st);
    void onFinished(const tanara::recording::RecordingOutcome& outcome);
    void persistSelection();
    void raiseAsk(const QString& text);
    void onTrackStalled(const QString& device, qint64 sinceMs);
    void onTrackResumed(const QString& device);
    void updateStallWarning(qint64 elapsedMs);   // the text from m_stalled at the recording clock
    void clearStalls();
    void onTrackReopening(const QString& device, const QString& reason);
    void onReopenEnded(const QString& device, bool resumed);
    void updateResumeText();             // the text from m_reopening / the resumed note at m_clock
    void clearResume();
    QString shownName(const QString& device) const;   // the row's friendly name, otherwise the raw one
    QString iconFor(const Row& r) const;
    void syncRecordingTags();            // handing the tags to the core (live during recording)
    void refreshTagNames();              // renamed / deleted tag in the pool
    int rowOf(const QString& deviceName) const;
    QVector<tanara::AudioDeviceInfo> selectedDevices() const;

    RecorderDeviceModel m_model{this};
    QVector<Row> m_rows;
    QPointer<tanara::recording::IRecorderFacade> m_facade;
    bool m_attached = false;             // Wiring::Full
    bool m_reloading = false;
    QTimer m_tick;
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
    QMap<QString, qint64> m_stalled;     // stalled track's device -> last data (recording clock ms)
    QString m_stallWarning;
    QMap<QString, QString> m_reopening;  // lost track's device -> reason ("sleep" | "device-lost")
    qint64 m_reopenSince = 0;            // m_clock when the current interruption began
    qint64 m_resumedAt = -1;             // m_clock when it resumed ("Resumed" note shown); -1 = none
    QString m_resumeText;
    struct TagRef { QString id, name; };
    QVector<TagRef> m_tags;
    QStringList m_knownDevices;          // what we have already seen (to recognize a "new device")
    bool m_selectionLoaded = false;
};

} // namespace tanara_qml
