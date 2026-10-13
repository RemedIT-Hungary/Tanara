#pragma once
//
// LibraryListModel: list model of the library sidebar over the core MeetingLibrary.
//
//  - Rows: meetings newest first; date section, title, meta ("Oct 2 · 30 min"), the key of the
//    three state icons (done | running | error | stale | missing) + tooltip, for a search
//    the title and transcript hit in three pieces (before / hit / after).
//  - Query: searchText (runs delayed), noTranscript, noSummary, people, tags (tags = ids,
//    tagsAll = AND / otherwise OR, untagged = those without a tag).
//  - Tags on the row: the third line's text ("#Nordvik  #Partners  +1"), for a search the
//    tag hit ("tag: #Nordvik"); the filter popover's list (tagOptions, untaggedCount).
//  - Refresh: on the MeetingLibrary's signals INCREMENTALLY (one row dataChanged / insert /
//    remove); a full reset only if the hit list's structure changed.
//  - Without a controller (App.demo) it works from the fictional sample library (LibraryDemoData).
//
#include "tanara/library/MeetingLibrary.h"

#include <QAbstractListModel>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace tanara {
namespace app { class AppController; }
}

namespace tanara_qml {

class LibraryListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY queryChanged)
    Q_PROPERTY(bool noTranscript READ noTranscript WRITE setNoTranscript NOTIFY queryChanged)
    Q_PROPERTY(bool noSummary READ noSummary WRITE setNoSummary NOTIFY queryChanged)
    Q_PROPERTY(QStringList people READ people WRITE setPeople NOTIFY queryChanged)
    // Whether any filtering is on (search or chip): then there are no date sections, and a hit count shows.
    Q_PROPERTY(bool filtered READ filtered NOTIFY queryChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    // The library's total size without filtering (0 -> empty library).
    Q_PROPERTY(int totalCount READ totalCount NOTIFY countChanged)
    Q_PROPERTY(QString resultText READ resultText NOTIFY resultChanged)
    Q_PROPERTY(QStringList tags READ tags WRITE setTags NOTIFY queryChanged)
    Q_PROPERTY(bool tagsAll READ tagsAll WRITE setTagsAll NOTIFY queryChanged)
    Q_PROPERTY(bool untagged READ untagged WRITE setUntagged NOTIFY queryChanged)
    // The selected tag filters' chips: [{ id, name }] (in tags order).
    Q_PROPERTY(QVariantList tagFilterItems READ tagFilterItems NOTIFY tagFilterItemsChanged)
    // The filter popover's tags: [{ id, name, count }], by frequency (alphabetical on a tie).
    Q_PROPERTY(QVariantList tagOptions READ tagOptions NOTIFY tagOptionsChanged)
    Q_PROPERTY(int untaggedCount READ untaggedCount NOTIFY tagOptionsChanged)
    // The "+ Filter" menu's people: [{ name, count, colorIndex }], by frequency.
    Q_PROPERTY(QVariantList peopleOptions READ peopleOptions NOTIFY peopleOptionsChanged)
    // Demo / screenshot only: forces the empty library.
    Q_PROPERTY(bool forceEmpty READ forceEmpty WRITE setForceEmpty NOTIFY forceEmptyChanged)

public:
    enum Role {
        MeetingIdRole = Qt::UserRole + 1,
        TitleRole,
        MetaRole,
        SectionRole,            // section title ("Today"...); empty in a filtered list
        TranscriptStateRole,
        SummaryStateRole,
        IdentifyStateRole,
        TranscriptTipRole,
        SummaryTipRole,
        IdentifyTipRole,
        TitleBeforeRole,        // the title in three pieces (without a hit: only "after")
        TitleMatchRole,
        TitleAfterRole,
        SnippetBeforeRole,
        SnippetMatchRole,
        SnippetAfterRole,
        SnippetMsRole,
        HasSnippetRole,
        TagIdsRole,             // QStringList, in the meeting's order
        TagNamesRole,           // QStringList, in the same order
        TagLineRole,            // the third line's pieces: ["#Nordvik", "#Partners", "+1"]
        TagMatchRole,           // the search matched in a tag's name
        TagMatchNameRole,       // the matching tag's name
    };

    explicit LibraryListModel(QObject* parent = nullptr);

    // The controller defaults to the App singleton's; injectable in tests.
    void setController(tanara::app::AppController* controller);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString searchText() const { return m_query.text; }
    void setSearchText(const QString& text);
    bool noTranscript() const { return m_query.noTranscript; }
    void setNoTranscript(bool on);
    bool noSummary() const { return m_query.noSummary; }
    void setNoSummary(bool on);
    QStringList people() const { return m_query.people; }
    void setPeople(const QStringList& people);
    bool filtered() const { return !m_query.isEmpty(); }
    int count() const { return int(m_entries.size()); }
    int totalCount() const { return m_total; }
    QStringList tags() const { return m_query.tags; }
    void setTags(const QStringList& tagIds);
    bool tagsAll() const { return m_query.tagsAll; }
    void setTagsAll(bool all);
    bool untagged() const { return m_query.untagged; }
    void setUntagged(bool on);
    QVariantList tagFilterItems() const;
    QVariantList tagOptions() const;
    int untaggedCount() const;
    QString resultText() const;
    QVariantList peopleOptions() const;

    // A meeting's entry regardless of filtering (the bulk selection's panel works from this):
    // with a controller from the MeetingLibrary, in the demo from the sample library.
    tanara::library::LibraryEntry entryFor(const QString& meetingId) const;
    // The row's third line: as many "#Name" as fit in ~24 characters, then "+N".
    static QStringList tagLine(const QStringList& names);
    bool forceEmpty() const { return m_forceEmpty; }
    void setForceEmpty(bool on);

    Q_INVOKABLE int indexOfMeeting(const QString& meetingId) const;
    Q_INVOKABLE QString meetingIdAt(int row) const;
    Q_INVOKABLE QString titleOf(const QString& meetingId) const;
    Q_INVOKABLE void addPerson(const QString& name);
    Q_INVOKABLE void removePerson(const QString& name);
    Q_INVOKABLE void clearFilters();
    Q_INVOKABLE void addTag(const QString& tagId);
    Q_INVOKABLE void removeTag(const QString& tagId);
    Q_INVOKABLE void toggleTag(const QString& tagId);
    Q_INVOKABLE QString tagName(const QString& tagId) const;
    // The popover's tags by the search box (accent- and case-insensitive substring).
    Q_INVOKABLE QVariantList tagOptionsMatching(const QString& text) const;
    // The person's constant color index for the chip (per the peopleOptions order).
    Q_INVOKABLE int personColorIndex(const QString& name) const;
    // Preloads the transcript texts for searching (when the search box gets focus).
    Q_INVOKABLE void warmUp();
    // Runs the pending (delayed) query immediately.
    Q_INVOKABLE void refreshNow();

signals:
    void queryChanged();
    void countChanged();
    void resultChanged();          // a query ran (the hit text may have changed)
    void peopleOptionsChanged();
    void tagOptionsChanged();
    void tagFilterItemsChanged();
    void forceEmptyChanged();

private:
    void attach();
    void scheduleRefresh(int delayMs = 0);
    void onMeetingChanged(const QString& meetingId);
    void apply(const tanara::library::LibraryResult& result);
    tanara::library::LibraryResult runQuery() const;
    tanara::library::MeetingLibrary* library() const;

    QPointer<tanara::app::AppController> m_controller;
    QPointer<tanara::library::MeetingLibrary> m_library;     // the one we are currently bound to
    bool m_controllerInjected = false;
    tanara::library::LibraryQuery m_query;
    QVector<tanara::library::LibraryEntry> m_entries;
    int m_total = 0;
    bool m_forceEmpty = false;
    QTimer m_refreshTimer;
    QTimer m_dayTimer;      // the date sections (today / yesterday) should refresh on a day change too
};

} // namespace tanara_qml
