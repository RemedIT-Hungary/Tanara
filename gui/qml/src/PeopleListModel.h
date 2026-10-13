#pragma once
//
// PeopleListModel: the People window's left-hand name list (provided by PeopleViewModel.people).
// A row: name (the search hit in three pieces for highlighting), monogram, "you" marker,
// bottom line (meeting and sample count), and, if the row starts a new section, the section header.
// Filtering and sorting are done by the view model; only the finished rows live here.
//
#include <QAbstractListModel>
#include <QString>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace tanara_qml {

struct PeopleListRow {
    QString name;
    QString monogram;
    bool    isSelf = false;
    bool    hasVoiceprint = false;
    QString meta;       // "23 meetings · 9 samples"
    QString header;     // non-empty: a section header above this row ("You", "B" ...)
    QString before;     // the name before the hit (without a search the full name)
    QString match;
    QString after;
    bool    tagHit = false; // the search matched the person's tag (the meta: "tag: #Nordvik ...")

    bool operator==(const PeopleListRow& o) const = default;
};

class PeopleListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("A PeopleListModel-t a PeopleViewModel adja (people).")
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role { NameRole = Qt::UserRole + 1, MonogramRole, IsSelfRole, HasVoiceprintRole, MetaRole,
                HeaderRole, BeforeRole, MatchRole, AfterRole, TagHitRole };

    explicit PeopleListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_rows.size()); }
    // New rows. If the order of names did not change (e.g. only the statistics loaded), the
    // model does not reset: the list does not jump, only the changed rows refresh.
    void setRows(const QVector<PeopleListRow>& rows);
    const QVector<PeopleListRow>& rows() const { return m_rows; }
    Q_INVOKABLE int indexOfName(const QString& name) const;
    Q_INVOKABLE QString nameAt(int row) const;

signals:
    void countChanged();

private:
    QVector<PeopleListRow> m_rows;
};

} // namespace tanara_qml
