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
#include <QTimer>

namespace tanara_qml {

Q_LOGGING_CATEGORY(lcQmlApp, "tanara.qml.app")

namespace {

// "--kapcsoló érték" és "--kapcsoló=érték" alak is.
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
    // JSON (true/false, szám, "szöveg", tömb, objektum) — ha nem az, sima szöveg.
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
                o.error = QStringLiteral("--size: SZÉLESSÉGxMAGASSÁG kell (pl. 1280x820), ez jött: %1").arg(v);
            else
                o.size = QSize(w, h);
        } else if (takeValue(args, i, QStringLiteral("--scale"), &v)) {
            o.scale = v.toDouble();
            if (o.scale < 0.5 || o.scale > 4.0)
                o.error = QStringLiteral("--scale: 0.5 és 4 közötti szám kell, ez jött: %1").arg(v);
        } else if (takeValue(args, i, QStringLiteral("--delay"), &v)) {
            o.delayMs = qMax(0, v.toInt());
        } else if (takeValue(args, i, QStringLiteral("--qml-prop"), &v)) {
            const int eq = v.indexOf(QLatin1Char('='));
            if (eq <= 0)
                o.error = QStringLiteral("--qml-prop: név=érték kell, ez jött: %1").arg(v);
            else
                o.props.insert(v.left(eq), parsePropValue(v.mid(eq + 1)));
        } else if (a == QLatin1String("--theme") || a == QLatin1String("--qml-shot")
                   || a == QLatin1String("--qml-page") || a == QLatin1String("--size")
                   || a == QLatin1String("--scale") || a == QLatin1String("--delay")
                   || a == QLatin1String("--qml-prop")) {
            o.error = QStringLiteral("%1: hiányzik az érték").arg(a);
        }
    }
    if (o.page.isEmpty())
        o.page = o.gallery ? QStringLiteral("Gallery") : QStringLiteral("Main");
    return o;
}

void prepareProcess(const QmlOptions& opts)
{
    // A vezérlőink Qt Quick Templates-re épülnek; ahol mégis „gyári” vezérlő látszik
    // (ApplicationWindow, Overlay), az a semleges Basic stílust kapja, ne az asztali témát.
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    // Natív (FreeType/DirectWrite) szövegrajzolás: kis méretben élesebb, és a szoftveres
    // rendererrel készült képernyőkép így ugyanazt mutatja, mint az élő ablak.
    QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);

    if (opts.shotMode()) {
        // Látható ablak nélkül: offscreen platform + szoftveres (QPainter) renderer. A modul
        // nem használ shadert (ikon/árnyék a kép-providerből jön), így ez hű képet ad.
        qputenv("QT_QPA_PLATFORM", "offscreen");
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
        if (!qFuzzyCompare(opts.scale, 1.0))
            qputenv("QT_SCALE_FACTOR", QByteArray::number(opts.scale));
    }
}

void applyOptions(const QmlOptions& opts)
{
    AppContext* ctx = AppContext::instance();
    QString theme = opts.theme;
    if (theme.isEmpty())
        theme = qEnvironmentVariable("TANARA_THEME");
    ctx->setThemeMode(theme);                       // ismeretlen/üres → "system"
    ctx->setDemo(opts.withoutController());
}

void setupEngine(QQmlEngine& engine)
{
    // IBM Plex Sans / Mono (SIL OFL) — a modul erőforrásaiból, egyszer folyamatonként.
    static bool fontsLoaded = false;
    if (!fontsLoaded) {
        fontsLoaded = true;
        const QDir dir(QStringLiteral(":/qt/qml/Tanara/fonts"));
        const QStringList files = dir.entryList({QStringLiteral("*.ttf")}, QDir::Files);
        for (const QString& f : files)
            if (QFontDatabase::addApplicationFont(dir.filePath(f)) < 0)
                qCWarning(lcQmlApp) << "Nem tölthető be a betű:" << f;
        if (files.isEmpty())
            qCWarning(lcQmlApp) << "Nincsenek beágyazott betűk (:/qt/qml/Tanara/fonts).";
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
        qCCritical(lcQmlApp) << page << "se nem ablak, se nem Item — nem jeleníthető meg.";
        return nullptr;
    }
    // Nem-ablak gyökér (Gallery, egy fül, egy vezérlő…): ApplicationWindow-ba csomagoljuk,
    // hogy a felugrók (Overlay) és a téma-háttér ugyanúgy működjenek, mint a főablakban.
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
            qCCritical(lcQmlApp) << "Nem menthető a képernyőkép:" << opts.shotPath;
            rc = 3;
        } else {
            qCInfo(lcQmlApp).noquote()
                << QStringLiteral("%1 → %2 (%3×%4 px, téma: %5, QML-figyelmeztetés: %6)")
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
