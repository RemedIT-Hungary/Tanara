#pragma once
//
// PeopleListModel — a Személyek ablak bal oldali névsora (a PeopleViewModel.people adja).
// Egy sor: név (a keresés találata három darabban a kiemeléshez), monogram, „te” jelző,
// alsor (megbeszélés- és mintaszám), és — ha a sor új szakaszt kezd — a szakasz fejléce.
// A szűrést és a rendezést a nézetmodell végzi; itt csak a kész sorok élnek.
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
    QString meta;       // „23 megbeszélés · 9 minta”
    QString header;     // nem üres: e sor fölött szakasz-fejléc („Te”, „B” …)
    QString before;     // a név a találat előtt (keresés nélkül a teljes név)
    QString match;
    QString after;

    bool operator==(const PeopleListRow& o) const = default;
};

class PeopleListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("A PeopleListModel-t a PeopleViewModel adja (people).")
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role { NameRole = Qt::UserRole + 1, MonogramRole, IsSelfRole, HasVoiceprintRole, MetaRole,
                HeaderRole, BeforeRole, MatchRole, AfterRole };

    explicit PeopleListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_rows.size()); }
    // Új sorok. Ha a nevek sorrendje nem változott (pl. csak a statisztika töltődött be), a
    // modell nem áll alaphelyzetbe: a lista nem ugrik, csak a megváltozott sorok frissülnek.
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
