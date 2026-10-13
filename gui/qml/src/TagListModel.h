#pragma once
//
// TagListModel: the Tags window's left-hand list (provided by TagsViewModel.tags). A row:
// id, name (the search hit in three pieces for highlighting), bottom line ("14 meetings · Oct 2").
// Filtering and sorting are done by the view model; only the finished rows live here.
//
#include <QAbstractListModel>
#include <QString>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace tanara_qml {

struct TagListRow {
    QString id;
    QString name;
    QString meta;
    QString before;     // the name before the hit (without a search the full name)
    QString match;
    QString after;

    bool operator==(const TagListRow& o) const = default;
};

class TagListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("A TagListModel-t a TagsViewModel adja (tags).")
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role { IdRole = Qt::UserRole + 1, NameRole, MetaRole, BeforeRole, MatchRole, AfterRole };

    explicit TagListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_rows.size()); }
    // If the order of ids did not change, there is no reset (the list does not jump).
    void setRows(const QVector<TagListRow>& rows);
    const QVector<TagListRow>& rows() const { return m_rows; }
    Q_INVOKABLE int indexOfId(const QString& id) const;
    Q_INVOKABLE QString idAt(int row) const;

signals:
    void countChanged();

private:
    QVector<TagListRow> m_rows;
};

} // namespace tanara_qml
