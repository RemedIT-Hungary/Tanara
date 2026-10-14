#pragma once
//
// SpeakerUndoStack: the in-memory undo / redo stack of ONE speaker-editor session (SpeakerEditSession).
// A step is a snapshot pair of the overlay (before / after) + the side effects outside the overlay that
// undo must reverse (the affected Meeting.speakerMap keys, voiceprint additions / removals, people created
// by the step). The stack only stores the steps; applying them is the session's job.
//
// At most kMaxSteps steps are kept (the oldest drops). Every pushed step gets a serial number unique for
// the process life (the shared undo, editing/undo/MeetingUndo.h, merges the stacks by it); a step keeps
// its number after redo.
#include "tanara/domain/Overlay.h"
#include "tanara/domain/Person.h"
#include "tanara/domain/Voice.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara::editing {

// A change of one Meeting.speakerMap key (undo restores only the AFFECTED keys, so a change arriving
// meanwhile from outside, the old UI or auto-identification, is not overwritten).
struct MapDelta {
    QString  raw;
    bool     hadBefore = false;
    PersonId before;
    bool     hasAfter = false;
    PersonId after;
};

// A voiceprint side effect (when fixing a "wrong recognition"): undo reverses it.
struct VoiceprintOp {
    bool       added = false;   // true: ADDED to the person; false: REMOVED from them
    PersonId   person;
    Voiceprint print;
};

// One undoable step.
struct SpeakerEditCommand {
    QString text;
    quint64 serial = 0;     // for the shared undo adapter (undoSerials); assigned by SpeakerUndoStack::push
    SpeakerOverlay before;
    SpeakerOverlay after;
    QVector<MapDelta> map;
    QVector<VoiceprintOp> voiceprints;
    QVector<Person> peopleAdded;    // people created by the step (undo deletes, redo restores them)
    QStringList affectedSpeakers;
};

class SpeakerUndoStack {
public:
    static constexpr int kMaxSteps = 200;

    // A new step: gets the next serial, goes on top, the redo stack is cleared.
    void push(SpeakerEditCommand command);
    // Moves the top undo step onto the redo stack and returns it (the caller applies it backwards).
    // Precondition: canUndo().
    SpeakerEditCommand takeUndo();
    // Moves the top redo step back onto the undo stack and returns it (the caller applies it forwards).
    // Precondition: canRedo().
    SpeakerEditCommand takeRedo();
    // Drops every step (external change / new transcript).
    void clear();

    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    QString undoText() const { return m_undo.isEmpty() ? QString() : m_undo.last().text; }
    QString redoText() const { return m_redo.isEmpty() ? QString() : m_redo.last().text; }
    // The serial numbers of the undoable steps, bottom to top.
    QVector<quint64> undoSerials() const;

private:
    QVector<SpeakerEditCommand> m_undo;
    QVector<SpeakerEditCommand> m_redo;
};

} // namespace tanara::editing
