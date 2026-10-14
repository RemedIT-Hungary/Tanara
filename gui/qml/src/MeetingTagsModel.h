#pragma once
//
// MeetingTagsModel: a meeting's tag row (TagRow.qml) and "Why?" panel (TagWhyPopover.qml).
//
// tags: the attached tags; suggestions: the not-yet-accepted, not-rejected suggestions
// (filtered from the backend's latest result: what is already on it or rejected drops out, so
// after an undo it comes back by itself). Accept / reject / "All" / remove are one undoable
// step each; the step's name (undoLabel) is also the toast text.
//
// After a manual add, it asks the backend for a co-occurring suggestion ("often with <tag>").
//
// Title-based fallback (draftTitle): if the host gives the meeting's title (the Overview
// before a transcript) and the backend has no regular suggestion (and is not computing), it
// suggests from the title (TagBackend::draftSuggestions; without attached and rejected
// ones, at most 3), suggestionSource = "title", label "Based on the title". Accept / reject work the same.
//
// Without a controller the fictional pool; `demoState` is the T01 rows:
// none | few | many | computing | similar | cooccur | llm, and why (the T02 panel's data).
//
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantList>
#include <QVector>
#include <QtQml/qqmlregistration.h>

#include "TagBackend.h"
#include "UiServices.h"

namespace tanara_qml {

class MeetingTagsModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString meetingId READ meetingId WRITE setMeetingId NOTIFY meetingIdChanged)
    Q_PROPERTY(QString demoState READ demoState WRITE setDemoState NOTIFY demoStateChanged)
    // The meeting's title for the title-based fallback suggestion ("" = no fallback).
    Q_PROPERTY(QString draftTitle READ draftTitle WRITE setDraftTitle NOTIFY draftTitleChanged)
    // [{ id, name }] in the meeting's order
    Q_PROPERTY(QVariantList tags READ tags NOTIFY tagsChanged)
    Q_PROPERTY(QStringList tagIds READ tagIds NOTIFY tagsChanged)
    // [{ id, name, isNew, source }]
    Q_PROPERTY(QVariantList suggestions READ suggestions NOTIFY suggestionsChanged)
    // "Suggested" | "Often with <tag>" | "Based on the summary" | "Based on the title"
    // ("" if no suggestion)
    Q_PROPERTY(QString suggestionLabel READ suggestionLabel NOTIFY suggestionsChanged)
    // "similar" | "cooccur" | "llm" | "title" (title-based fallback) | ""
    Q_PROPERTY(QString suggestionSource READ suggestionSource NOTIFY suggestionsChanged)
    Q_PROPERTY(bool computing READ computing NOTIFY suggestionsChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged)
    Q_PROPERTY(QString undoLabel READ undoLabel NOTIFY undoChanged)

public:
    // QML: the services of the moment (follows App.controller / App.demo).
    explicit MeetingTagsModel(QObject* parent = nullptr);
    // The tag backend of the given services.
    explicit MeetingTagsModel(const UiServices& services, QObject* parent = nullptr);
    // Transitional: ShellMeetingModel (P15-2) and PreTranscriptViewModel (P15-3) still hand the
    // controller over; the services of that controller (null: the fictional ones) are used from then on.
    void setController(QObject* controller);

    void setBackend(TagBackend* backend);           // for tests; not taken over
    TagBackend* backend() const { return m_backend; }

    QString meetingId() const { return m_meetingId; }
    void setMeetingId(const QString& id);
    QString demoState() const { return m_demoState; }
    void setDemoState(const QString& state);
    QString draftTitle() const { return m_draftTitle; }
    void setDraftTitle(const QString& title);

    QVariantList tags() const;
    QStringList tagIds() const { return m_tagIds; }
    QVariantList suggestions() const;
    QString suggestionLabel() const;
    QString suggestionSource() const { return m_visible.isEmpty() ? QString() : m_source; }
    bool computing() const { return m_computing; }
    bool canUndo() const { return m_backend && m_backend->canUndo(); }
    QString undoLabel() const { return m_backend ? m_backend->undoLabel() : QString(); }

    const QVector<TagSuggestionItem>& visibleSuggestions() const { return m_visible; }
    // The meeting id actually used (in the demo the demo meeting instead of empty).
    QString effectiveMeetingId() const;

    // Adds a name (or id) manually; returns the id.
    Q_INVOKABLE QString add(const QString& nameOrId);
    Q_INVOKABLE void remove(const QString& tagId);
    Q_INVOKABLE void accept(int index);
    Q_INVOKABLE void reject(int index);
    Q_INVOKABLE void acceptAll();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void requestSuggestions();
    // The "Why?" panel: [{ index, id, name, isNew, source,
    //   reasons: [{ kind, values, label, text }], similarMeetings: [{ meetingId, title, dateText }] }]
    Q_INVOKABLE QVariantList whyData() const;
    Q_INVOKABLE QString tagName(const QString& tagId) const;

signals:
    void meetingIdChanged();
    void demoStateChanged();
    void draftTitleChanged();
    void tagsChanged();
    void suggestionsChanged();
    void undoChanged();
    // Notice after the step (the shell shows it with "Undo").
    void toast(const QString& text, bool undoable);

private:
    void attach(TagBackend* backend);
    void resolveBackend();
    void applyDemoState();
    void reloadTags();
    void reloadSuggestions();
    bool applied(const TagSuggestionItem& s) const;
    void finishStep();

    bool m_explicitServices = false;
    QPointer<QObject> m_handedController;   // setController (transitional)
    TagBackend* m_backend = nullptr;
    TagBackend* m_ownBackend = nullptr;
    QList<QMetaObject::Connection> m_conns;
    QString m_meetingId;
    QString m_demoState;
    QString m_draftTitle;

    QStringList m_tagIds;
    QStringList m_tagNames;
    QVector<TagSuggestionItem> m_visible;
    QString m_source;
    QString m_baseTagId;
    bool m_computing = false;
};

} // namespace tanara_qml
