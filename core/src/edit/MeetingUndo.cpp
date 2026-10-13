#include "tanara/edit/MeetingUndo.h"

#include "tanara/edit/SpeakerEditor.h"
#include "tanara/tags/TagService.h"

#include <QSet>

#include <algorithm>

namespace tanara {

namespace {
constexpr int kMaxEntries = 200;
}

struct MeetingUndoService::Impl {
    struct Entry {
        Source  source = Source::Participants;
        quint64 serial = 0;              // the source step's serial (for an own step: our own counter)
        QString text;
        std::function<void()> undo;      // only for an own step
        quint64 linkedEditor = 0;        // serial of the absorbed editor step (0: none)
    };

    MeetingUndoService* q = nullptr;
    QString meetingId;
    QPointer<SpeakerEditor> editor;
    QPointer<TagService> tags;
    QVector<QMetaObject::Connection> editorConns, tagConns;
    QVector<Entry> entries;              // in time order (the most recent last)
    QSet<quint64> ignoredEditor;         // editor steps that already existed at attach time
    QSet<quint64> ignoredTags;           // tag steps that existed at attach time / do not belong here
    quint64 nextOwn = 0;

    QVector<quint64> serialsOf(Source s) const {
        if (s == Source::Speakers) return editor ? editor->undoSerials() : QVector<quint64>();
        if (s == Source::Tags) return tags ? tags->undoSerials() : QVector<quint64>();
        return {};
    }
    bool known(Source s, quint64 serial) const {
        for (const Entry& e : entries) {
            if (e.source == s && e.serial == serial) return true;
            if (s == Source::Speakers && e.linkedEditor == serial) return true;
        }
        return s == Source::Speakers ? ignoredEditor.contains(serial) : ignoredTags.contains(serial);
    }
    // Merging the source's stack and the entries. true if changed.
    bool reconcile(Source s) {
        const QVector<quint64> serials = serialsOf(s);
        const QSet<quint64> present(serials.cbegin(), serials.cend());
        bool changed = false;
        for (int i = entries.size() - 1; i >= 0; --i)
            if (entries.at(i).source == s && !present.contains(entries.at(i).serial)) {
                entries.removeAt(i);
                changed = true;
            }
        QSet<quint64>& ignored = s == Source::Speakers ? ignoredEditor : ignoredTags;
        ignored.intersect(present);
        for (int i = 0; i < serials.size(); ++i) {
            const quint64 serial = serials.at(i);
            if (known(s, serial)) continue;
            const bool top = i == serials.size() - 1;
            if (s == Source::Tags && !(tags && tags->undoMeetingIds(serial).contains(meetingId))) {
                ignored.insert(serial);
                continue;
            }
            Entry e;
            e.source = s;
            e.serial = serial;
            if (top) e.text = s == Source::Speakers ? editor->undoText() : tags->undoLabel();
            entries.append(e);
            changed = true;
        }
        while (entries.size() > kMaxEntries) {
            entries.removeFirst();
            changed = true;
        }
        return changed;
    }
    void reconcileAndNotify(Source s) {
        if (reconcile(s)) emit q->changed();
    }
    // Index of the most recent undoable entry (-1: none). A tag step only if it is on top of TagService; an
    // editor step only if it is on top of the editor.
    int latestUsable() const {
        const QVector<quint64> ed = serialsOf(Source::Speakers);
        const QVector<quint64> tg = serialsOf(Source::Tags);
        for (int i = entries.size() - 1; i >= 0; --i) {
            const Entry& e = entries.at(i);
            if (e.source == Source::Tags && (tg.isEmpty() || tg.last() != e.serial)) continue;
            if (e.source == Source::Speakers && (ed.isEmpty() || ed.last() != e.serial)) continue;
            return i;
        }
        return -1;
    }
    void detachEditor() {
        for (const QMetaObject::Connection& c : std::as_const(editorConns)) QObject::disconnect(c);
        editorConns.clear();
        editor = nullptr;
        ignoredEditor.clear();
        bool changed = false;
        for (int i = entries.size() - 1; i >= 0; --i) {
            if (entries.at(i).source == Source::Speakers) {
                entries.removeAt(i);
                changed = true;
            } else if (entries.at(i).linkedEditor) {
                entries[i].linkedEditor = 0;   // its editor part can no longer be undone
            }
        }
        if (changed) emit q->changed();
    }
    void detachTags() {
        for (const QMetaObject::Connection& c : std::as_const(tagConns)) QObject::disconnect(c);
        tagConns.clear();
        tags = nullptr;
        ignoredTags.clear();
        const auto before = entries.size();
        entries.erase(std::remove_if(entries.begin(), entries.end(),
                                     [](const Entry& e) { return e.source == Source::Tags; }),
                      entries.end());
        if (entries.size() != before) emit q->changed();
    }
};

MeetingUndoService::MeetingUndoService(const QString& meetingId, QObject* parent)
    : QObject(parent), d(std::make_unique<Impl>())
{
    d->q = this;
    d->meetingId = meetingId;
}

MeetingUndoService::~MeetingUndoService()
{
    for (const QMetaObject::Connection& c : std::as_const(d->editorConns)) disconnect(c);
    for (const QMetaObject::Connection& c : std::as_const(d->tagConns)) disconnect(c);
}

QString MeetingUndoService::meetingId() const { return d->meetingId; }
SpeakerEditor* MeetingUndoService::speakerEditor() const { return d->editor; }
TagService* MeetingUndoService::tagService() const { return d->tags; }

void MeetingUndoService::attachSpeakerEditor(SpeakerEditor* editor)
{
    if (d->editor == editor) return;
    if (d->editor) d->detachEditor();
    if (!editor) return;
    d->editor = editor;
    const QVector<quint64> pre = editor->undoSerials();
    d->ignoredEditor = QSet<quint64>(pre.cbegin(), pre.cend());
    d->editorConns << connect(editor, &SpeakerEditor::undoStateChanged, this,
                              [this] { d->reconcileAndNotify(Source::Speakers); });
    d->editorConns << connect(editor, &SpeakerEditor::reloaded, this,
                              [this] { d->reconcileAndNotify(Source::Speakers); });
    d->editorConns << connect(editor, &QObject::destroyed, this, [this] { d->detachEditor(); });
}

void MeetingUndoService::attachTagService(TagService* tags)
{
    if (d->tags == tags) return;
    if (d->tags) d->detachTags();
    if (!tags) return;
    d->tags = tags;
    const QVector<quint64> pre = tags->undoSerials();
    d->ignoredTags = QSet<quint64>(pre.cbegin(), pre.cend());
    d->tagConns << connect(tags, &TagService::undoChanged, this,
                           [this] { d->reconcileAndNotify(Source::Tags); });
    d->tagConns << connect(tags, &QObject::destroyed, this, [this] { d->detachTags(); });
}

void MeetingUndoService::pushStep(const QString& text, std::function<void()> undo, quint64 absorbEditorSerial)
{
    // The editor's signal (undoStateChanged) may already have registered its step during the operation.
    d->reconcile(Source::Speakers);
    Impl::Entry e;
    e.source = Source::Participants;
    e.serial = ++d->nextOwn;
    e.text = text;
    e.undo = std::move(undo);
    if (absorbEditorSerial) {
        for (int i = d->entries.size() - 1; i >= 0; --i)
            if (d->entries.at(i).source == Source::Speakers && d->entries.at(i).serial == absorbEditorSerial)
                d->entries.removeAt(i);
        e.linkedEditor = absorbEditorSerial;
    }
    d->entries.append(e);
    while (d->entries.size() > kMaxEntries) d->entries.removeFirst();
    emit changed();
}

bool MeetingUndoService::canUndo() const { return d->latestUsable() >= 0; }

QString MeetingUndoService::undoText() const
{
    const int i = d->latestUsable();
    return i < 0 ? QString() : d->entries.at(i).text;
}

MeetingUndoService::Source MeetingUndoService::undoSource() const
{
    const int i = d->latestUsable();
    return i < 0 ? Source::Participants : d->entries.at(i).source;
}

int MeetingUndoService::count() const { return int(d->entries.size()); }

QStringList MeetingUndoService::texts() const
{
    QStringList out;
    for (const Impl::Entry& e : std::as_const(d->entries)) out << e.text;
    return out;
}

bool MeetingUndoService::undo()
{
    const int i = d->latestUsable();
    if (i < 0) return false;
    const Impl::Entry e = d->entries.takeAt(i);
    switch (e.source) {
    case Source::Speakers:
        if (d->editor) d->editor->undo();
        break;
    case Source::Tags:
        if (d->tags) d->tags->undo();
        break;
    case Source::Participants:
        if (e.linkedEditor && d->editor) {
            const QVector<quint64> ed = d->editor->undoSerials();
            if (!ed.isEmpty() && ed.last() == e.linkedEditor) d->editor->undo();
        }
        if (e.undo) e.undo();
        break;
    }
    d->reconcile(Source::Speakers);
    d->reconcile(Source::Tags);
    emit undone(e.text, e.source);
    emit changed();
    return true;
}

} // namespace tanara
