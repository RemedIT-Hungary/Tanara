#pragma once
//
// A tálca-ikon három állapota (design/handoff-recorder R11), kódból rajzolva — nincs külső
// asset. Header-only (QtGui), a figyelő (watcher/) és az önálló felvevő is ezt használja.
//   Figyel          halvány gyűrű + pötty
//   Hívás észlelve  accent gyűrű + pötty
//   Felvétel fut    teli piros korong + fehér pötty
//
#include <QColor>
#include <QGuiApplication>
#include <QIcon>
#include <QPainter>
#include <QPalette>
#include <QPixmap>

namespace tanara_gui {

enum class TrayState { Watching, CallDetected, Recording };

// Sötét-e a panel (ahol a tálca-ikon megjelenik)? A rendszer-paletta ablakszínéből becsüljük;
// a legtöbb asztalon a panel a színsémát követi.
inline bool trayPanelIsDark()
{
    return QGuiApplication::palette().color(QPalette::Window).lightness() < 128;
}

// Egy méret kirajzolása (px × px). A design 22 px-es rajzát arányosan nagyítjuk:
// 2 px gyűrű, 8 px pötty.
inline QPixmap drawTrayPixmap(TrayState state, int px, bool darkPanel)
{
    // A Theme.qml megfelelő tokenjei (textMuted / accent / rec), sötét és világos panelre.
    const QColor muted  = darkPanel ? QColor(0xac, 0xab, 0xa7) : QColor(0x58, 0x5b, 0x5f);
    const QColor accent = darkPanel ? QColor(0x7e, 0xb1, 0xf3) : QColor(0x2f, 0x62, 0xac);
    const QColor rec    = darkPanel ? QColor(0xe9, 0x50, 0x4d) : QColor(0xd4, 0x2f, 0x34);

    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    const qreal u = px / 22.0;                       // a design egysége
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
