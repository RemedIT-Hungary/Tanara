#pragma once
//
// TranscriptLaneStrip — az áttekintő egy sora: egy beszélő megszólalásai a meeting
// idővonalán (8 px magas sáv). Egyetlen festett elem a sok kis téglalap helyett, hogy
// 700 soros meetingnél se legyen több száz QML-elem. A szoftveres rendererrel is megy.
//
#include <QColor>
#include <QList>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

namespace tanara_qml {

class TranscriptLaneStrip : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    // [x0, w0, x1, w1, …] a teljes szélesség hányadában (0..1).
    Q_PROPERTY(QList<qreal> segments READ segments WRITE setSegments NOTIFY changed)
    // Kiemelt szakaszok (a „Megmutatom" javasolt sorai) ugyanilyen formában.
    Q_PROPERTY(QList<qreal> marks READ marks WRITE setMarks NOTIFY changed)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY changed)
    Q_PROPERTY(QColor trackColor READ trackColor WRITE setTrackColor NOTIFY changed)
    Q_PROPERTY(QColor markColor READ markColor WRITE setMarkColor NOTIFY changed)

public:
    explicit TranscriptLaneStrip(QQuickItem* parent = nullptr);
    void paint(QPainter* painter) override;

    QList<qreal> segments() const { return m_segments; }
    void setSegments(const QList<qreal>& s) { m_segments = s; emit changed(); update(); }
    QList<qreal> marks() const { return m_marks; }
    void setMarks(const QList<qreal>& s) { m_marks = s; emit changed(); update(); }
    QColor color() const { return m_color; }
    void setColor(const QColor& c) { m_color = c; emit changed(); update(); }
    QColor trackColor() const { return m_track; }
    void setTrackColor(const QColor& c) { m_track = c; emit changed(); update(); }
    QColor markColor() const { return m_mark; }
    void setMarkColor(const QColor& c) { m_mark = c; emit changed(); update(); }

signals:
    void changed();

private:
    QList<qreal> m_segments;
    QList<qreal> m_marks;
    QColor m_color{Qt::gray};
    QColor m_track{Qt::transparent};
    QColor m_mark{Qt::blue};
};

} // namespace tanara_qml
