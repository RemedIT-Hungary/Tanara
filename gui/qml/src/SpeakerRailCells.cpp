#include "SpeakerRailCells.h"
#include "tanara/Logging.h"

#include <QPainter>
#include <QPainterPath>

namespace tanara_qml {

namespace {
constexpr qreal kInsetV = 3.0;      // above / below the block
constexpr qreal kInsetLane = 4.0;   // on the side in the speaker column
constexpr qreal kInsetGroup = 6.0;  // in the group column, on the side
constexpr qreal kRadius = 3.0;
}

SpeakerRailCells::SpeakerRailCells(QQuickItem* parent) : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    connect(this, &SpeakerRailCells::changed, this, [this] { update(); });
}

QRectF SpeakerRailCells::cellRect(int lane) const
{
    const qreal h = height();
    if (lane >= 0 && lane < m_laneCount)
        return QRectF(m_leftInset + lane * m_laneWidth + kInsetLane, kInsetV,
                      m_laneWidth - 2 * kInsetLane, h - 2 * kInsetV);
    if (lane == -1 && m_hasGroup)
        return QRectF(m_leftInset + m_laneCount * m_laneWidth + kInsetGroup, kInsetV,
                      m_groupWidth - 2 * kInsetGroup, h - 2 * kInsetV);
    return QRectF();
}

void SpeakerRailCells::paint(QPainter* p)
{
    tanara::PerfScope perfScope("SpeakerRailCells::paint", 20);
    p->setRenderHint(QPainter::Antialiasing, true);

    // 1) Drop targets in the selected row's other columns.
    if (m_selected) {
        QPen pen(m_dash, 1.0, Qt::CustomDashLine);
        pen.setDashPattern({3.0, 3.0});
        pen.setCapStyle(Qt::FlatCap);
        p->setPen(pen);
        p->setBrush(Qt::NoBrush);
        for (int lane = 0; lane < m_laneCount; ++lane) {
            if (lane == m_ownLane || lane == m_targetLane) continue;
            p->drawRoundedRect(cellRect(lane).adjusted(0.5, 0.5, -0.5, -0.5), kRadius, kRadius);
        }
    }

    // 2) The row's own block.
    const QRectF own = cellRect(m_ownLane);
    if (own.isValid()) {
        p->setOpacity(m_dimmed ? 0.3 : 1.0);
        if (m_uncertain) {
            SpeakerHatch::paintHatch(p, own, kRadius, m_soft, m_line, 3.0);
        } else {
            p->setPen(Qt::NoPen);
            p->setBrush(m_line);
            p->drawRoundedRect(own, kRadius, kRadius);
        }
        if (m_selected || m_suggested) {
            // 2 px frame at the block's edge (selection in the text color, suggestion in accent).
            p->setPen(QPen(m_selected ? m_outline : m_accent, 2.0));
            p->setBrush(Qt::NoBrush);
            p->drawRoundedRect(own, kRadius, kRadius);
        }
        p->setOpacity(1.0);
    }

    // 3) Suggested target column (a row similar to the new person).
    const QRectF suggest = m_suggestLane >= 0 && m_suggestLane != m_ownLane ? cellRect(m_suggestLane) : QRectF();
    if (suggest.isValid()) {
        p->setPen(QPen(m_accent, 2.0));
        p->setBrush(Qt::NoBrush);
        p->drawRoundedRect(suggest.adjusted(1, 1, -1, -1), kRadius, kRadius);
    }

    // 4) Drag target.
    const QRectF target = m_targetLane >= 0 ? cellRect(m_targetLane) : QRectF();
    if (target.isValid() && m_targetLane != m_ownLane) {
        if (!m_targetDashed) {
            p->setPen(Qt::NoPen);
            p->setBrush(m_targetSoft);
            p->drawRoundedRect(target, kRadius, kRadius);
        }
        QPen pen(m_targetLine, m_targetDashed ? 1.5 : 2.0, Qt::CustomDashLine);
        pen.setDashPattern(m_targetDashed ? QVector<qreal>{2.0, 2.0} : QVector<qreal>{2.0, 1.5});
        pen.setCapStyle(Qt::FlatCap);
        p->setPen(pen);
        p->setBrush(Qt::NoBrush);
        p->drawRoundedRect(target, kRadius, kRadius);
    }
}

SpeakerHatch::SpeakerHatch(QQuickItem* parent) : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    connect(this, &SpeakerHatch::changed, this, [this] { update(); });
}

void SpeakerHatch::paintHatch(QPainter* p, const QRectF& r, qreal radius, const QColor& fill,
                              const QColor& line, qreal stripe)
{
    p->save();
    QPainterPath clip;
    clip.addRoundedRect(r, radius, radius);
    p->setClipPath(clip, Qt::IntersectClip);
    p->setPen(Qt::NoPen);
    if (fill.alpha() > 0) p->fillRect(r, fill);
    // 135-degree stripes: a `stripe`-wide line, a `stripe` gap (after CSS repeating-linear-gradient).
    p->setPen(QPen(line, stripe, Qt::SolidLine, Qt::FlatCap));
    const qreal step = stripe * 2.0 * 1.41421356;
    for (qreal x = r.left() - r.height() - step; x < r.right() + step; x += step)
        p->drawLine(QPointF(x, r.bottom() + 1), QPointF(x + r.height() + 2, r.top() - 1));
    p->restore();
}

void SpeakerHatch::paint(QPainter* p)
{
    p->setRenderHint(QPainter::Antialiasing, true);
    paintHatch(p, QRectF(0, 0, width(), height()), m_radius, m_fill, m_color, m_stripe);
}

} // namespace tanara_qml
