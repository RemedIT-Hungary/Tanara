#pragma once
//
// PersonReferenceEditor: the meeting-side files a person operation (rename / delete / merge, moving a
// voice sample) touches beyond meeting.json: the transcript editor's speaker overlay, the summary's
// stale marks and the derived transcript.md. Their format is the editing module's
// (editing::speakeredit), so editing implements this port (editing::PeopleEditingAdapter) and
// app::Services wires it into library::PeopleService (the inversion of cleanup plan T 1.2).
#include "tanara/domain/Meeting.h"
#include "tanara/domain/Overlay.h"
#include "tanara/domain/Person.h"

#include <QString>
#include <QStringList>

namespace tanara::library {

class PersonReferenceEditor {
public:
    virtual ~PersonReferenceEditor() = default;

    // ---- summary stale marks ----
    // Marks the summary stale for these speaker keys ON DISK (no signal); returns the keys added.
    virtual QStringList markSummaryStaleKeys(const Meeting& m, const QStringList& speakerKeys) = 0;
    // Undo of one markSummaryStaleKeys; true if the state changed.
    virtual bool unmarkSummaryStale(const QString& meetingFolder, const QStringList& speakerKeys) = 0;
    // A speaker key of a manually added participant ("participant:N"), not a raw label.
    virtual bool isParticipantKey(const QString& key) const = 0;

    // ---- the speaker overlay ----
    // Whether the overlay refers to the person (participants, identifications).
    virtual bool overlayRefersTo(const QString& meetingFolder, const PersonId& person) const = 0;
    virtual bool removePersonFromOverlay(const QString& meetingFolder, const PersonId& person) = 0;
    virtual OverlayPersonRefs replacePersonInOverlay(const QString& meetingFolder, const PersonId& from,
                                                     const PersonId& to) = 0;
    virtual bool restorePersonInOverlay(const QString& meetingFolder, const OverlayPersonRefs& refs,
                                        const PersonId& current, const PersonId& original) = 0;

    // ---- derived files ----
    // transcript.md regenerated with the current names (no-op without a transcript).
    virtual void rewriteTranscriptNames(const Meeting& m, const PersonNames& names) = 0;
};

} // namespace tanara::library
