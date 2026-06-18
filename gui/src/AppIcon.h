#pragma once
//
// Tanara app-ikon — kódból rajzolt mikrofon-ikon (nincs külső asset/plugin-függőség,
// így minden platformon ugyanaz). QApplication::setWindowIcon-nal állítjuk be.
//
#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QLinearGradient>
#include <QColor>
#include <QRectF>

namespace tanara_gui {

inline QIcon makeTanaraIcon() {
    QPixmap pm(256, 256);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Háttér: lekerekített négyzet, kék→indigó átmenet.
    QLinearGradient g(0, 0, 256, 256);
    g.setColorAt(0.0, QColor(0x3B, 0x82, 0xF6));
    g.setColorAt(1.0, QColor(0x63, 0x66, 0xF1));
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawRoundedRect(QRectF(8, 8, 240, 240), 52, 52);

    // Mikrofon-test (fehér kapszula).
    p.setBrush(Qt::white);
    p.drawRoundedRect(QRectF(100, 44, 56, 104), 28, 28);

    // Tartó-ív (cradle) a mikrofon alatt.
    QPen pen(Qt::white);
    pen.setWidth(14);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawArc(QRectF(78, 84, 100, 100), 180 * 16, 180 * 16);   // alsó félkör

    // Szár + talp.
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::white);
    p.drawRoundedRect(QRectF(121, 182, 14, 20), 6, 6);
    p.drawRoundedRect(QRectF(98, 200, 60, 16), 8, 8);

    p.end();
    return QIcon(pm);
}

} // namespace tanara_gui
