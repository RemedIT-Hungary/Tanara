#pragma once
//
// IVoiceModelsFacade: what the voice-model list (Settings > Speakers, the "Getting started" row,
// the People window's backfill note) needs from the core: where the model files live, which models
// are enabled, deleting / rescanning a model file, and the voiceprint backfill (running, its
// progress, the samples it skipped).
//
// The real implementation composes SettingsManager and the voice analysis' notifications
// (gui/qml/src/VoiceModelsServicesFacade); the fictional one (demo/VoiceModelsDemoFacade) serves
// --demo, --gallery, --qml-shot and the tests: no files, no downloads, and a design state
// (scenario()) that says how each model and the backfill look. It is a QObject only because it
// carries signals.
#include "tanara/processing/voiceid/BackfillReport.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace tanara::processing {

// The design state of a fictional facade: how the list looks (the real facade has none).
struct VoiceModelScenario {
    struct Row {
        QString state = QStringLiteral("downloaded");   // missing | downloading | downloaded | bundled | verifying | error | offline
        double receivedFraction = 0.0;                   // downloading: how far
        QString errorText;                               // error: the human text
        bool corrupt = false;                            // error: the existing file has the wrong size ...
        qint64 fileSize = -1;                            // ... this one
    };
    QHash<QString, Row> rows;    // model id -> state; a model not listed is "downloaded"
    bool offline = false;
    QStringList enabled;         // the enabled models
    QString backfillState;       // "" | running | done | partial
    int backfillTotal = 0, backfillDone = 0, backfillSkipped = 0;
};

class IVoiceModelsFacade : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~IVoiceModelsFacade() override = default;

    // False: fictional data; no file is read, downloaded or deleted.
    virtual bool live() const { return true; }
    virtual std::optional<VoiceModelScenario> scenario() const { return std::nullopt; }

    // The downloaded models' folder (empty: none).
    virtual QString modelsDir() const = 0;
    // The enabled models (the saved setting) / saving it.
    virtual QStringList enabledModels() const = 0;
    virtual void setEnabledModels(const QStringList& ids) = 0;
    // A model file arrived for an already enabled model: the core re-reads the files (and backfills).
    virtual void reloadModels() = 0;
    // Deletes a downloaded model file. false (+ *error): unknown id / no file / in use.
    virtual bool deleteModelFile(const QString& id, QString* error) = 0;
    // After a manually copied model file: the core recomputes the models in use (modelsChanged).
    virtual void rescan() = 0;

    virtual bool backfillRunning() const = 0;
    // The samples the latest backfill skipped. person: the asking view's person (a fictional facade
    // may serve that one's; the caller filters anyway).
    virtual QVector<VoiceprintBackfillSkip> backfillSkips(const QString& person = QString()) const = 0;
    // A source meeting's title (empty: unknown).
    virtual QString meetingTitle(const QString& meetingId) const = 0;

signals:
    void modelsChanged();
    void settingsChanged();
    void backfillStarted(int total);
    void backfillProgress(int done, int total);
    void backfillFinished(int added, int skipped);
};

} // namespace tanara::processing
