#pragma once
//
// ArchiveService: the asynchronous meeting-archive operations (export to *.tanara.zip, import of an
// archive or of a plain meeting folder) around the static MeetingArchive helpers.
//
//  - One operation at a time, on a background thread; the result comes as signals. The operation is
//    reflected in the MeetingJobTracker (JobKind::Export, under the exported meeting's id).
//  - Export is cancellable (cancel(meetingId)); unpacking an archive / staging a folder is not (it is
//    short, and cleans up at the end anyway).
//  - The adoption of the unpacked folder (index + tag set) runs on the owner's thread.
// UI-independent (QtCore).
#include "tanara/domain/Meeting.h"

#include <QObject>
#include <QString>

#include <atomic>
#include <functional>
#include <memory>

namespace tanara::library {

class MeetingJobTracker;
class MeetingRepository;
class TagService;

class ArchiveService : public QObject {
    Q_OBJECT
public:
    ArchiveService(MeetingRepository* store, TagService* tags, MeetingJobTracker* jobs,
                   QObject* parent = nullptr);
    ~ArchiveService() override;

    // The owner's "is this meeting being recorded right now" probe: such a meeting cannot be exported.
    void setRecordingGuard(std::function<bool(const Meeting&)> isBeingRecorded);

    bool    busy() const;          // an operation is running
    QString currentOp() const;     // "export:<meetingId>" | "import:<uuid>", empty when idle

    // Asynchronous. Return the operation id; empty (and rejected(message)) if refused.
    QString exportMeeting(const QString& meetingId, const QString& zipPath);
    QString importArchive(const QString& zipPath);
    QString importFolder(const QString& folder);

    // Cancels the running export of the meeting. False if there is none (or it is not running).
    bool cancel(const QString& meetingId);

    // Blocking variants (CLI, tests): the same checks, no thread, no signals.
    bool    exportNow(const QString& meetingId, const QString& zipPath, QString* error,
                      std::function<void(int)> progress = {});
    Meeting importArchiveNow(const QString& zipPath, QString* error,
                             std::function<void(int)> progress = {});

signals:
    void rejected(QString message);                       // the operation did not start
    void progress(QString opId, int percent);
    // ok == false: message is the error ("Cancelled." text on cancel).
    void finished(QString opId, bool ok, QString meetingId, QString path, QString message);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace tanara::library
