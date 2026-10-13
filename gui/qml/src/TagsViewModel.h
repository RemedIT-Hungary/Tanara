#pragma once
//
// TagsViewModel: view model of the Tags window (TagsWindow.qml), modeled on PeopleViewModel.
//
// Left side: `tags` (TagListModel): search in names (accent- and case-insensitive, hit
// highlighted), sorting (recently used / alphabetical / most frequent). Right side: the
// selected tag's (`selectedId`) learned profile (`detail`): participants, terms,
// co-occurring tags, meetings. Actions: rename, merge, delete; each an undoable step in the
// backend; afterwards `toast(text, undoable)`.
//
// People (successor of "Typical participants"): the tag's manual wearers + the learned
// relation (>= threshold), by count; the row has "their tag" (x: remove) or "+ Add" / x
// (reject). The own person and a rejected learned row are left out.
//
// Without a controller the fictional 12 tags; `demoState`: T10 | T11 | T12 | T13 | rename |
// people | peopleEmpty | peopleAdd.
//
#include "TagListModel.h"

#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include "TagBackend.h"

namespace tanara_qml {

class TagsViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QObject* controller READ controller WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(QString demoState READ demoState WRITE setDemoState NOTIFY demoStateChanged)

    Q_PROPERTY(tanara_qml::TagListModel* tags READ tags CONSTANT)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(bool searching READ searching NOTIFY filterChanged)
    // "recent" | "alpha" | "count"
    Q_PROPERTY(QString sort READ sort WRITE setSort NOTIFY sortChanged)
    Q_PROPERTY(QString sortLabel READ sortLabel NOTIFY sortChanged)
    Q_PROPERTY(int count READ count NOTIFY listChanged)            // visible rows
    Q_PROPERTY(int totalCount READ totalCount NOTIFY listChanged)  // the whole pool
    Q_PROPERTY(QString countText READ countText NOTIFY listChanged)

    Q_PROPERTY(QString selectedId READ selectedId WRITE setSelectedId NOTIFY selectionChanged)
    Q_PROPERTY(int selectedRow READ selectedRow NOTIFY selectionChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    // { id, name, meta, meetingCount, participants: [{ name, monogram, countText }], terms: [..],
    //   cooccurring: [{ id, name, countText }], meetings: [{ meetingId, title, dateText,
    //   durationText }], meetingsMeta, people: [{ name, monogram, countText, manual }],
    //   peopleCount, peopleBusy, emptyPeopleText }
    Q_PROPERTY(QVariantMap detail READ detail NOTIFY detailChanged)

    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged)
    Q_PROPERTY(QString undoLabel READ undoLabel NOTIFY undoChanged)
    Q_PROPERTY(QString toastText READ toastText NOTIFY toast)

public:
    explicit TagsViewModel(QObject* parent = nullptr);

    QObject* controller() const { return m_controller; }
    void setController(QObject* controller);
    void setBackend(TagBackend* backend);           // for tests; not taken over
    TagBackend* backend() const { return m_backend; }
    QString demoState() const { return m_demoState; }
    void setDemoState(const QString& state);

    TagListModel* tags() { return &m_list; }
    QString filter() const { return m_filter; }
    void setFilter(const QString& filter);
    bool searching() const { return !m_filter.trimmed().isEmpty(); }
    QString sort() const { return m_sort; }
    void setSort(const QString& sort);
    QString sortLabel() const;
    int count() const { return m_list.count(); }
    int totalCount() const { return int(m_all.size()); }
    QString countText() const;

    QString selectedId() const { return m_selected; }
    void setSelectedId(const QString& id);
    int selectedRow() const { return m_list.indexOfId(m_selected); }
    bool hasSelection() const { return !m_selected.isEmpty(); }
    QVariantMap detail() const { return m_detail; }

    bool canUndo() const { return m_backend && m_backend->canUndo(); }
    QString undoLabel() const { return m_backend ? m_backend->undoLabel() : QString(); }
    QString toastText() const { return m_toastText; }

    Q_INVOKABLE QString tagName(const QString& id) const;
    // Error message, or empty if it succeeded.
    Q_INVOKABLE QString rename(const QString& id, const QString& name);
    // Candidates: similar names first, then frequent co-occurrers: [{ id, name, meta }].
    Q_INVOKABLE QVariantList mergeCandidates(const QString& id) const;
    // The result row (StyledText): "Result: <b>16 meetings</b> (1 shared). ..."
    Q_INVOKABLE QString mergeResultText(const QString& fromId, const QString& keepId) const;
    Q_INVOKABLE QString merge(const QString& fromId, const QString& keepId);
    Q_INVOKABLE QString deleteTitle(const QString& id) const;
    Q_INVOKABLE QString deleteText(const QString& id) const;
    Q_INVOKABLE void remove(const QString& id);
    // Person tag with the selected tag (each an undoable step, then a toast).
    Q_INVOKABLE void addPerson(const QString& name);
    Q_INVOKABLE void removePerson(const QString& name);
    Q_INVOKABLE void rejectPerson(const QString& name);
    Q_INVOKABLE void undo();

signals:
    void controllerChanged();
    void demoStateChanged();
    void filterChanged();
    void sortChanged();
    void listChanged();
    void selectionChanged();
    void detailChanged();
    void undoChanged();
    void toast(const QString& text, bool undoable);

private:
    void attach(TagBackend* backend);
    void rebuild();
    void refreshDetail();
    void step(const QString& label);
    const TagItem* find(const QString& id) const;

    QPointer<QObject> m_controller;
    TagBackend* m_backend = nullptr;
    TagBackend* m_ownBackend = nullptr;
    QList<QMetaObject::Connection> m_conns;
    QString m_demoState;

    TagListModel m_list;
    QVector<TagItem> m_all;
    QString m_filter;
    QString m_sort = QStringLiteral("recent");
    QString m_selected;
    QVariantMap m_detail;
    QString m_toastText;
};

// The Hungarian definite article before a name ("a #Nordvik", "az #Ugyfeltamogatas"), for the Hungarian texts.
QString tagArticle(const QString& name);

} // namespace tanara_qml
