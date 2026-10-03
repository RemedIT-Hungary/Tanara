#include "AppContext.h"

#include "tanara/AppController.h"

#include <QGuiApplication>
#include <QJSEngine>
#include <QStyleHints>

namespace tanara_qml {

AppContext::AppContext(QObject* parent) : QObject(parent)
{
    // A rendszer színsémájának követése (csak "system" módban számít). QGuiApplication
    // nélkül (pl. QTEST_GUILESS_MAIN) nincs styleHints → világos marad.
    if (qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
        connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
                this, &AppContext::updateDark);
    }
    updateDark();
}

AppContext* AppContext::instance()
{
    // Szándékosan nem kap szülőt és nem szabadul fel: a QML-motor leállása után is
    // érvényes marad (a nézetmodellek destruktorai még hivatkozhatnak rá).
    static AppContext* s_instance = new AppContext;
    return s_instance;
}

AppContext* AppContext::create(QQmlEngine*, QJSEngine*)
{
    AppContext* ctx = instance();
    QJSEngine::setObjectOwnership(ctx, QJSEngine::CppOwnership);
    return ctx;
}

QString AppContext::normalizedThemeMode(const QString& mode)
{
    const QString m = mode.trimmed().toLower();
    if (m == QLatin1String("light") || m == QLatin1String("dark"))
        return m;
    return QStringLiteral("system");
}

void AppContext::setThemeMode(const QString& mode)
{
    const QString m = normalizedThemeMode(mode);
    if (m == m_themeMode)
        return;
    m_themeMode = m;
    emit themeModeChanged();
    updateDark();
}

void AppContext::updateDark()
{
    bool dark = false;
    if (m_themeMode == QLatin1String("dark")) {
        dark = true;
    } else if (m_themeMode == QLatin1String("system")
               && qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
        dark = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    }
    if (dark == m_dark)
        return;
    m_dark = dark;
    emit darkChanged();
}

void AppContext::setDemo(bool demo)
{
    if (demo == m_demo)
        return;
    m_demo = demo;
    emit demoChanged();
}

tanara::AppController* AppContext::controller() const
{
    return qobject_cast<tanara::AppController*>(m_controller.data());
}

QObject* AppContext::controllerObject() const
{
    return m_controller.data();
}

void AppContext::setController(tanara::AppController* controller)
{
    if (controller == m_controller.data())
        return;
    m_controller = controller;
    emit controllerChanged();
}

void AppContext::setBridge(QObject* bridge)
{
    if (bridge == m_bridge.data())
        return;
    m_bridge = bridge;
    emit bridgeChanged();
}

} // namespace tanara_qml
