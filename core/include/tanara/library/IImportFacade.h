#pragma once
//
// IImportFacade: what the "Import audio file" window's model needs from the import side: probing
// the picked files, starting / cancelling the import, the tags put on the new meeting. The real
// implementation wraps the AudioImporter, the import job and the TagService
// (gui/qml/src/ControllerFacades); the fictional one (demo/ImportDemoFacade) serves the window's
// design states ("files", "split", "probing", "error", "failed", "progress") for --demo, --gallery,
// --qml-shot and the tests.
#include "tanara/domain/Meeting.h"
#include "tanara/domain/Tag.h"
#include "tanara/library/import/AudioImporter.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace tanara::library {

// One row of a design state's file list.
struct ImportScenarioFile {
    QString path;
    bool probed = false;
    ImportFileInfo info;
    bool split = false;
};

// The fixed content of a design state (the window's "demoState"): files, tags, the suggestion, the
// error or the running import. Only fictional facades serve one.
struct ImportScenario {
    struct TagRef { QString id; QString name; };
    struct Suggestion { QString id; QString name; QString source; QString reason; };

    QVector<ImportScenarioFile> files;
    int ownTrack = -1;
    QVector<TagRef> tags;
    std::optional<Suggestion> suggestion;
    QString error;                 // the window's error line ("failed")
    bool running = false;          // an import in progress ("progress")
    int percent = -1;
    int fileIndex = 0;
    int fileTotal = 0;
};

class IImportFacade : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~IImportFacade() override = default;

    // True: the facade imports for real (a fictional one never starts anything, and the suggestions
    // for a typed title are not recomputed: the design state's suggestion stays).
    virtual bool live() const = 0;
    // Whether the "Import" button may light up when a design state (scenario) is loaded.
    virtual bool canStart(const QString& scenario) const = 0;

    // File info comes back as probed().
    virtual void probeAsync(const QString& path) = 0;
    // The future meeting's id; empty if refused.
    virtual QString start(const ImportRequest& request) = 0;
    virtual void cancel(const QString& importId) = 0;

    // The tags: a name creates the tag if it does not exist (the existing one is returned for an
    // existing key); an invalid Tag if it cannot be done. applyTags puts the tags that still exist
    // on the new meeting.
    virtual Tag createTag(const QString& name) = 0;
    virtual void applyTags(const QString& meetingId, const QStringList& tagIds) = 0;
    // Tag suggestions for the typed title.
    virtual QVector<TagSuggestion> suggestionsFor(const QString& title) const = 0;

    // The design state's fixed content (an empty state: an empty scenario); nullopt for a live facade.
    virtual std::optional<ImportScenario> scenario(const QString& scenario) const = 0;

signals:
    void probed(tanara::library::ImportFileInfo info);
    void progress(QString importId, int percent, int fileIndex, int fileCount);
    void finished(tanara::Meeting meeting);
    void failed(QString importId, QString message, QString detail);
    void cancelled(QString importId);
};

} // namespace tanara::library
