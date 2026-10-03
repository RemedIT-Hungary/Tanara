#include "ImageProviders.h"

#include <QFile>
#include <QLoggingCategory>
#include <QPainter>
#include <QPainterPath>
#include <QSvgRenderer>

#include <algorithm>
#include <vector>

namespace tanara_qml {

Q_LOGGING_CATEGORY(lcQmlImg, "tanara.qml.image")

namespace {

QColor parseColor(const QString& hex)
{
    const QColor c(QLatin1Char('#') + hex);
    return c.isValid() ? c : QColor(Qt::black);
}

// Egy menetnyi doboz-elmosás egy 8 bites alfa-síkon (vízszintes + függőleges).
void boxBlur(std::vector<int>& a, int w, int h, int r)
{
    if (r <= 0)
        return;
    std::vector<int> tmp(a.size());
    const int win = 2 * r + 1;
    for (int y = 0; y < h; ++y) {
        int sum = 0;
        for (int x = -r; x <= r; ++x)
            sum += a[y * w + std::clamp(x, 0, w - 1)];
        for (int x = 0; x < w; ++x) {
            tmp[y * w + x] = sum / win;
            sum += a[y * w + std::min(x + r + 1, w - 1)] - a[y * w + std::max(x - r, 0)];
        }
    }
    for (int x = 0; x < w; ++x) {
        int sum = 0;
        for (int y = -r; y <= r; ++y)
            sum += tmp[std::clamp(y, 0, h - 1) * w + x];
        for (int y = 0; y < h; ++y) {
            a[y * w + x] = sum / win;
            sum += tmp[std::min(y + r + 1, h - 1) * w + x] - tmp[std::max(y - r, 0) * w + x];
        }
    }
}

} // namespace

TanaraImageProvider::TanaraImageProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

QImage TanaraImageProvider::renderIcon(const QString& name, const QColor& color,
                                       qreal strokeWidth, const QSize& pixelSize)
{
    QImage img(pixelSize, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);

    QFile f(QStringLiteral(":/qt/qml/Tanara/icons/%1.svg").arg(name));
    if (!f.open(QIODevice::ReadOnly)) {
        qCWarning(lcQmlImg) << "Ismeretlen ikon:" << name
                            << "(gui/qml/icons/ — új ikon: gui/qml/fetch-assets.sh <név>)";
        return img;
    }
    // A Lucide SVG-k currentColor-ral és 2-es vonalvastagsággal jönnek: a színt és a
    // vastagságot a forrásban cseréljük (az alfa a festő átlátszóságán megy).
    QByteArray svg = f.readAll();
    QColor opaque = color;
    opaque.setAlpha(255);
    svg.replace("currentColor", opaque.name(QColor::HexRgb).toLatin1());
    svg.replace("stroke-width=\"2\"",
                "stroke-width=\"" + QByteArray::number(strokeWidth, 'f', 2) + "\"");

    QSvgRenderer renderer(svg);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setOpacity(color.alphaF());
    renderer.render(&p, QRectF(QPointF(0, 0), QSizeF(pixelSize)));
    return img;
}

QImage TanaraImageProvider::renderShadow(int blur, int radius, const QColor& color)
{
    // CSS box-shadow megfelelője: a „blur” sugár ≈ 2·szigma. A kép közepe egy pixelnyi
    // nyújtható sáv; a szél (2·blur + sugár) a BorderImage kerete — lásd TShadow.qml.
    blur = std::clamp(blur, 0, 64);
    radius = std::clamp(radius, 0, 64);
    const int edge = 2 * blur + radius;
    const int side = 2 * edge + 1;

    QImage mask(side, side, QImage::Format_Alpha8);
    mask.fill(0);
    {
        QPainter p(&mask);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        p.drawRoundedRect(QRectF(blur, blur, side - 2 * blur, side - 2 * blur), radius, radius);
    }
    std::vector<int> a(size_t(side) * side);
    for (int y = 0; y < side; ++y) {
        const uchar* line = mask.constScanLine(y);
        for (int x = 0; x < side; ++x)
            a[y * side + x] = line[x];
    }
    // Három doboz-menet ≈ Gauss; blur/2 sugarú dobozzal a szigma ≈ blur/2 (mint a CSS-ben).
    const int boxR = std::max(1, blur / 2);
    if (blur > 0)
        for (int pass = 0; pass < 3; ++pass)
            boxBlur(a, side, side, boxR);

    QImage out(side, side, QImage::Format_ARGB32_Premultiplied);
    const qreal ca = color.alphaF();
    for (int y = 0; y < side; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < side; ++x) {
            const int alpha = qRound(a[y * side + x] * ca);
            line[x] = qPremultiply(qRgba(color.red(), color.green(), color.blue(), alpha));
        }
    }
    return out;
}

QImage TanaraImageProvider::requestImage(const QString& id, QSize* size,
                                         const QSize& requestedSize)
{
    const QStringList parts = id.split(QLatin1Char('/'));
    QImage img;
    if (parts.size() >= 3 && parts.at(0) == QLatin1String("icon")) {
        const qreal stroke = parts.size() >= 4 ? parts.at(3).toDouble() : 1.75;
        const QSize px = requestedSize.isValid() && !requestedSize.isEmpty() ? requestedSize
                                                                             : QSize(24, 24);
        img = renderIcon(parts.at(1), parseColor(parts.at(2)), stroke > 0 ? stroke : 1.75, px);
    } else if (parts.size() >= 4 && parts.at(0) == QLatin1String("shadow")) {
        img = renderShadow(parts.at(1).toInt(), parts.at(2).toInt(), parseColor(parts.at(3)));
    } else {
        qCWarning(lcQmlImg) << "Ismeretlen image://tanara azonosító:" << id;
        img = QImage(1, 1, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
    }
    if (size)
        *size = img.size();
    return img;
}

} // namespace tanara_qml
