#pragma once
//
// PersonMerge: what merging two people (AppController::mergePeople) changed, so the merge can be undone
// exactly (AppController::undoMergePeople; PeopleService keeps it on its undo stack).
//
// Merging `drop` into `keep` rewrites the REFERENCES to `drop` (voiceprints.json, tags.json rejections,
// every meeting's speakerMap / participants / approval candidates / speaker overlay) to `keep`, and folds
// `drop`'s people.json record into `keep`'s. Because people are referred to by PersonId, the undo puts
// back exactly the recorded references (those still pointing to `keep`) and both records as they were.
#include "tanara/domain/Person.h"
#include "tanara/domain/Overlay.h"
#include "tanara/library/tags/TagService.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara::library {

// The references of one meeting that the merge rewrote.
struct PersonMergeMeeting {
    QString     meetingId;
    QStringList speakerMapKeys;         // raw labels whose person was `drop`
    QStringList participantIds;         // meeting.json participants of `drop`
    QStringList approvalParticipantIds; // participantApproval candidates of `drop`
    OverlayPersonRefs overlay;
    bool isEmpty() const
    {
        return speakerMapKeys.isEmpty() && participantIds.isEmpty() && approvalParticipantIds.isEmpty()
            && overlay.isEmpty();
    }
};

struct PersonMergeUndo {
    PersonId keep;
    PersonId drop;
    Person   keepBefore;                // the two people.json records before the merge
    Person   dropBefore;
    QStringList movedPrintIds;          // voiceprints moved from `drop` to `keep`
    QVector<PersonRejectionMove> rejections;
    QVector<PersonMergeMeeting> meetings;
    bool     selfWasDrop = false;       // the user's own person was `drop` (it moved to `keep`)
    QString  selfNameBefore;            // the settings' name before the merge
    bool isValid() const { return !keep.isNull() && !drop.isNull(); }
};

} // namespace tanara::library
