#pragma once
//
// Tanara QML — hullámforma-rajz (M09): 2 px-es, lekerekített oszlopok a gyorsítótárazott
// csúcsokból (WaveformService), a szélességhez újramintavételezve. QQuickPaintedItem, mert a
// képernyőkép-mód szoftveres rendererrel fut (shader nincs).
//   WaveformItem { peaks: model.peaks; reference: tracks.peakReference; color: Theme.speakerLine(0) }
// flat: eldobott / csendes sáv → pontsor. loading: még számol → egyforma, halvány oszlopok
// (a QML lüktető átlátszósággal jelzi).
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
    // A teljes kitérésnek megfelelő csúcs (a meeting leghangosabb sávja) — így a csendes sáv
    // tényleg laposnak látszik. <= 0 → a saját maximumához igazít.
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

    // Az oszlop-magasságok (0..1) adott oszlopszámra — a rajz és a teszt közös számítása.
    static QList<qreal> barLevels(const QList<qreal>& peaks, int bars, qreal reference);
    // A „telt magasság" szintje: a szintek 97. percentilise (egyetlen kiugró érték ne nyomja
    // össze a képet). Üres listára 0.
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
