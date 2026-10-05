#pragma once
//
// TagListModel — a Címkék ablak bal oldali listája (a TagsViewModel.tags adja). Egy sor:
// id, név (a keresés találata három darabban a kiemeléshez), alsor („14 megbeszélés · okt. 2.”).
// A szűrést és a rendezést a nézetmodell végzi; itt csak a kész sorok élnek.
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
    QString before;     // a név a találat előtt (keresés nélkül a teljes név)
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
    // Ha az id-k sorrendje nem változott, nincs alaphelyzetbe állítás (a lista nem ugrik).
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
