#pragma once
//
// MeetingTableModel — GUI-oldali táblamodell a bal oldali (rendezhető) meeting-listához.
//   3 oszlop: Idő (startedAt), Hossz (durationMs → m:ss), Név (title).
//   A DisplayRole emberi-olvasható; az EditRole a RENDEZÉSHEZ típushelyes
//   nyers értéket ad (QDateTime / qlonglong / QString), így a QSortFilterProxyModel
//   setSortRole(Qt::EditRole) mellett helyesen rendez (kronológiai/numerikus/ábécé).
//
#include "tanara/Types.h"
#include <QAbstractTableModel>
#include <QVector>

namespace tanara_gui {

class MeetingTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { ColTime = 0, ColDuration = 1, ColName = 2, ColumnCount = 3 };

    // Sor-státusz role-ok (a meglévő oszlopok/rendezés érintetlenül maradnak).
    // A MainWindow ezekkel tudja a mockup „✓ átirat   ○ össz   ● azonosítva"
    // státusz-sorát a név alá renderelni — bármelyik oszlop indexéről lekérhető,
    // mert sor-szintű (oszlopfüggetlen) információ.
    enum StatusRole {
        // Kész, ember-olvasható státusz-szöveg (pl. "✓ átirat   ○ össz   ● azonosítva").
        StatusTextRole = Qt::UserRole + 1,
        HasTranscriptRole,   // bool
        HasSummaryRole,      // bool
        SpeakersIdentifiedRole, // bool — van legalább egy azonosított beszélő
    };

    explicit MeetingTableModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    void setMeetings(QVector<tanara::Meeting> meetings);

    // Forrás-sor → Meeting / id.
    tanara::Meeting meetingAt(int row) const;
    QString idAt(int row) const;

private:
    QVector<tanara::Meeting> m_meetings;
};

} // namespace tanara_gui
