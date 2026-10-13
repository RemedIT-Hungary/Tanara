#pragma once
//
// TranscriptLaneStrip: one row of the overview: a speaker's utterances on the meeting
// timeline (8 px high band). One painted item instead of many small rectangles, so even a
// 700-row meeting has no hundreds of QML items. It works with the software renderer too.
//
#include <QColor>
#include <QList>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

namespace tanara_qml {

class TranscriptLaneStrip : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    // [x0, w0, x1, w1, ...] as a fraction of the full width (0..1).
    Q_PROPERTY(QList<qreal> segments READ segments WRITE setSegments NOTIFY changed)
    // Highlighted spans (the "Show me" suggested rows) in the same form.
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
