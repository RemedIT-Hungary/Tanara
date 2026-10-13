#pragma once
//
// Tanara QML: waveform drawing: 2 px rounded columns from the cached peaks (WaveformService),
// resampled to the width. A QQuickPaintedItem, because screenshot mode runs with the software
// renderer (no shaders).
//   WaveformItem { peaks: model.peaks; reference: tracks.peakReference; color: Theme.speakerLine(0) }
// flat: dropped / quiet track -> a dotted line. loading: still computing -> even, dim columns
// (QML signals it with a pulsing opacity).
//
#include <QColor>
#include <QList>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

namespace tanara_qml {

class WaveformItem : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QList<qreal> peaks READ peaks WRITE setPeaks NOTIFY peaksChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)
    // The peak corresponding to the full excursion (the meeting's loudest track), so a quiet track
    // really looks flat. <= 0 -> scales to its own maximum.
    Q_PROPERTY(qreal reference READ reference WRITE setReference NOTIFY referenceChanged)
    Q_PROPERTY(bool flat READ flat WRITE setFlat NOTIFY flatChanged)
    Q_PROPERTY(bool loading READ loading WRITE setLoading NOTIFY loadingChanged)
    Q_PROPERTY(qreal barWidth READ barWidth WRITE setBarWidth NOTIFY barWidthChanged)
    Q_PROPERTY(qreal barGap READ barGap WRITE setBarGap NOTIFY barWidthChanged)

public:
    explicit WaveformItem(QQuickItem* parent = nullptr);

    void paint(QPainter* painter) override;

    QList<qreal> peaks() const { return m_peaks; }
    void setPeaks(const QList<qreal>& peaks);
    QColor color() const { return m_color; }
    void setColor(const QColor& color);
    qreal reference() const { return m_reference; }
    void setReference(qreal reference);
    bool flat() const { return m_flat; }
    void setFlat(bool flat);
    bool loading() const { return m_loading; }
    void setLoading(bool loading);
    qreal barWidth() const { return m_barWidth; }
    void setBarWidth(qreal w);
    qreal barGap() const { return m_barGap; }
    void setBarGap(qreal g);

    // The column heights (0..1) for a given column count: the shared computation of the drawing and the test.
    static QList<qreal> barLevels(const QList<qreal>& peaks, int bars, qreal reference);
    // The "full height" level: the 97th percentile of the levels (a single outlier must not
    // squash the picture). 0 for an empty list.
    static qreal referenceLevel(const QList<qreal>& levels);

signals:
    void peaksChanged();
    void colorChanged();
    void referenceChanged();
    void flatChanged();
    void loadingChanged();
    void barWidthChanged();

private:
    QList<qreal> m_peaks;
    QColor m_color{Qt::gray};
    qreal m_reference = 0.0;
    bool m_flat = false;
    bool m_loading = false;
    qreal m_barWidth = 2.0;
    qreal m_barGap = 2.0;
};

} // namespace tanara_qml
