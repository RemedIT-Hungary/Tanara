#pragma once
//
// Tanara QML: the summary's source-linked statements (S1-S3) of one kind: SummaryViewModel
// owns five (sentences: the executive summary's sentences, decisionItems: the decisions,
// questionItems: the open questions, todoItems: the action items, memoItems: the memo's
// intro sentences and points). Per row: text, the time chips of the source ranges (empty =
// "no source"), for an action item the owner + due date, and targeted staleness (whether the
// statement / a source range / the owner is affected since the speaker fix).
//
// `rows` is the same as a QVariantList: the sentences wrap into one flowing paragraph (one
// Flow item per word), for which QML requests the whole at once.
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
        bool affected = false;      // the speaker of one of the source rows changed since
    };
    struct Row {
        QString id;                 // "s1", "d1", "q1", "t1", "i3.1", "m3.2" …
        QString kind;               // statement | decision | openQuestion | todo | intro | memoPoint
        QString text;
        QVector<Span> spans;
        QString owner;              // the action item's owner (raw text)
        QVariantList owners;        // [{ name, index }] (speaker color, -1: not the meeting's speaker)
        QString due;
        bool flagged = false;       // "Not how it was said?": flagged
        bool affected = false;      // targeted staleness: the speaker changed in its source
        QString staleBecause;       // "X -> Y?" (the source rows' speaker changes, joined with ", ")
        QString ownerStale;         // "X -> Y?" (the owner is affected too); empty: no
        int ownerStaleIndex = -1;   // the new (Y) speaker's color, if known
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
    // Flags a statement (in place, after the disk write or in the demo). True: there was such a one.
    bool setFlagged(const QString& id, bool flagged);

    Q_INVOKABLE int indexOfId(const QString& id) const;
    Q_INVOKABLE QVariantMap get(int row) const;

    // "00:04", "47:02", "131:04": min:sec, minutes not rolled over to hours (per the design).
    static QString stamp(qint64 ms);
    static QVariantMap spanMap(const Span& s);
    static QVariantMap rowMap(const Row& r);

signals:
    void rowsChanged();

private:
    QVector<Row> m_rows;
};

} // namespace tanara_qml
