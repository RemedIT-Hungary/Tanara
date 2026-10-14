#pragma once
//
// MeetingUndoRegistry: the per-meeting shared undo stacks (MeetingUndoService, editing/undo/MeetingUndo.h)
// and the participant operations' undo step (ParticipantUndoScope). Moved unchanged from AppController
// (AppController::meetingUndo, ParticipantUndoScope; cleanup phase 2, P12d): ONE stack per meeting,
// created lazily (opening the editor creates it too), with the TagService and the meeting's open editor
// attached; it ends when the meeting is deleted.
//
// (MeetingUndoService is the name of the per-meeting stack class, so the owner of the stacks is the
// "registry".)
#include "tanara/domain/Meeting.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>

namespace tanara::library {
class MeetingRepository;
class TagService;
}

namespace tanara::editing {

class MeetingUndoService;
class SpeakerEditor;

class MeetingUndoRegistry : public QObject {
    Q_OBJECT
public:
    // existingEditor: the meeting's open transcript-editor session, without creating one (nullptr: none).
    MeetingUndoRegistry(library::MeetingRepository* store, library::TagService* tags,
                        std::function<SpeakerEditor*(const QString& meetingId)> existingEditor,
                        QObject* parent = nullptr);
    ~MeetingUndoRegistry() override;

    // The meeting's shared undo stack (created lazily; the open editor is (re)attached on every call).
    // Unknown meeting = nullptr.
    MeetingUndoService* forMeeting(const QString& meetingId);

signals:
    // A participant step was undone (the restored snapshot): the participants changed; the tracks' inclusion
    // changed (then a remix is requested if an included track remains).
    void participantsChanged(QString meetingId);
    void tracksChanged(QString meetingId);
    void remixRequested(QString meetingId);

private:
    friend class ParticipantUndoScope;
    void restore(const Meeting& snap);

    library::MeetingRepository* m_store = nullptr;
    library::TagService* m_tags = nullptr;
    std::function<SpeakerEditor*(const QString&)> m_existingEditor;
    QHash<QString, QPointer<MeetingUndoService>> m_services;   // meetingId -> shared stack
    QHash<QString, int> m_scopeDepth;                           // nesting of ParticipantUndoScope
};

// ==== the shared undo step of the participant operations ===================================
// RAII: BEFORE the operation it takes a snapshot of the meeting's "participant state" (participants,
// approval, description, track inclusion), and AFTER it, if changed, pushes one step onto the meeting's
// shared stack (MeetingUndoService) that restores this state. An editor step created during the operation
// (bindings) is absorbed by the step. When nested, only the outermost records (e.g. removeParticipant ->
// unbindParticipant).
class ParticipantUndoScope {
public:
    ParticipantUndoScope(MeetingUndoRegistry* registry, const QString& meetingId, const QString& label);
    ~ParticipantUndoScope();
    ParticipantUndoScope(const ParticipantUndoScope&) = delete;
    ParticipantUndoScope& operator=(const ParticipantUndoScope&) = delete;

    QString text;   // the step's text (may be refined during the operation)

private:
    MeetingUndoRegistry* m_registry = nullptr;
    QString m_meetingId;
    bool    m_active = false;
    Meeting m_before;
    quint64 m_editorTopBefore = 0;
};

} // namespace tanara::editing
