#pragma once
//
// LibrarySelectionModel: the library's multi-selection and bulk tagging.
//
//  - Selection: Ctrl+click -> toggle(id), Shift+click -> rangeTo(id) (from the anchor, in the
//    list's visible order), Esc -> clear(). The first Ctrl+click also takes the open meeting
//    (currentId) into the selection, so it starts with two items.
//  - The right panel's data: the selected meetings (title, tags, date) and the common tag
//    rows (on all / only some, "3/3" / "1/3").
//  - Bulk tagging: addTagToAll / removeTagFromAll. With a controller through the TagService
//    (beginGroup -> bulkAdd / bulkRemove -> endGroup), so it is undoable in ONE step; without
//    a controller (demo) it modifies the sample library's in-memory tags.
//
// For the order and the rows' data it uses LibraryListModel (`library`).
//
#include "LibraryListModel.h"

#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace tanara {
namespace app { class AppController; }
}

namespace tanara_qml {

class LibrarySelectionModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(LibraryListModel* library READ library WRITE setLibrary NOTIFY libraryChanged)
    // The open meeting (the first Ctrl+click selects it along with the clicked one).
    Q_PROPERTY(QString currentId READ currentId WRITE setCurrentId NOTIFY currentIdChanged)
    Q_PROPERTY(QStringList ids READ ids NOTIFY selectionChanged)
    Q_PROPERTY(int count READ count NOTIFY selectionChanged)
    // The selected meetings in list order: [{ id, title, tagsText, dateText }].
    Q_PROPERTY(QVariantList items READ items NOTIFY contentChanged)
    // The common tags: [{ id, name, onCount, total, full, countText, note }]
    //   full: on all of them; countText: "3/3" / "1/3"; note: the explanation next to the chip.
    Q_PROPERTY(QVariantList tagRows READ tagRows NOTIFY contentChanged)
    // The tags on all of them (the input field no longer offers these).
    Q_PROPERTY(QStringList fullTagIds READ fullTagIds NOTIFY contentChanged)

public:
    explicit LibrarySelectionModel(QObject* parent = nullptr);

    // The controller defaults to the App singleton's; injectable in tests.
    void setController(tanara::app::AppController* controller);

    LibraryListModel* library() const { return m_library; }
    void setLibrary(LibraryListModel* library);
    QString currentId() const { return m_currentId; }
    void setCurrentId(const QString& id);
    QStringList ids() const { return m_ids; }
    int count() const { return int(m_ids.size()); }
    QVariantList items() const { return m_items; }
    QVariantList tagRows() const { return m_tagRows; }
    QStringList fullTagIds() const;

    Q_INVOKABLE bool contains(const QString& id) const { return m_ids.contains(id); }
    Q_INVOKABLE void toggle(const QString& id);
    Q_INVOKABLE void rangeTo(const QString& id);
    Q_INVOKABLE void clear();
    // Selection from outside (demo state, test).
    Q_INVOKABLE void selectIds(const QStringList& ids);
    // Name or id; for a name, creates it if needed. One undoable step.
    Q_INVOKABLE void addTagToAll(const QString& nameOrId);
    Q_INVOKABLE void removeTagFromAll(const QString& tagId);

signals:
    void libraryChanged();
    void currentIdChanged();
    void selectionChanged();
    void contentChanged();

private:
    void setIds(const QStringList& ids);
    void rebuild();
    QStringList orderedIds() const;    // in list order (what is not visible, last)

    QPointer<LibraryListModel> m_library;
    QPointer<tanara::app::AppController> m_controller;
    bool m_controllerInjected = false;
    QString m_currentId;
    QString m_anchor;
    QStringList m_ids;
    QVariantList m_items;
    QVariantList m_tagRows;
};

} // namespace tanara_qml
