#pragma once
//
// MeetingUndoService: ONE meeting's shared undo stack (design decision: "One shared undo stack for
// tags and speakers"). A UI-independent QObject; an instance comes from AppController::meetingUndo.
//
// The two existing stacks stay untouched: SpeakerEditor's (transcript editor) and TagService's
// (tags, global). This layer only keeps track of which source's step is the most recent IN TIME ORDER,
// and undo() reverses that through its own source. As a third source it also keeps its own steps: the
// participant operations (approve, skip, "Only I spoke", including / removing a track, adding /
// deleting a participant, description), which AppController records with a restoring function.
//
// Rules:
//  - Steps of the source stacks are identified by a serial number (SpeakerEditor::undoSerials,
//    TagService::undoSerials). A new serial on a source's top -> a new entry on top of the shared stack
//    (the editor's redo is like that too: the restored step is the most recent). A vanished serial (it
//    was undone directly in the source, the stack reloaded / overflowed) -> the entry drops out too.
//  - TagService is global: only its steps belonging to the meeting come here (those touching the
//    meeting's tags, a rejection for the meeting, or a TagService::beginGroup(label, meetingId) group). If
//    another step of TagService (e.g. from the Tags window) landed above a tag step belonging here, it
//    cannot be undone from here while that one is there: the entry is skipped.
//  - A participant step may "absorb" an editor step created during the operation (e.g. the approval's
//    bindings): then it is one entry, and undoing it reverses both (the editor's only if it is still on top).
//  - Source steps that existed before attaching do not come here.
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVector>

#include <functional>
#include <memory>

namespace tanara {

class SpeakerEditor;
class TagService;

class MeetingUndoService : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(QString undoText READ undoText NOTIFY changed)
public:
    enum class Source { Speakers, Tags, Participants };
    Q_ENUM(Source)

    explicit MeetingUndoService(const QString& meetingId, QObject* parent = nullptr);
    ~MeetingUndoService() override;

    QString meetingId() const;

    // Attaching the sources (nullptr: detach, the source's entries drop out). It detaches by itself when the
    // editor goes away.
    void attachSpeakerEditor(SpeakerEditor* editor);
    void attachTagService(TagService* tags);
    SpeakerEditor* speakerEditor() const;
    TagService* tagService() const;

    // Own (participant) step. undo: the restore (on the main thread, called by the shared undo()).
    // absorbEditorSerial: serial of the editor step created during the operation (0: none); its entry
    // merges into this step.
    void pushStep(const QString& text, std::function<void()> undo, quint64 absorbEditorSerial = 0);

    // The most recent undoable entry (blocked tag steps skipped).
    bool canUndo() const;
    QString undoText() const;
    Source undoSource() const;   // invalid if !canUndo()
    // All entries (including blocked ones): diagnostics / tests.
    int count() const;
    // The entries' texts in time order (oldest first): diagnostics / tests.
    QStringList texts() const;

public slots:
    // Undoes the most recent through its own source. false: nothing to undo.
    bool undo();

signals:
    // canUndo / undoText / the entries changed.
    void changed();
    // A step was undone (for the "Undone: ..." feedback).
    void undone(QString text, tanara::MeetingUndoService::Source source);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace tanara
