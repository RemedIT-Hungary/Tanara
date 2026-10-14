#pragma once
//
// IMeetingNoteFacade: what the meeting note editor (MeetingNoteModel: the pre-transcript view's
// step 1 and the Summary tab) needs from the core: a meeting's note and detected call app, saving
// the note (with its undo step), the template suggestions from similar meetings, and the source
// meetings' tag names.
//
// Only the real side exists (gui/qml/src/MeetingNoteServicesFacade): in the demo the owner view
// model fills the note editor's sample content itself, the note model then has no facade. It is a
// QObject so that the owners' transitional noteController() hooks can hand it over.
#include "tanara/library/MeetingNotes.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara::library {

class IMeetingNoteFacade : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~IMeetingNoteFacade() override = default;

    // The meeting's note (Meeting::contextNote) and the app the watcher recognized ("" if none).
    struct Note {
        QString text;
        QString detectedCallApp;
    };
    virtual Note note(const QString& meetingId) const = 0;
    virtual void setNote(const QString& meetingId, const QString& text) = 0;
    // At most `limit` notes of other meetings with a similar title (newest first).
    virtual QVector<meetingnotes::NoteSuggestion> suggestions(const QString& meetingId, const QString& currentNote,
                                                              int limit) const = 0;
    // The meeting's tag names, in the meeting's order.
    virtual QStringList tagNames(const QString& meetingId) const = 0;
};

} // namespace tanara::library
