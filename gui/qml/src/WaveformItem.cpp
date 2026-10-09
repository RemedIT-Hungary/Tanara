#include "WaveformItem.h"
#include "tanara/Logging.h"

#include <QPainter>

#include <algorithm>
#include <cmath>

namespace tanara_qml {

namespace {
constexpr qreal kLevelGamma = 1.6;
}

WaveformItem::WaveformItem(QQuickItem* parent) : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    connect(this, &QQuickItem::widthChanged, this, [this]() { update(); });
    connect(this, &QQuickItem::heightChanged, this, [this]() { update(); });
}

void WaveformItem::setPeaks(const QList<qreal>& peaks)
{
    if (peaks == m_peaks) return;
    m_peaks = peaks;
    emit peaksChanged();
    update();
}

void WaveformItem::setColor(const QColor& color)
{
    if (color == m_color) return;
    m_color = color;
    emit colorChanged();
    update();
}

void WaveformItem::setReference(qreal reference)
{
    if (qFuzzyCompare(reference + 1.0, m_reference + 1.0)) return;
    m_reference = reference;
    emit referenceChanged();
    update();
}

void WaveformItem::setFlat(bool flat)
{
    if (flat == m_flat) return;
    m_flat = flat;
    emit flatChanged();
    update();
}

void WaveformItem::setLoading(bool loading)
{
    if (loading == m_loading) return;
    m_loading = loading;
    emit loadingChanged();
    update();
}

void WaveformItem::setBarWidth(qreal w)
{
    if (w <= 0 || qFuzzyCompare(w, m_barWidth)) return;
    m_barWidth = w;
    emit barWidthChanged();
    update();
}

void WaveformItem::setBarGap(qreal g)
{
    if (g < 0 || qFuzzyCompare(g + 1.0, m_barGap + 1.0)) return;
    m_barGap = g;
    emit barWidthChanged();
    update();
}

qreal WaveformItem::referenceLevel(const QList<qreal>& levels)
{
    if (levels.isEmpty())
        return 0.0;
    QList<qreal> sorted = levels;
    const qsizetype k = std::min<qsizetype>(sorted.size() - 1, qsizetype(double(sorted.size()) * 0.97));
    std::nth_element(sorted.begin(), sorted.begin() + k, sorted.end());
    return sorted.at(k);
}

QList<qreal> WaveformItem::barLevels(const QList<qreal>& peaks, int bars, qreal reference)
{
    QList<qreal> out;
    if (bars <= 0)
        return out;
    out.reserve(bars);
    if (peaks.isEmpty()) {
        out.fill(0.0, bars);
        return out;
    }
    qreal ref = reference;
    if (ref <= 0.0)
        ref = referenceLevel(peaks);
    if (ref <= 0.0)
        ref = 1.0;
    const qsizetype n = peaks.size();
    for (int i = 0; i < bars; ++i) {
        // Az oszlopra eső vödrök ÁTLAGA (nagyításnál ismétel). A max-tartás hosszú felvételnél
        // mindent egyformán magasra húzna: egy oszlop 10–20 másodpercet fed le.
        const qsizetype a = qsizetype(i) * n / bars;
        const qsizetype b = std::max<qsizetype>(a + 1, qsizetype(i + 1) * n / bars);
        qreal sum = 0.0;
        int cnt = 0;
        for (qsizetype k = a; k < b && k < n; ++k, ++cnt)
            sum += peaks.at(k);
        const qreal v = cnt > 0 ? sum / cnt : 0.0;
        // Enyhe széthúzás (kitevő > 1): a hangos és a halk szakaszok különbsége a kis
        // magasságú rajzon is látszik — így olvasható le, hol volt beszéd.
        out.append(std::pow(std::clamp(v / ref, 0.0, 1.0), kLevelGamma));
    }
    return out;
}

void WaveformItem::paint(QPainter* painter)
{
    tanara::PerfScope perfScope("WaveformItem::paint", 20);
    const qreal w = width(), h = height();
    const qreal step = m_barWidth + m_barGap;
    const int bars = int(std::floor((w + m_barGap) / step));
    if (bars <= 0 || h <= 0)
        return;

    painter->setPen(Qt::NoPen);
    painter->setBrush(m_color);
    painter->setRenderHint(QPainter::Antialiasing, true);

    const bool dots = m_flat || m_loading || m_peaks.isEmpty();
    const QList<qreal> levels = dots ? QList<qreal>() : barLevels(m_peaks, bars, m_reference);
    const qreal minBar = m_barWidth;                 // a csend is látszó pont
    const qreal loadingBar = std::min(h, 10.0);
    for (int i = 0; i < bars; ++i) {
        qreal bh;
        if (m_loading)       bh = loadingBar;
        else if (dots)       bh = minBar * 0.5;
        else                 bh = std::max(minBar, levels.at(i) * h);
        const QRectF r(i * step, (h - bh) / 2.0, m_barWidth, bh);
        painter->drawRoundedRect(r, m_barWidth / 2.0, m_barWidth / 2.0);
    }
}

} // namespace tanara_qml
