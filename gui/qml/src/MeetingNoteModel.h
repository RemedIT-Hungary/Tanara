#pragma once
//
// Tanara QML: shared view model for editing the meeting note (Meeting::contextNote). It
// lives in two places: step 1 of the pre-transcript view (PreTranscriptViewModel.note) and
// the Summary tab (SummaryViewModel.note); its QML-side counterpart is MeetingNoteEditor.
//
//  - Delayed save, always into its OWN meeting: the field's content while typing (draft) is
//    remembered with the meeting's id; the save (commitDraft) and a selection switch write it
//    where it was written, even if another meeting became selected meanwhile.
//  - Template suggestions (suggestions): notes of earlier meetings with a similar title
//    (tanara::MeetingLibrary::noteSuggestions). On click it goes into the field
//    (applySuggestion); nothing is ever filled in on its own.
//  - Detected call (detectedCallApp): the app recognized by the watcher, info only.
//
// It works on a tanara::library::IMeetingNoteFacade the owner hands over (setController: the owner
// facade's transitional noteController() object). Without one (the demo) the owner view model fills
// the sample data (setDemoContent); saving then happens in memory only.
//
#include "tanara/library/IMeetingNoteFacade.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>


namespace tanara_qml {

class MeetingNoteModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("A PreTranscriptViewModel.note / SummaryViewModel.note adja.")

    Q_PROPERTY(QString meetingId READ meetingId NOTIFY meetingIdChanged)
    // The saved note (while typing the field uses draft; this changes only on save).
    Q_PROPERTY(QString note READ note WRITE setNote NOTIFY noteChanged)
    // "Microsoft Teams": empty if the watcher did not start the recording.
    Q_PROPERTY(QString detectedCallApp READ detectedCallApp NOTIFY changed)
    // [{ meetingId, title, dateText, preview, note, tags }]: at most 3, newest first;
    // tags: the source meeting's tag names (the card's mini chips).
    Q_PROPERTY(QVariantList suggestions READ suggestions NOTIFY suggestionsChanged)

public:
    explicit MeetingNoteModel(QObject* parent = nullptr);

    // Set by the owner view model: its facade's note object (an IMeetingNoteFacade; anything else /
    // nullptr: demo content).
    void setController(QObject* noteFacade);
    QString meetingId() const { return m_meetingId; }
    // Before a switch, saves the pending draft into the OLD meeting.
    void setMeetingId(const QString& id);

    QString note() const { return m_note; }
    void setNote(const QString& note);
    QString detectedCallApp() const { return m_detectedCallApp; }
    QVariantList suggestions() const { return m_suggestions; }
    bool hasPendingDraft() const { return m_hasDraft; }

    // The field's content while typing, TOGETHER with the meeting it was written for.
    Q_INVOKABLE void draft(const QString& text);
    // Saves the pending draft into its own meeting (delayed save, focus loss).
    Q_INVOKABLE void commitDraft();
    // Copies in a suggestion (the text of suggestions[i].note): mode "replace" | "append" (after
    // the existing, on a new line). The new note is saved immediately. It takes the text (not
    // the index), because saving the draft may recompute the list. Empty -> nothing.
    Q_INVOKABLE void applySuggestion(const QString& suggestion, const QString& mode);

    // Re-read from the store (the owner calls it, e.g. after meetingUpdated). In the demo it does
    // not touch the sample data.
    void reload();
    // Demo content (fictional data).
    void setDemoContent(const QString& note, const QString& detectedCallApp,
                        const QVariantList& suggestions);

    // The suggestion's preview: its start on one line, at most ~90 characters.
    static QString previewOf(const QString& note);
    // The fictional template suggestions of tanara_demo for the demo states (pre-transcript view, Summary tab).
    static QVariantList demoSuggestions();

signals:
    void meetingIdChanged();
    void noteChanged();
    void suggestionsChanged();
    void changed();

private:
    bool demo() const { return m_facade.isNull(); }
    void reloadSuggestions();

    QPointer<tanara::library::IMeetingNoteFacade> m_facade;
    QString m_meetingId;
    QString m_note;
    QString m_detectedCallApp;
    QVariantList m_suggestions;

    QString m_draft;
    QString m_draftMeetingId;
    bool m_hasDraft = false;
};

} // namespace tanara_qml
