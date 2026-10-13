#pragma once
//
// Tanara QML: FICTIONAL sample library for the shell's demo and screenshot mode (App.demo):
// there is no AppController; the library model, the "Waiting for you" list, the header and
// the player work from it. Names and texts are fictitious; real data must NEVER go here.
//
#include "tanara/library/MeetingLibrary.h"

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara_qml::demo {

struct DemoMeeting {
    tanara::LibraryEntry entry;    // title, time, length, states, participants
    QString text;                  // one sentence of "transcript" for the search snippet
    int speakers = 0;              // number of speakers (after the transcript)
    int tracks = 0;                // number of audio tracks
};

// The demo's "today" (fixed, so the date sections and the screenshots are stable).
QDateTime now();

const QVector<DemoMeeting>& meetings();
const DemoMeeting* find(const QString& meetingId);

// The same contract as MeetingLibrary::query / people / pendingItems.
tanara::LibraryResult query(const tanara::LibraryQuery& q);
QVector<tanara::PersonPresence> people();
QVector<tanara::PendingItem> pendingItems();

// ---- tags (fictional pool; the bulk-tagging demo may modify it in memory) ----
struct DemoTag {
    QString id;
    QString name;
};
QVector<DemoTag> tagCatalog();
QString tagName(const QString& tagId);              // empty if none
QString tagIdByName(const QString& name);           // tagKey match; empty if none
QString createTag(const QString& name);             // returns the existing one if the name is taken
QStringList tagsOf(const QString& meetingId);
void setTagsOf(const QString& meetingId, const QStringList& tagIds);
void resetTags();                                   // back to the initial state (tests)
// The same contract as MeetingLibrary::tagOptions / untaggedCount / entry.
QVector<tanara::TagUsage> tagOptions();
int untaggedCount();
tanara::LibraryEntry entry(const QString& meetingId);

// Signals a change in the demo's tags (the library model refreshes on it).
class DemoTagNotifier : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
signals:
    void changed();
};
DemoTagNotifier* tagNotifier();

// The meeting opened by default (it has a transcript and a stale summary).
QString defaultMeetingId();
// A meeting without a transcript (failed transcription): for the "preTranscript" state's image.
QString failedMeetingId();

} // namespace tanara_qml::demo
