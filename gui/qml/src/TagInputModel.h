#pragma once
//
// TagInputModel: the popup list of the tag input field (TagInput.qml).
//
// text -> rows: on an empty field the recently used tags, when typing the hits
// (accent- and case-insensitive, name start first, then word start, then anywhere) +
// "New tag: "..."", for a very similar name "SIMILAR ALREADY EXISTS" + "Create new anyway:
// "..."" (rules: TagMatching.h). The selection moves with the arrows; Enter =
// chooseSelected() -> chosen(name, isNew).
//
// Data source: TagBackend, from UiServices::tagBackend (the real one, or the fictional 12 tags). A
// view model that has its own data source (the recorder's) gives it through the `backend` property;
// in tests a fake backend can be given with setBackend().
//
#include "TagBackend.h"
#include "TagMatching.h"
#include "UiServices.h"

#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace tanara_qml {

class TagInputModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    // The data source handed in by the host (null: the services' own).
    Q_PROPERTY(QObject* backend READ backendObject WRITE setBackendObject NOTIFY backendChanged)
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged)
    // [{ kind, id, name, count, matchStart, matchLen, before, match, after }]
    //   kind: "recent" | "match" | "new" | "nearDuplicate" | "forceNew"
    Q_PROPERTY(QVariantList rows READ rows NOTIFY rowsChanged)
    // "recent" (empty field) | "typing" | "similar" (a very similar name exists): for the header
    Q_PROPERTY(QString mode READ mode NOTIFY rowsChanged)
    Q_PROPERTY(int selectedRow READ selectedRow WRITE setSelectedRow NOTIFY selectedRowChanged)
    // At most this many existing tags (5; in the recorder's compact list 3).
    Q_PROPERTY(int limit READ limit WRITE setLimit NOTIFY limitChanged)
    // The tags already on the meeting (not offered).
    Q_PROPERTY(QStringList excludeIds READ excludeIds WRITE setExcludeIds NOTIFY excludeIdsChanged)

public:
    // QML: the services of the moment (follows App.controller / App.demo).
    explicit TagInputModel(QObject* parent = nullptr);
    // The tag backend of the given services.
    explicit TagInputModel(const UiServices& services, QObject* parent = nullptr);

    QObject* backendObject() const { return m_backend; }
    void setBackendObject(QObject* backend) { setBackend(qobject_cast<TagBackend*>(backend)); }
    // For tests / a shared backend: does not take ownership.
    void setBackend(TagBackend* backend);
    TagBackend* backend() const { return m_backend; }

    QString text() const { return m_text; }
    void setText(const QString& text);
    QVariantList rows() const { return m_rowsVariant; }
    QString mode() const;
    int selectedRow() const { return m_selected; }
    void setSelectedRow(int row);
    int limit() const { return m_limit; }
    void setLimit(int limit);
    QStringList excludeIds() const { return m_exclude; }
    void setExcludeIds(const QStringList& ids);

    const QVector<tagmatch::InputRow>& rowData() const { return m_rows; }

    // Moves the selection (does not wrap).
    Q_INVOKABLE void move(int delta);
    // Chooses the selected row: chosen(name, isNew). False if there is nothing to choose.
    Q_INVOKABLE bool chooseSelected();
    Q_INVOKABLE bool choose(int row);

signals:
    void backendChanged();
    void textChanged();
    void rowsChanged();
    void selectedRowChanged();
    void limitChanged();
    void excludeIdsChanged();
    void chosen(const QString& name, bool isNew);

private:
    void attach(TagBackend* backend);
    void resolveBackend();
    void rebuild();

    bool m_explicitServices = false;
    TagBackend* m_backend = nullptr;
    TagBackend* m_ownBackend = nullptr;
    QMetaObject::Connection m_conn;
    QString m_text;
    QVector<tagmatch::InputRow> m_rows;
    QVariantList m_rowsVariant;
    int m_selected = -1;
    int m_limit = 5;
    QStringList m_exclude;
};

} // namespace tanara_qml
