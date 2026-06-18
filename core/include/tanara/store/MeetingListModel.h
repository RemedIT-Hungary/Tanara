#pragma once
//
// Tanara — meeting-lista modell (QListView és QML ListView is használja).
// A custom role-okat roleNames() exportálja, hogy QML-ből név szerint érhetők el.
//
#include "tanara/Types.h"
#include <QAbstractListModel>
#include <QVector>

namespace tanara {

class MeetingStore;

class MeetingListModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        DateRole,        // startedAt (QDateTime)
        DurationRole,    // durationMs (qint64)
        HasSummaryRole,
    };

    explicit MeetingListModel(QObject* parent = nullptr);

    // Opcionálisan store-ral: feliratkozik a store jelzéseire (frissítés/listázás).
    explicit MeetingListModel(MeetingStore* store, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Teljes csere (reset).
    void setMeetings(const QVector<Meeting>& meetings);
    const QVector<Meeting>& meetings() const { return m_meetings; }

    // A store-ral köti össze a modellt (feliratkozás + azonnali feltöltés).
    void setStore(MeetingStore* store);

public slots:
    // A store jelzéseire reagálva újratölt a store-ból (egyszerű, robusztus MVP).
    void refreshFromStore();

private:
    QVector<Meeting> m_meetings;
    MeetingStore*    m_store = nullptr;
};

} // namespace tanara
