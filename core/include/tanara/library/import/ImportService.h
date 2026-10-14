#pragma once
//
// ImportService: the audio-import operation around AudioImporter. It reflects the import in the
// MeetingJobTracker (JobKind::Import, under the FUTURE meeting's id: the meeting does not exist in the
// library until success) and re-emits the importer's end signals. The caller (AppController) decides
// what follows a finished import (mixdown / speech check). UI-independent (QtCore).
#include "tanara/domain/Meeting.h"
#include "tanara/domain/Person.h"
#include "tanara/media/MediaTool.h"

#include <QObject>

namespace tanara::library {

class AudioImporter;
struct ImportRequest;
class MeetingJobTracker;
class MeetingRepository;

class ImportService : public QObject {
    Q_OBJECT
public:
    ImportService(MeetingRepository* store, media::MediaToolPtr media, MeetingJobTracker* jobs,
                  QObject* parent = nullptr);
    ~ImportService() override;

    // The underlying engine (file info: probeAsync; progress / busy for the shells).
    AudioImporter* importer() const;

    bool busy() const;
    // Starts an import with the given track bitrate and "own microphone" speaker. Returns the future
    // meeting's id; empty (and rejected(message)) if one is already running.
    QString start(const ImportRequest& request, int opusBitrateKbps, const SelfPerson& self);
    // Cancels the running import of that future meeting id. False if it is not running.
    bool cancel(const QString& meetingId);

signals:
    void rejected(QString message);
    void finished(tanara::Meeting meeting);
    void failed(QString importId, QString message, QString detail);
    void cancelled(QString importId);

private:
    AudioImporter*     m_importer = nullptr;
    MeetingJobTracker* m_jobs = nullptr;
};

} // namespace tanara::library
