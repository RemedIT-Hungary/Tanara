#pragma once
//
// SpeakerRailCells: ONE row of the speaker rail (on the left of the list delegate): the
// utterance's block in its speaker's column, over the row's full height. A single painted
// item draws every column's state, so rows need no per-column QML items:
//   solid block · uncertain (soft fill + 135-degree stripes) · selected (2 px frame, in the
//   other columns a dashed drop target) · suggested (2 px accent frame) · drag source
//   (30 %) · drag target (soft fill + 2 px dashed frame in the target's color; Shift = "not a
//   sample": no fill, sparse dashed frame) · suggested
//   target column (2 px accent frame in the empty cell: a row similar to the new person).
// Columns: `laneCount` 24 px speaker columns, then (hasGroup) one 20 px "+N" group column.
// The "+" column is empty in the rows.
//
#include <QColor>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

namespace tanara_qml {

class SpeakerRailCells : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int laneCount MEMBER m_laneCount NOTIFY changed)
    Q_PROPERTY(bool hasGroup MEMBER m_hasGroup NOTIFY changed)
    Q_PROPERTY(qreal leftInset MEMBER m_leftInset NOTIFY changed)       // the left edge of the first column
    Q_PROPERTY(qreal laneWidth MEMBER m_laneWidth NOTIFY changed)
    Q_PROPERTY(qreal groupWidth MEMBER m_groupWidth NOTIFY changed)
    // The row's speaker's column; -1 = in the group column; -2 = no block.
    Q_PROPERTY(int ownLane MEMBER m_ownLane NOTIFY changed)
    Q_PROPERTY(QColor lineColor MEMBER m_line NOTIFY changed)
    Q_PROPERTY(QColor softColor MEMBER m_soft NOTIFY changed)
    Q_PROPERTY(bool uncertain MEMBER m_uncertain NOTIFY changed)
    Q_PROPERTY(bool selected MEMBER m_selected NOTIFY changed)
    Q_PROPERTY(bool suggested MEMBER m_suggested NOTIFY changed)
    Q_PROPERTY(bool dimmed MEMBER m_dimmed NOTIFY changed)
    // Suggested target column (a row similar to the new person): 2 px accent frame in the empty cell (-1 = none).
    Q_PROPERTY(int suggestLane MEMBER m_suggestLane NOTIFY changed)
    // Drag target in this row (-1 = none).
    Q_PROPERTY(int targetLane MEMBER m_targetLane NOTIFY changed)
    Q_PROPERTY(QColor targetLineColor MEMBER m_targetLine NOTIFY changed)
    Q_PROPERTY(QColor targetSoftColor MEMBER m_targetSoft NOTIFY changed)
    // The target is "not a sample" (Shift): sparse dashed frame, no fill.
    Q_PROPERTY(bool targetDashed MEMBER m_targetDashed NOTIFY changed)
    Q_PROPERTY(QColor outlineColor MEMBER m_outline NOTIFY changed)     // selection (Theme.text)
    Q_PROPERTY(QColor dashColor MEMBER m_dash NOTIFY changed)           // drop target (borderStrong)
    Q_PROPERTY(QColor accentColor MEMBER m_accent NOTIFY changed)       // suggested row

public:
    explicit SpeakerRailCells(QQuickItem* parent = nullptr);
    void paint(QPainter* painter) override;

signals:
    void changed();

private:
    QRectF cellRect(int lane) const;

    int m_laneCount = 0;
    bool m_hasGroup = false;
    qreal m_leftInset = 4;
    qreal m_laneWidth = 24;
    qreal m_groupWidth = 20;
    int m_ownLane = -2;
    QColor m_line{Qt::gray};
    QColor m_soft{Qt::lightGray};
    bool m_uncertain = false;
    bool m_selected = false;
    bool m_suggested = false;
    bool m_dimmed = false;
    int m_targetLane = -1;
    int m_suggestLane = -1;
    QColor m_targetLine{Qt::gray};
    QColor m_targetSoft{Qt::lightGray};
    bool m_targetDashed = false;
    QColor m_outline{Qt::black};
    QColor m_dash{Qt::gray};
    QColor m_accent{Qt::blue};
};

// A small stripe-filled (135 degrees) swatch: the marker of the "Uncertain" filter chip.
class SpeakerHatch : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QColor color MEMBER m_color NOTIFY changed)
    Q_PROPERTY(QColor backColor MEMBER m_fill NOTIFY changed)
    Q_PROPERTY(qreal radius MEMBER m_radius NOTIFY changed)
    Q_PROPERTY(qreal stripe MEMBER m_stripe NOTIFY changed)             // stripe and gap width

public:
    explicit SpeakerHatch(QQuickItem* parent = nullptr);
    void paint(QPainter* painter) override;
    // Shared painter: a rounded rectangle with `fill` and `line` stripes.
    static void paintHatch(QPainter* p, const QRectF& r, qreal radius, const QColor& fill,
                           const QColor& line, qreal stripe);

signals:
    void changed();

private:
    QColor m_color{Qt::gray};
    QColor m_fill{Qt::transparent};
    qreal m_radius = 2;
    qreal m_stripe = 2;
};

} // namespace tanara_qml
