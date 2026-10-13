#include "TrackActivityItem.h"

#include "TrackActivityModel.h"

#include "tanara/foundation/log/Logging.h"

#include <QPainter>

#include <algorithm>
#include <cmath>

namespace tanara_qml {

TrackActivityItem::TrackActivityItem(QQuickItem* parent) : QQuickPaintedItem(parent)
{
    setAntialiasing(false);
    connect(this, &QQuickItem::widthChanged, this, [this]() { update(); });
    connect(this, &QQuickItem::heightChanged, this, [this]() { update(); });
}

void TrackActivityItem::setModel(TrackActivityModel* model)
{
    if (model == m_model)
        return;
    if (m_modelConn)
        disconnect(m_modelConn);
    m_model = model;
    if (m_model)
        m_modelConn = connect(m_model, &TrackActivityModel::changed, this, &TrackActivityItem::invalidate);
    emit modelChanged();
    invalidate();
}

void TrackActivityItem::setTrackId(const QString& id)
{
    if (id == m_trackId)
        return;
    m_trackId = id;
    emit trackIdChanged();
    invalidate();
}

void TrackActivityItem::setDurationMs(int ms)
{
    if (ms == m_durationMs)
        return;
    m_durationMs = ms;
    emit durationMsChanged();
    invalidate();
}

void TrackActivityItem::setColor(const QColor& c)
{
    if (c == m_color) return;
    m_color = c;
    emit colorsChanged();
    update();
}

void TrackActivityItem::setQuietColor(const QColor& c)
{
    if (c == m_quietColor) return;
    m_quietColor = c;
    emit colorsChanged();
    update();
}

void TrackActivityItem::setEmptyColor(const QColor& c)
{
    if (c == m_emptyColor) return;
    m_emptyColor = c;
    emit colorsChanged();
    update();
}

bool TrackActivityItem::hasData() const
{
    return m_model && m_model->lane(m_trackId) != nullptr;
}

void TrackActivityItem::invalidate()
{
    m_cacheWidth = -1;
    const bool has = hasData();
    if (has != m_lastHasData) {
        m_lastHasData = has;
        emit hasDataChanged();
    }
    update();
}

QVector<qreal> TrackActivityItem::columns(const QVector<qint8>& frames, qint64 originMs, int frameMs,
                                          qint64 durationMs, int count)
{
    QVector<qreal> out(std::max(0, count), -1.0);
    if (count <= 0 || frameMs <= 0 || durationMs <= 0 || frames.isEmpty())
        return out;
    const qsizetype n = frames.size();
    for (int x = 0; x < count; ++x) {
        // The column's meeting-time range -> frame range (at least one frame).
        const qint64 t0 = qint64(x) * durationMs / count;
        const qint64 t1 = qint64(x + 1) * durationMs / count;
        qint64 a = (t0 - originMs) / frameMs;
        qint64 b = std::max(a + 1, (t1 - originMs + frameMs - 1) / frameMs);
        if (t1 <= originMs || a >= n) continue;
        a = std::max<qint64>(0, a);
        b = std::min<qint64>(n, b);
        int covered = 0, speech = 0;
        for (qint64 i = a; i < b; ++i) {
            const qint8 v = frames.at(i);
            if (v == TrackActivityModel::NoData) continue;
            ++covered;
            if (v == TrackActivityModel::Speech) ++speech;
        }
        if (covered > 0) out[x] = qreal(speech) / qreal(covered);
    }
    return out;
}

void TrackActivityItem::paint(QPainter* painter)
{
    tanara::PerfScope perfScope("TrackActivityItem::paint", 10);
    const qreal w = width(), h = height();
    if (w <= 0 || h <= 0)
        return;
    painter->setPen(Qt::NoPen);
    const TrackActivityModel::Lane* lane = m_model ? m_model->lane(m_trackId) : nullptr;
    const qint64 duration = m_durationMs > 0 ? m_durationMs : (m_model ? m_model->durationMs() : 0);
    if (!lane || duration <= 0) {
        painter->setBrush(m_emptyColor);
        const qreal bh = std::max(2.0, std::round(h * 0.25));
        painter->drawRect(QRectF(0, (h - bh) / 2.0, w, bh));
        return;
    }
    const int count = int(std::ceil(w));
    const int rev = m_model->revision();
    if (m_cacheWidth != count || m_cacheRevision != rev) {
        m_cols = columns(lane->frames, lane->originMs, lane->frameMs, duration, count);
        m_cacheWidth = count;
        m_cacheRevision = rev;
    }
    // Silence: a thin baseline where the track has audio.
    const qreal base = std::max(1.0, std::round(h * 0.12));
    painter->setBrush(m_quietColor);
    for (int x = 0; x < count;) {
        if (m_cols.at(x) < 0) { ++x; continue; }
        int e = x;
        while (e < count && m_cols.at(e) >= 0) ++e;
        painter->drawRect(QRectF(x, (h - base) / 2.0, e - x, base));
        x = e;
    }
    // Speech: full height, filled in proportion to the speech frames (a short word should show too).
    for (int x = 0; x < count;) {
        const qreal f = m_cols.at(x);
        if (f <= 0) { ++x; continue; }
        int e = x;
        while (e < count && std::abs(m_cols.at(e) - f) < 1e-9) ++e;
        QColor c = m_color;
        c.setAlphaF(float(m_color.alphaF() * (0.45 + 0.55 * std::min(1.0, f * 1.5))));
        painter->setBrush(c);
        painter->drawRect(QRectF(x, 0, e - x, h));
        x = e;
    }
}

} // namespace tanara_qml
