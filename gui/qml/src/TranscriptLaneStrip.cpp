#include "TranscriptLaneStrip.h"
#include "tanara/Logging.h"

#include <QPainter>

namespace tanara_qml {

TranscriptLaneStrip::TranscriptLaneStrip(QQuickItem* parent) : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
}

void TranscriptLaneStrip::paint(QPainter* p)
{
    tanara::PerfScope perfScope("TranscriptLaneStrip::paint", 20);
    const qreal w = width(), h = height();
    // A sáv 8 px magas, középen; a kiemelés túllóg rajta (ezért magasabb az elem).
    const qreal trackH = qMin<qreal>(8.0, h);
    const qreal top = (h - trackH) / 2.0;
    p->setRenderHint(QPainter::Antialiasing, true);
    p->setPen(Qt::NoPen);
    if (m_track.alpha() > 0) {
        p->setBrush(m_track);
        p->drawRoundedRect(QRectF(0, top, w, trackH), 2, 2);
    }
    p->setBrush(m_color);
    for (int i = 0; i + 1 < m_segments.size(); i += 2) {
        const qreal x = m_segments[i] * w;
        const qreal sw = qMax<qreal>(1.5, m_segments[i + 1] * w - 1.0);
        p->drawRoundedRect(QRectF(x, top, qMin(sw, w - x), trackH), 1, 1);
    }
    if (!m_marks.isEmpty()) {
        p->setPen(QPen(m_mark, 1.5));
        p->setBrush(Qt::NoBrush);
        for (int i = 0; i + 1 < m_marks.size(); i += 2) {
            const qreal x = m_marks[i] * w;
            const qreal sw = qMax<qreal>(3.0, m_marks[i + 1] * w);
            p->drawRoundedRect(QRectF(x - 1.25, top - 1.25, sw + 2.5, trackH + 2.5), 2, 2);
        }
    }
}

} // namespace tanara_qml
