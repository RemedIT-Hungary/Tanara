#include "QmlApp.h"

#include "AppContext.h"
#include "ImageProviders.h"

#include <QCoreApplication>
#include <QDir>
#include <QFontDatabase>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QGuiApplication>
#include <QScreen>
#include <cmath>
#include <QTimer>

namespace tanara_qml {

Q_LOGGING_CATEGORY(lcQmlApp, "tanara.qml.app")

namespace {

// Both the "--switch value" and "--switch=value" forms.
bool takeValue(const QStringList& args, int& i, const QString& name, QString* out)
{
    const QString& a = args.at(i);
    if (a == name) {
        if (i + 1 >= args.size())
            return false;
        *out = args.at(++i);
        return true;
    }
    if (a.startsWith(name + QLatin1Char('='))) {
        *out = a.mid(name.size() + 1);
        return true;
    }
    return false;
}

QVariant parsePropValue(const QString& text)
{
    // JSON (true/false, number, "text", array, object); if it is not, plain text.
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(
        QByteArray("[") + text.toUtf8() + QByteArray("]"), &err);
    if (err.error == QJsonParseError::NoError && doc.isArray() && doc.array().size() == 1)
        return doc.array().at(0).toVariant();
    return text;
}

} // namespace

QmlOptions parseQmlOptions(const QStringList& args)
{
    QmlOptions o;
    for (int i = 1; i < args.size(); ++i) {
        const QString& a = args.at(i);
        QString v;
        if (a == QLatin1String("--gallery")) {
            o.gallery = true;
        } else if (a == QLatin1String("--demo")) {
            o.demo = true;
        } else if (takeValue(args, i, QStringLiteral("--theme"), &v)) {
            o.theme = v;
        } else if (takeValue(args, i, QStringLiteral("--qml-shot"), &v)) {
            o.shotPath = v;
        } else if (takeValue(args, i, QStringLiteral("--qml-page"), &v)) {
            o.page = v;
        } else if (takeValue(args, i, QStringLiteral("--size"), &v)) {
            const QStringList wh = v.toLower().split(QLatin1Char('x'));
            const int w = wh.value(0).toInt(), h = wh.value(1).toInt();
            if (wh.size() != 2 || w <= 0 || h <= 0)
                o.error = QStringLiteral("--size: expects WIDTHxHEIGHT (e.g. 1280x820), got: %1").arg(v);
            else
                o.size = QSize(w, h);
        } else if (takeValue(args, i, QStringLiteral("--scale"), &v)) {
            o.scale = v.toDouble();
            if (o.scale < 0.5 || o.scale > 4.0)
                o.error = QStringLiteral("--scale: expects a number between 0.5 and 4, got: %1").arg(v);
        } else if (takeValue(args, i, QStringLiteral("--delay"), &v)) {
            o.delayMs = qMax(0, v.toInt());
        } else if (takeValue(args, i, QStringLiteral("--qml-prop"), &v)) {
            const int eq = v.indexOf(QLatin1Char('='));
            if (eq <= 0)
                o.error = QStringLiteral("--qml-prop: expects name=value, got: %1").arg(v);
            else
                o.props.insert(v.left(eq), parsePropValue(v.mid(eq + 1)));
        } else if (a == QLatin1String("--theme") || a == QLatin1String("--qml-shot")
                   || a == QLatin1String("--qml-page") || a == QLatin1String("--size")
                   || a == QLatin1String("--scale") || a == QLatin1String("--delay")
                   || a == QLatin1String("--qml-prop")) {
            o.error = QStringLiteral("%1: missing value").arg(a);
        }
    }
    if (o.page.isEmpty())
        o.page = o.gallery ? QStringLiteral("Gallery") : QStringLiteral("Main");
    return o;
}

void prepareProcess(const QmlOptions& opts)
{
    // Our controls are built on Qt Quick Templates; where a stock control still shows
    // (ApplicationWindow, Overlay), it gets the neutral Basic style, not the desktop theme.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    if (opts.shotMode()) {
        // Without a visible window: the offscreen platform + software (QPainter) renderer. The module
        // uses no shaders (icons / shadows come from the image provider), so this gives a faithful image.
        if (!qEnvironmentVariableIsSet("TANARA_SHOT_GPU"))
            qputenv("QT_QPA_PLATFORM", "offscreen");
        // TANARA_SHOT_GPU=1: the real (OpenGL) scene graph on the platform from QT_QPA_PLATFORM / the
        // session (a window shows briefly) instead, to check how the GPU renderer draws text (the
        // software renderer ignores the text render type).
        if (!qEnvironmentVariableIsSet("TANARA_SHOT_GPU"))
            QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
        if (!qFuzzyCompare(opts.scale, 1.0))
            qputenv("QT_SCALE_FACTOR", QByteArray::number(opts.scale));
    }
}

void applyTextRendering()
{
    // Native (FreeType/DirectWrite) rendering is sharper at small sizes, and a software-renderer
    // screenshot then matches the live window -- but its hinted glyphs are bitmap-resampled when the
    // item lands on a fractional device pixel, which smears the text at a non-integer scale
    // (KDE 1.15, Windows 125%). Qt's distance-field rendering is scale-invariant, so use it there.
    // The render type is a process-wide default read when an item is created, i.e. before any window
    // knows its own scale, so the decision comes from the primary screen. Wayland rounds a
    // fractional scale UP for QScreen (1.15 -> 2): only a screen DPR of exactly 1 is trustworthy
    // there, anything else may be fractional.
    // TANARA_TEXT_RENDER=native|qt|auto overrides (auto = decide as described).
    const QString mode = qEnvironmentVariable("TANARA_TEXT_RENDER", QStringLiteral("auto")).toLower();
    const QScreen* screen = QGuiApplication::primaryScreen();
    const qreal dpr = screen ? screen->devicePixelRatio() : 1.0;
    const bool integerDpr = qFuzzyCompare(dpr, std::round(dpr));
    const bool wayland = QGuiApplication::platformName().contains(QLatin1String("wayland"));
    const bool safeNative = integerDpr && (!wayland || qFuzzyCompare(dpr, 1.0));
    const bool native = mode == QLatin1String("native") || (mode != QLatin1String("qt") && safeNative);
    QQuickWindow::setTextRenderType(native ? QQuickWindow::NativeTextRendering
                                           : QQuickWindow::QtTextRendering);
    qCInfo(lcQmlApp).noquote() << QStringLiteral("Text rendering: %1 (DPR %2, platform: %3, mode: %4)")
                                      .arg(native ? QStringLiteral("native") : QStringLiteral("qt"))
                                      .arg(dpr).arg(QGuiApplication::platformName(), mode);
}

void applyOptions(const QmlOptions& opts)
{
    applyTextRendering();
    AppContext* ctx = AppContext::instance();
    QString theme = opts.theme;
    if (theme.isEmpty())
        theme = qEnvironmentVariable("TANARA_THEME");
    ctx->setThemeMode(theme);                       // unknown/empty -> "system"
    ctx->setDemo(opts.withoutController());
}

void setupEngine(QQmlEngine& engine)
{
    // IBM Plex Sans / Mono (SIL OFL): from the module's resources, once per process.
    static bool fontsLoaded = false;
    if (!fontsLoaded) {
        fontsLoaded = true;
        const QDir dir(QStringLiteral(":/qt/qml/Tanara/fonts"));
        const QStringList files = dir.entryList({QStringLiteral("*.ttf")}, QDir::Files);
        for (const QString& f : files)
            if (QFontDatabase::addApplicationFont(dir.filePath(f)) < 0)
                qCWarning(lcQmlApp) << "Cannot load font:" << f;
        if (files.isEmpty())
            qCWarning(lcQmlApp) << "No embedded fonts (:/qt/qml/Tanara/fonts).";
    }
    if (!engine.imageProvider(QStringLiteral("tanara")))
        engine.addImageProvider(QStringLiteral("tanara"), new TanaraImageProvider);
}

static QQuickWindow* createPage(QQmlEngine& engine, const QString& page,
                                const QVariantMap& props, const QSize& size)
{
    QQmlComponent comp(&engine);
    comp.loadFromModule("Tanara", page);
    if (comp.isError()) {
        for (const QQmlError& e : comp.errors())
            qCCritical(lcQmlApp).noquote() << e.toString();
        return nullptr;
    }
    QObject* obj = comp.createWithInitialProperties(props);
    if (!obj) {
        for (const QQmlError& e : comp.errors())
            qCCritical(lcQmlApp).noquote() << e.toString();
        return nullptr;
    }
    obj->setParent(&engine);

    if (auto* window = qobject_cast<QQuickWindow*>(obj)) {
        if (size.isValid())
            window->resize(size);
        return window;
    }
    auto* item = qobject_cast<QQuickItem*>(obj);
    if (!item) {
        qCCritical(lcQmlApp) << page << "is neither a window nor an Item — cannot be shown.";
        return nullptr;
    }
    // Non-window root (Gallery, a tab, a control...): wrapped in an ApplicationWindow so that
    // popups (Overlay) and the theme background work as in the main window.
    QQmlComponent hostComp(&engine);
    hostComp.setData("import QtQuick\nimport QtQuick.Controls\nimport Tanara\n"
                     "ApplicationWindow { color: Theme.bg; font.family: Theme.fontSans; "
                     "font.pixelSize: Theme.fontBody }\n",
                     QUrl(QStringLiteral("qrc:/qt/qml/Tanara/PageHost.qml")));
    auto* host = qobject_cast<QQuickWindow*>(hostComp.create());
    if (!host) {
        for (const QQmlError& e : hostComp.errors())
            qCCritical(lcQmlApp).noquote() << e.toString();
        return nullptr;
    }
    host->QObject::setParent(&engine);
    host->setTitle(QStringLiteral("Tanara — %1").arg(page));
    host->resize(size.isValid() ? size : QSize(1280, 820));
    QQuickItem* content = host->contentItem();
    item->setParentItem(content);
    auto fit = [item, content] { item->setSize(content->size()); };
    QObject::connect(content, &QQuickItem::widthChanged, item, fit);
    QObject::connect(content, &QQuickItem::heightChanged, item, fit);
    fit();
    return host;
}

bool loadPage(QQmlApplicationEngine& engine, const QString& page, const QVariantMap& props,
              const QSize& size)
{
    setupEngine(engine);
    QQuickWindow* window = createPage(engine, page, props, size);
    if (!window)
        return false;
    window->show();
    return true;
}

int runShot(const QmlOptions& opts)
{
    QQmlApplicationEngine engine;
    setupEngine(engine);
    int warnings = 0;
    QObject::connect(&engine, &QQmlEngine::warnings, &engine,
                     [&warnings](const QList<QQmlError>& list) { warnings += list.size(); });

    QQuickWindow* window = createPage(engine, opts.page, opts.props, opts.size);
    if (!window)
        return 2;
    window->show();

    int rc = 0;
    QTimer::singleShot(opts.delayMs, window, [&] {
        const QImage img = window->grabWindow();
        if (img.isNull() || !img.save(opts.shotPath)) {
            qCCritical(lcQmlApp) << "Cannot save the screenshot:" << opts.shotPath;
            rc = 3;
        } else {
            qCInfo(lcQmlApp).noquote()
                << QStringLiteral("%1 → %2 (%3×%4 px, theme: %5, QML warnings: %6)")
                       .arg(opts.page, opts.shotPath)
                       .arg(img.width()).arg(img.height())
                       .arg(AppContext::instance()->dark() ? QStringLiteral("dark")
                                                           : QStringLiteral("light"))
                       .arg(warnings);
        }
        QCoreApplication::exit(rc);
    });
    return QCoreApplication::exec();
}

} // namespace tanara_qml
