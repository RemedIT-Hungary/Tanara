#pragma once
//
// SpeakerRailCells — a beszélő-sín EGY sora (a lista-delegált bal oldalán): a megszólalás
// blokkja a beszélője oszlopában, a sor teljes magasságában. Egyetlen festett elem rajzolja
// az összes oszlop állapotát, így soronként nem kell oszloponkénti QML-elem:
//   tömör blokk · bizonytalan (lágy kitöltés + 135°-os csíkok) · kijelölt (2 px keret, a
//   többi oszlopban szaggatott ejtési cél) · javasolt (2 px accent keret) · húzás forrása
//   (30 %) · húzás célja (lágy kitöltés + 2 px szaggatott keret a cél színében) · javasolt
//   cél-oszlop (2 px accent keret az üres cellában: az új személyhez hasonló sor).
// Oszlopok: `laneCount` darab 24 px-es beszélő-oszlop, utána (hasGroup) egy 20 px-es
// „+N" csoport-oszlop. A „+" oszlop a sorokban üres.
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
    Q_PROPERTY(qreal leftInset MEMBER m_leftInset NOTIFY changed)       // az első oszlop bal széle
    Q_PROPERTY(qreal laneWidth MEMBER m_laneWidth NOTIFY changed)
    Q_PROPERTY(qreal groupWidth MEMBER m_groupWidth NOTIFY changed)
    // A sor beszélőjének oszlopa; -1 = a csoport-oszlopban; -2 = nincs blokk.
    Q_PROPERTY(int ownLane MEMBER m_ownLane NOTIFY changed)
    Q_PROPERTY(QColor lineColor MEMBER m_line NOTIFY changed)
    Q_PROPERTY(QColor softColor MEMBER m_soft NOTIFY changed)
    Q_PROPERTY(bool uncertain MEMBER m_uncertain NOTIFY changed)
    Q_PROPERTY(bool selected MEMBER m_selected NOTIFY changed)
    Q_PROPERTY(bool suggested MEMBER m_suggested NOTIFY changed)
    Q_PROPERTY(bool dimmed MEMBER m_dimmed NOTIFY changed)
    // Javasolt cél-oszlop (az új személyhez hasonló sor): 2 px accent keret az üres cellában (-1 = nincs).
    Q_PROPERTY(int suggestLane MEMBER m_suggestLane NOTIFY changed)
    // Húzás célja ebben a sorban (-1 = nincs).
    Q_PROPERTY(int targetLane MEMBER m_targetLane NOTIFY changed)
    Q_PROPERTY(QColor targetLineColor MEMBER m_targetLine NOTIFY changed)
    Q_PROPERTY(QColor targetSoftColor MEMBER m_targetSoft NOTIFY changed)
    Q_PROPERTY(QColor outlineColor MEMBER m_outline NOTIFY changed)     // kijelölés (Theme.text)
    Q_PROPERTY(QColor dashColor MEMBER m_dash NOTIFY changed)           // ejtési cél (borderStrong)
    Q_PROPERTY(QColor accentColor MEMBER m_accent NOTIFY changed)       // javasolt sor

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
    QColor m_outline{Qt::black};
    QColor m_dash{Qt::gray};
    QColor m_accent{Qt::blue};
};

// Csíkozott (135°) kitöltésű kis minta — a „Bizonytalan" szűrő-chip jelölője.
class SpeakerHatch : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QColor color MEMBER m_color NOTIFY changed)
    Q_PROPERTY(QColor backColor MEMBER m_fill NOTIFY changed)
    Q_PROPERTY(qreal radius MEMBER m_radius NOTIFY changed)
    Q_PROPERTY(qreal stripe MEMBER m_stripe NOTIFY changed)             // csík- és hézagszélesség

public:
    explicit SpeakerHatch(QQuickItem* parent = nullptr);
    void paint(QPainter* painter) override;
    // Közös rajzoló: lekerekített téglalap `fill` kitöltéssel és `line` csíkokkal.
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
