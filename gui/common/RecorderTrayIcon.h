#pragma once
// The tray icon's three states, drawn in code (no external asset). Header-only (QtGui); the
// watcher (watcher/) and the standalone recorder both use it.
//   Watching       faint ring + dot
//   Call detected  accent ring + dot
//   Recording      solid red disc + white dot
#include <QColor>
#include <QGuiApplication>
#include <QIcon>
#include <QPainter>
#include <QPalette>
#include <QPixmap>

namespace tanara_gui {

enum class TrayState { Watching, CallDetected, Recording };

// Is the panel dark (where the tray icon appears)? Estimated from the system palette's
// window color; on most desktops the panel follows the color scheme.
inline bool trayPanelIsDark()
{
    return QGuiApplication::palette().color(QPalette::Window).lightness() < 128;
}

// Draws one size (px x px). The design's 22 px drawing is scaled proportionally:
// 2 px ring, 8 px dot.
inline QPixmap drawTrayPixmap(TrayState state, int px, bool darkPanel)
{
    // The matching Theme.qml tokens (textMuted / accent / rec), for dark and light panels.
    const QColor muted  = darkPanel ? QColor(0xac, 0xab, 0xa7) : QColor(0x58, 0x5b, 0x5f);
    const QColor accent = darkPanel ? QColor(0x7e, 0xb1, 0xf3) : QColor(0x2f, 0x62, 0xac);
    const QColor rec    = darkPanel ? QColor(0xe9, 0x50, 0x4d) : QColor(0xd4, 0x2f, 0x34);

    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    const qreal u = px / 22.0;                       // the design unit
    const qreal ring = 2.0 * u;
    const QRectF outer(ring / 2, ring / 2, px - ring, px - ring);
    const QRectF dot(px / 2.0 - 4 * u, px / 2.0 - 4 * u, 8 * u, 8 * u);

    if (state == TrayState::Recording) {
        p.setPen(Qt::NoPen);
        p.setBrush(rec);
        p.drawEllipse(QRectF(0, 0, px, px));
        p.setBrush(Qt::white);
        p.drawEllipse(dot);
    } else {
        const QColor c = state == TrayState::CallDetected ? accent : muted;
        p.setPen(QPen(c, ring));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(outer);
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawEllipse(dot);
    }
    p.end();
    return pm;
}

inline QIcon makeTrayIcon(TrayState state, bool darkPanel = trayPanelIsDark())
{
    QIcon icon;
    for (int px : {16, 22, 24, 32, 44, 48, 64})
        icon.addPixmap(drawTrayPixmap(state, px, darkPanel));
    return icon;
}

} // namespace tanara_gui
