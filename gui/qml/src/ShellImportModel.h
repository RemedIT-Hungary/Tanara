#pragma once
//
// ShellImportModel: the backend of the "Import audio file" dialog (ShellImportDialog.qml): the
// selected files with their data read in the background (length, channels, size), the
// per-file "separate track per channel" switch, the list and number of the future tracks, the
// "own microphone" choice, the title and the date (from the file by default, editable), then
// the running import's progress and cancellation. The work belongs to the core
// (tanara::AudioImporter via the AppController); without a controller (demo, screenshot) it
// shows fictional sample data.
//
#include "tanara/library/import/AudioImporter.h"

#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace tanara {
namespace app { class AppController; }
}

namespace tanara_qml {

class ShellImportModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    // [{ name, path, state: "probing"|"ok"|"error", meta, error, video, canSplit, split,
    //    channels, splitHint }]
    Q_PROPERTY(QVariantList files READ files NOTIFY filesChanged)
    Q_PROPERTY(int fileCount READ fileCount NOTIFY filesChanged)
    Q_PROPERTY(bool probing READ probing NOTIFY filesChanged)
    // The future tracks' display names (the "own microphone" can be chosen among these).
    Q_PROPERTY(QStringList trackNames READ trackNames NOTIFY filesChanged)
    Q_PROPERTY(int trackCount READ trackCount NOTIFY filesChanged)
    // "It will become 3 tracks.": a matching text during probing and without an importable file.
    Q_PROPERTY(QString trackSummary READ trackSummary NOTIFY filesChanged)
    Q_PROPERTY(int ownTrack READ ownTrack WRITE setOwnTrack NOTIFY filesChanged)
    Q_PROPERTY(bool canStart READ canStart NOTIFY filesChanged)

    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY titleChanged)
    // "2026-03-05 14:30" (editable; the date also accepts exact / space-separated forms).
    Q_PROPERTY(QString dateText READ dateText WRITE setDateText NOTIFY dateChanged)
    Q_PROPERTY(bool dateValid READ dateValid NOTIFY dateChanged)
    Q_PROPERTY(QString dateHint READ dateHint NOTIFY dateChanged)

    Q_PROPERTY(bool running READ running NOTIFY runChanged)
    Q_PROPERTY(bool cancelling READ cancelling NOTIFY runChanged)
    Q_PROPERTY(int percent READ percent NOTIFY runChanged)            // -1 = cannot be measured yet
    Q_PROPERTY(QString progressText READ progressText NOTIFY runChanged)
    Q_PROPERTY(QString runningTitle READ runningTitle NOTIFY runChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString errorDetail READ errorDetail NOTIFY errorChanged)

    // Tags: go onto the imported meeting when it is done. [{ id, name }]
    Q_PROPERTY(QVariantList tags READ tags NOTIFY tagsChanged)
    Q_PROPERTY(QStringList tagIds READ tagIds NOTIFY tagsChanged)
    // Suggestions by title (AppController::draftTagSuggestions; recomputed with a delay when the
    // title changes): [{ id, name, isNew, source, reason }]; reason is a short justification
    // ("similar title: "Focus group 2""). Attached and dismissed ones are not listed.
    Q_PROPERTY(QVariantList suggestions READ suggestions NOTIFY suggestionsChanged)
    Q_PROPERTY(QString suggestionReason READ suggestionReason NOTIFY suggestionsChanged)

    // For screenshots (without a controller): "files" | "split" | "probing" | "progress" | "error".
    Q_PROPERTY(QString demoState READ demoState WRITE setDemoState NOTIFY demoStateChanged)

public:
    explicit ShellImportModel(QObject* parent = nullptr);

    // Defaults to the App singleton's controller; injectable in tests.
    void setController(tanara::app::AppController* controller);

    QVariantList files() const;
    int fileCount() const { return int(m_rows.size()); }
    bool probing() const;
    QStringList trackNames() const;
    int trackCount() const;
    QString trackSummary() const;
    int ownTrack() const { return m_ownTrack; }
    void setOwnTrack(int index);
    bool canStart() const;

    QString title() const { return m_title; }
    void setTitle(const QString& title);
    QString dateText() const { return m_dateText; }
    void setDateText(const QString& text);
    bool dateValid() const { return parseDate(m_dateText).isValid(); }
    QString dateHint() const;

    bool running() const { return m_running; }
    bool cancelling() const { return m_cancelling; }
    int percent() const { return m_percent; }
    QString progressText() const;
    QString runningTitle() const { return m_runningTitle; }
    QString error() const { return m_error; }
    QString errorDetail() const { return m_errorDetail; }

    QString demoState() const { return m_demoState; }
    void setDemoState(const QString& state);

    QVariantList tags() const;
    QStringList tagIds() const;
    QVariantList suggestions() const;
    QString suggestionReason() const;
    // Tag by name (created in the pool if needed). False: empty / already on it.
    Q_INVOKABLE bool addTag(const QString& name);
    Q_INVOKABLE void removeTag(const QString& id);
    Q_INVOKABLE void acceptSuggestion(int index);
    // No longer asks for it in the window (the meeting does not exist yet: nothing to reject).
    Q_INVOKABLE void dismissSuggestion(int index);
    // Immediate recomputation of the suggestions (without the title-change delay; for tests).
    Q_INVOKABLE void refreshSuggestions();

    // Adds files (local paths or file:// URLs; drag and drop gives these); those already on the
    // list and folders are skipped. Returns how many were added.
    Q_INVOKABLE int addFiles(const QVariantList& pathsOrUrls);
    Q_INVOKABLE void removeFile(int row);
    Q_INVOKABLE void setSplit(int row, bool split);
    // Clears the form (does not affect a running import).
    Q_INVOKABLE void reset();
    Q_INVOKABLE void clearError();
    // Starts the import from the current form. False if it cannot start.
    Q_INVOKABLE bool start();
    Q_INVOKABLE void cancel();

    // Parsing the date field: "2026-03-05 14:30", "2026. 03. 05. 14:30", "2026-03-05" ...
    static QDateTime parseDate(const QString& text);

signals:
    void filesChanged();
    void titleChanged();
    void dateChanged();
    void runChanged();
    void errorChanged();
    void demoStateChanged();
    void tagsChanged();
    void suggestionsChanged();
    // The import finished: the new meeting's id (ShellActions does the selection).
    void imported(const QString& meetingId);
    void failed(const QString& message);

private:
    struct Row {
        QString path;
        tanara::library::ImportFileInfo info;
        bool probed = false;
        bool split = false;
    };
    void attach();
    void onProbed(const tanara::library::ImportFileInfo& info);
    void refreshDefaults();
    QVector<tanara::library::ImportSource> sources(QVector<tanara::library::ImportFileInfo>* infos) const;
    QVector<tanara::library::ImportPlannedTrack> plan() const;
    void setError(const QString& message, const QString& detail = QString());
    void loadDemo();
    void applyTags(const QString& meetingId, const QStringList& ids);

    struct TagRef { QString id, name; };
    struct Suggestion { QString id, name, source, reason; bool isNew = false; };

    QPointer<tanara::app::AppController> m_controller;
    QPointer<tanara::app::AppController> m_attached;
    bool m_controllerInjected = false;

    QVector<Row> m_rows;
    int m_ownTrack = -1;
    QString m_title;
    bool m_titleEdited = false;
    QString m_dateText;
    bool m_dateEdited = false;

    bool m_running = false;
    bool m_cancelling = false;
    int m_percent = -1;
    int m_fileIndex = 0;
    int m_fileTotal = 0;
    QString m_importId;
    QString m_runningTitle;
    QString m_error;
    QString m_errorDetail;
    QString m_demoState;

    QVector<TagRef> m_tags;
    QStringList m_runningTagIds;         // tags selected at start (attached at the end)
    QVector<Suggestion> m_suggestions;
    QStringList m_dismissed;             // dismissed suggestions (lowercase name)
    QTimer m_suggestTimer;
};

} // namespace tanara_qml
