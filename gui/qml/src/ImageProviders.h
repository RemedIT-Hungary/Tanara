#pragma once
//
// Tanara QML — kép-provider (image://tanara/…). Shader nélkül, CPU-n rajzol, ezért a
// szoftveres rendererrel (képernyőkép-mód, gyenge GPU) is pontosan ugyanazt adja.
//
//   image://tanara/icon/<név>/<szín-hex>/<vonalvastagság>
//       Lucide SVG (:/qt/qml/Tanara/icons/<név>.svg) a kért színnel, a kért pixelméretben
//       raszterelve (a TIcon a devicePixelRatio-val szorzott sourceSize-t kér → törtléptéken
//       is éles). A szín "rrggbb" vagy "aarrggbb" (a '#' nélkül).
//   image://tanara/shadow/<blur>/<sarok-sugár>/<szín-hex>
//       Elmosott, lekerekített téglalap kilencfoltos (BorderImage) árnyéknak — lásd TShadow.
//
#include <QQuickImageProvider>

namespace tanara_qml {

class TanaraImageProvider : public QQuickImageProvider {
public:
    TanaraImageProvider();
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

    // Külön is hívhatók (tesztelhetők).
    static QImage renderIcon(const QString& name, const QColor& color, qreal strokeWidth,
                             const QSize& pixelSize);
    static QImage renderShadow(int blur, int radius, const QColor& color);
};

} // namespace tanara_qml
