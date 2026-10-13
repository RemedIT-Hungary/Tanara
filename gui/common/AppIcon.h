#pragma once
// Tanara app icon: a microphone icon drawn in code (no external asset/plugin dependency, so
// it looks the same on every platform). Set with QApplication::setWindowIcon.
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

    // Background: rounded square, blue-to-indigo gradient.
    QLinearGradient g(0, 0, 256, 256);
    g.setColorAt(0.0, QColor(0x3B, 0x82, 0xF6));
    g.setColorAt(1.0, QColor(0x63, 0x66, 0xF1));
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawRoundedRect(QRectF(8, 8, 240, 240), 52, 52);

    // Microphone body (white capsule).
    p.setBrush(Qt::white);
    p.drawRoundedRect(QRectF(100, 44, 56, 104), 28, 28);

    // Cradle arc under the microphone.
    QPen pen(Qt::white);
    pen.setWidth(14);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawArc(QRectF(78, 84, 100, 100), 180 * 16, 180 * 16);   // lower half circle

    // Stem + base.
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::white);
    p.drawRoundedRect(QRectF(121, 182, 14, 20), 6, 6);
    p.drawRoundedRect(QRectF(98, 200, 60, 16), 8, 8);

    p.end();
    return QIcon(pm);
}

} // namespace tanara_gui
