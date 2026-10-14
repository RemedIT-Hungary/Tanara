#include "tanara/domain/Types.h"
#include "AppContext.h"

#include "RecorderServicesFacade.h"
#include "RecorderViewModel.h"
#include "TrackActivitySource.h"

#include "demo/RecorderDemoFacade.h"

#include "tanara/app/AppController.h"
#include "tanara/app/Services.h"
#include "tanara/library/store/MeetingRepository.h"

#include <QGuiApplication>
#include <QJSEngine>
#include <QStyleHints>

namespace tanara_qml {

namespace {

// The real TrackActivitySource: the core's activity computation still lives in the
// AppController (it becomes a service of its own later; then this adapter goes away).
class TrackActivityControllerSource : public TrackActivitySource {
public:
    explicit TrackActivityControllerSource(tanara::app::AppController* controller)
        : TrackActivitySource(controller), m_controller(controller)
    {
        connect(controller, &tanara::app::AppController::trackActivityReady, this,
                &TrackActivitySource::activityReady);
        connect(controller, &tanara::app::AppController::tracksChanged, this,
                &TrackActivitySource::tracksChanged);
    }

    tanara::Meeting meeting(const QString& meetingId) const override
    {
        if (meetingId.isEmpty() || !m_controller->store())
            return {};
        return m_controller->store()->load(meetingId);
    }
    tanara::MeetingActivity activity(const QString& meetingId) override
    {
        return m_controller->trackActivity(meetingId);
    }
    bool computing(const QString& meetingId) const override
    {
        return m_controller->trackActivityComputing(meetingId);
    }

private:
    tanara::app::AppController* m_controller;
};

} // namespace

AppContext::AppContext(QObject* parent) : QObject(parent)
{
    // Following the system's color scheme (only matters in "system" mode). Without a
    // QGuiApplication (e.g. QTEST_GUILESS_MAIN) there is no styleHints -> stays light.
    if (qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
        connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
                this, &AppContext::updateDark);
    }
    updateDark();
}

AppContext* AppContext::instance()
{
    // Deliberately has no parent and is never freed: it stays valid after the QML engine shuts
    // down (the view models' destructors may still refer to it).
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

QString AppContext::version() const { return tanara::libraryVersion(); }

void AppContext::setDemo(bool demo)
{
    if (demo == m_demo)
        return;
    m_demo = demo;
    emit demoChanged();
}

tanara::app::AppController* AppContext::controller() const
{
    return qobject_cast<tanara::app::AppController*>(m_controller.data());
}

QObject* AppContext::controllerObject() const
{
    return m_controller.data();
}

void AppContext::setController(tanara::app::AppController* controller)
{
    if (controller == m_controller.data())
        return;
    m_controller = controller;
    emit controllerChanged();
}

const UiServices& AppContext::services()
{
    // The same predicate as the view models' old "demo mode": no controller, or App.demo.
    tanara::app::AppController* c = controller();
    return (c == nullptr || m_demo) ? demoServices() : servicesFor(c);
}

const UiServices& AppContext::demoServices()
{
    if (!m_demoServices) {
        m_demoServices = std::make_unique<UiServices>();
        m_demoServices->recorder = new tanara_demo::RecorderDemoFacade(QString(),
                                       tanara_demo::RecorderDemoFacade::Fixture::Recorder, this);
        m_demoServices->trackActivity = [](const QString& scenario, QObject* parent) -> TrackActivitySource* {
            return new TrackActivityDemoSource(scenario, parent);
        };
    }
    return *m_demoServices;
}

const UiServices& AppContext::servicesFor(tanara::app::AppController* c)
{
    if (!c)
        return demoServices();
    const auto it = m_realServices.find(c);
    if (it != m_realServices.end())
        return it->second;

    // The facades are children of the controller: they go away with it.
    tanara::app::Services* sv = c->services();
    RecorderServicesFacade::Deps rd;
    rd.recording = &sv->recording();
    rd.store = c->store();
    rd.tags = c->tags();
    rd.settings = c->settings();
    rd.tagController = c;
    UiServices set;
    set.recorder = new RecorderServicesFacade(rd, c);
    auto* activity = new TrackActivityControllerSource(c);
    set.trackActivity = [activity](const QString&, QObject*) -> TrackActivitySource* { return activity; };
    connect(c, &QObject::destroyed, this, [this, c] { m_realServices.erase(c); });
    return m_realServices.emplace(c, std::move(set)).first->second;
}

QObject* AppContext::createRecorderDemo(const QString& designState)
{
    auto* facade = new tanara_demo::RecorderDemoFacade(designState);
    auto* vm = new RecorderViewModel(facade);
    facade->setParent(vm);
    return vm;
}

void AppContext::setBridge(QObject* bridge)
{
    if (bridge == m_bridge.data())
        return;
    m_bridge = bridge;
    emit bridgeChanged();
}

} // namespace tanara_qml
