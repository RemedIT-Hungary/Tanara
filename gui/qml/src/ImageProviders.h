#pragma once
//
// Tanara QML: image provider (image://tanara/...). Draws on the CPU without shaders, so even
// with the software renderer (screenshot mode, weak GPU) it gives exactly the same.
//
//   image://tanara/icon/<name>/<color-hex>/<stroke-width>
//       Lucide SVG (:/qt/qml/Tanara/icons/<name>.svg) in the requested color, rasterized at
//       the requested pixel size (TIcon asks for sourceSize times devicePixelRatio, so it is
//       sharp at fractional scales too). The color is "rrggbb" or "aarrggbb" (without '#').
//   image://tanara/shadow/<blur>/<corner-radius>/<color-hex>
//       A blurred rounded rectangle for a nine-patch (BorderImage) shadow; see TShadow.
//
#include <QQuickImageProvider>

namespace tanara_qml {

class TanaraImageProvider : public QQuickImageProvider {
public:
    TanaraImageProvider();
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

    // Can be called separately (testable).
    static QImage renderIcon(const QString& name, const QColor& color, qreal strokeWidth,
                             const QSize& pixelSize);
    static QImage renderShadow(int blur, int radius, const QColor& color);
};

} // namespace tanara_qml
