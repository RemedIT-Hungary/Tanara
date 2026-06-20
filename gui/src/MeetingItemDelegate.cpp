#include "MeetingItemDelegate.h"
#include "MeetingTableModel.h"

#include <QPainter>
#include <QApplication>
#include <QFontMetrics>
#include <QLocale>
#include <QDateTime>

namespace tanara_gui {

namespace {

// A 3 sor függőleges margói/térközei (px).
constexpr int kVPad = 6;     // felső/alsó belső margó
constexpr int kHPad = 8;     // bal belső margó
constexpr int kLineGap = 3;  // sorok közti rés

// A státusz-badge-ek szövege a role-okból: "✓ átirat · ○ össz · ● azonosítva".
QString badgeText(const QModelIndex& idx) {
    const QChar yes(QChar(0x2713)); // ✓
    const QChar no(QChar(0x25CB));  // ○
    const QChar dot(QChar(0x25CF)); // ●
    const bool tr  = idx.data(MeetingTableModel::HasTranscriptRole).toBool();
    const bool sum = idx.data(MeetingTableModel::HasSummaryRole).toBool();
    const bool spk = idx.data(MeetingTableModel::SpeakersIdentifiedRole).toBool();
    return QStringLiteral("%1 átirat   %2 össz   %3 azonosítva")
        .arg(tr ? yes : no)
        .arg(sum ? yes : no)
        .arg(spk ? dot : no);
}

// Emberi 3. sor: "2026. június 18. · 1ó 32p" (a szomszéd oszlopok nyers értékéből).
QString humanLine(const QModelIndex& nameIdx) {
    const QModelIndex timeIdx =
        nameIdx.sibling(nameIdx.row(), MeetingTableModel::ColTime);
    const QModelIndex durIdx =
        nameIdx.sibling(nameIdx.row(), MeetingTableModel::ColDuration);

    const QDateTime dt = timeIdx.data(Qt::EditRole).toDateTime();
    const qint64 ms = durIdx.data(Qt::EditRole).toLongLong();

    QString date;
    if (dt.isValid())
        date = QLocale(QLocale::Hungarian).toString(dt, QStringLiteral("yyyy. MMMM d."));

    QString dur;
    if (ms > 0) {
        const qint64 totalSec = ms / 1000;
        const qint64 hh = totalSec / 3600;
        const qint64 mm = (totalSec % 3600) / 60;
        dur = (hh > 0) ? QStringLiteral("%1ó %2p").arg(hh).arg(mm)
                       : QStringLiteral("%1p").arg(mm);
    }

    if (!date.isEmpty() && !dur.isEmpty())
        return date + QStringLiteral(" · ") + dur;
    return date.isEmpty() ? dur : date;
}

} // namespace

MeetingItemDelegate::MeetingItemDelegate(QObject* parent)
    : QStyledItemDelegate(parent) {}

void MeetingItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                const QModelIndex& index) const {
    // A háttér / kijelölés / hover a stílusra bízva (alternáló sorszín, selection).
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    opt.text.clear();   // a szöveget MI rajzoljuk (3 sor); a stílus csak a hátteret
    const QWidget* w = opt.widget;
    QStyle* style = w ? w->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, w);

    const bool selected = opt.state & QStyle::State_Selected;
    const QColor nameColor = selected
        ? opt.palette.color(QPalette::HighlightedText)
        : opt.palette.color(QPalette::Text);
    const QColor mutedColor = selected
        ? opt.palette.color(QPalette::HighlightedText)
        : opt.palette.color(QPalette::Disabled, QPalette::Text);

    painter->save();

    const QRect r = opt.rect.adjusted(kHPad, kVPad, -kHPad, -kVPad);

    QFont nameFont = opt.font;
    nameFont.setBold(true);
    QFont smallFont = opt.font;
    smallFont.setPointSizeF(qMax(6.0, opt.font.pointSizeF() - 1.0));

    const QFontMetrics nameFm(nameFont);
    const QFontMetrics smallFm(smallFont);

    int y = r.top();

    // 1. sor — név (félkövér).
    {
        painter->setFont(nameFont);
        painter->setPen(nameColor);
        const QString name = index.data(Qt::DisplayRole).toString();
        const QString elided = nameFm.elidedText(name, Qt::ElideRight, r.width());
        const QRect lineRect(r.left(), y, r.width(), nameFm.height());
        painter->drawText(lineRect, Qt::AlignLeft | Qt::AlignVCenter, elided);
        y += nameFm.height() + kLineGap;
    }

    // 2. sor — státusz-badge-ek (halvány).
    {
        painter->setFont(smallFont);
        painter->setPen(mutedColor);
        const QString badges = badgeText(index);
        const QString elided = smallFm.elidedText(badges, Qt::ElideRight, r.width());
        const QRect lineRect(r.left(), y, r.width(), smallFm.height());
        painter->drawText(lineRect, Qt::AlignLeft | Qt::AlignVCenter, elided);
        y += smallFm.height() + kLineGap;
    }

    // 3. sor — emberi dátum + hossz (halvány).
    {
        painter->setFont(smallFont);
        painter->setPen(mutedColor);
        const QString human = humanLine(index);
        const QString elided = smallFm.elidedText(human, Qt::ElideRight, r.width());
        const QRect lineRect(r.left(), y, r.width(), smallFm.height());
        painter->drawText(lineRect, Qt::AlignLeft | Qt::AlignVCenter, elided);
    }

    painter->restore();
}

QSize MeetingItemDelegate::sizeHint(const QStyleOptionViewItem& option,
                                    const QModelIndex& index) const {
    QFont nameFont = option.font;
    nameFont.setBold(true);
    QFont smallFont = option.font;
    smallFont.setPointSizeF(qMax(6.0, option.font.pointSizeF() - 1.0));

    const int h = kVPad
                + QFontMetrics(nameFont).height() + kLineGap
                + QFontMetrics(smallFont).height() + kLineGap
                + QFontMetrics(smallFont).height()
                + kVPad;

    QSize base = QStyledItemDelegate::sizeHint(option, index);
    base.setHeight(qMax(base.height(), h));
    return base;
}

} // namespace tanara_gui
