#pragma once
//
// MeetingItemDelegate — a meeting-tábla Név-oszlopának 3 soros renderelése
// (Könyvtár-otthon mockup):
//   1. sor: a meeting neve (félkövér)
//   2. sor: státusz-badge-ek (✓/○ átirat · ✓/○ össz · ●/○ azonosítva) — a
//           MeetingTableModel HasTranscript/HasSummary/SpeakersIdentified role-jaiból
//   3. sor: emberi dátum + hossz (a szomszéd oszlopok EditRole nyers értékéből)
//
// FONTOS: a delegate CSAK rajzol; a rendezés a proxy setSortRole(Qt::EditRole)-ján
// keresztül megy, azt nem érinti. A nevet maga a delegate adja (a model DisplayRole-ja
// a Név-oszlopban már nem fűz emoji-suffixet), így nincs duplázás.
//
#include <QStyledItemDelegate>

namespace tanara_gui {

class MeetingItemDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit MeetingItemDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;
};

} // namespace tanara_gui
