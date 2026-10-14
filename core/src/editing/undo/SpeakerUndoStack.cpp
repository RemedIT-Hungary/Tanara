#include "tanara/editing/undo/SpeakerUndoStack.h"

#include <atomic>

namespace tanara::editing {

void SpeakerUndoStack::push(SpeakerEditCommand command)
{
    static std::atomic<quint64> nextSerial{0};
    command.serial = ++nextSerial;
    m_undo.append(std::move(command));
    if (m_undo.size() > kMaxSteps) m_undo.removeFirst();
    m_redo.clear();
}

SpeakerEditCommand SpeakerUndoStack::takeUndo()
{
    const SpeakerEditCommand c = m_undo.takeLast();
    m_redo.append(c);
    return c;
}

SpeakerEditCommand SpeakerUndoStack::takeRedo()
{
    const SpeakerEditCommand c = m_redo.takeLast();
    m_undo.append(c);
    return c;
}

void SpeakerUndoStack::clear()
{
    m_undo.clear();
    m_redo.clear();
}

QVector<quint64> SpeakerUndoStack::undoSerials() const
{
    QVector<quint64> out;
    out.reserve(m_undo.size());
    for (const SpeakerEditCommand& c : m_undo) out << c.serial;
    return out;
}

} // namespace tanara::editing
