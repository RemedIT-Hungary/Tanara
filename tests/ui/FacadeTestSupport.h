#pragma once
//
// Test support: the real facades of an AppController in an isolated TANARA_HOME (what
// AppContext::servicesFor() builds), for the view models that take a facade in their constructor.
#include "AppContext.h"

#include "tanara/editing/IShellFacade.h"
#include "tanara/library/IImportFacade.h"
#include "tanara/library/ILibraryFacade.h"
#include "tanara/library/IOverviewFacade.h"

namespace tanara::app { class AppController; }

namespace facadetest {

inline tanara::library::ILibraryFacade* libraryOf(tanara::app::AppController* app)
{
    return tanara_qml::AppContext::instance()->servicesFor(app).library;
}
inline tanara::editing::IShellFacade* shellOf(tanara::app::AppController* app)
{
    return tanara_qml::AppContext::instance()->servicesFor(app).shell;
}
inline tanara::library::IImportFacade* importOf(tanara::app::AppController* app)
{
    return tanara_qml::AppContext::instance()->servicesFor(app).import;
}
inline tanara::library::IOverviewFacade* overviewOf(tanara::app::AppController* app)
{
    return tanara_qml::AppContext::instance()->servicesFor(app).overview(QString(), nullptr);
}

} // namespace facadetest
