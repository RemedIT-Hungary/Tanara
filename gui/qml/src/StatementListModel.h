#pragma once
//
// Tanara QML — a vezetői összefoglaló forrás-hivatkozásos állításai (S1/S3) egy fajtából:
// a SummaryViewModel háromat birtokol (sentences: a vezetői összefoglaló mondatai,
// decisionItems: a döntések, todoItems: a teendők). Soronként: szöveg, a forrás-tartományok
// idő-chipjei (üres = „nincs forrás”), teendőnél felelős + határidő, és a célzott elavulás
// (az állítás / egy forrás-tartomány / a felelős érintett-e a beszélő-javítás óta).
//
// A `rows` ugyanez QVariantList-ként: a mondatok egy folyó bekezdésbe tördelődnek (szavanként
// egy Flow-ban), ahhoz a QML egyben kéri az egészet.
//
#include <QAbstractListModel>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace tanara_qml {

class StatementListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("A StatementListModel-t a SummaryViewModel adja.")

    Q_PROPERTY(int count READ count NOTIFY rowsChanged)
    Q_PROPERTY(QVariantList rows READ rows NOTIFY rowsChanged)

public:
    struct Span {
        qint64 startMs = -1;
        qint64 endMs = -1;
        QStringList utteranceIds;
        bool affected = false;      // a forrás-sorok egyikének beszélője változott azóta
    };
    struct Row {
        QString id;                 // "s1", "d1", "t1" …
        QString kind;               // statement | decision | todo
        QString text;
        QVector<Span> spans;
        QString owner;              // teendő felelőse (nyers szöveg)
        QVariantList owners;        // [{ name, index }] (beszélő-szín, -1: nem a meeting beszélője)
        QString due;
        bool flagged = false;       // „Nem így hangzott el?” — jelezve
        bool affected = false;      // célzott elavulás: a forrásában más lett a beszélő
        QString staleBecause;       // „X → Y?” (a forrás-sorok beszélő-váltásai, „, ”-vel)
        QString ownerStale;         // „X → Y?” (a felelős is érintett) — üres: nem
        int ownerStaleIndex = -1;   // az új (Y) beszélő színe, ha ismert
    };

    enum Role {
        StatementIdRole = Qt::UserRole + 1,
        KindRole,
        TextRole,
        SpansRole,            // [{ startMs, endMs, stamp, utteranceIds, affected }]
        HasSourcesRole,
        OwnerRole,
        OwnersRole,
        DueRole,
        FlaggedRole,
        AffectedRole,
        StaleBecauseRole,
        OwnerStaleRole,
        OwnerStaleIndexRole,
    };

    explicit StatementListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_rows.size()); }
    QVariantList rows() const;

    void setRows(const QVector<Row>& rows);
    void clear() { setRows({}); }
    const QVector<Row>& items() const { return m_rows; }
    // Egy állítás jelzése (helyben, a lemez-írás után vagy demóban). true: volt ilyen.
    bool setFlagged(const QString& id, bool flagged);

    Q_INVOKABLE int indexOfId(const QString& id) const;
    Q_INVOKABLE QVariantMap get(int row) const;

    // „00:04”, „47:02”, „131:04”: perc:mp, a perc nincs órára váltva (a design szerint).
    static QString stamp(qint64 ms);
    static QVariantMap spanMap(const Span& s);
    static QVariantMap rowMap(const Row& r);

signals:
    void rowsChanged();

private:
    QVector<Row> m_rows;
};

} // namespace tanara_qml
