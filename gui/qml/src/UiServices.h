#pragma once
//
// UiServices: the facades the view models are built on, ONE set per mode. AppContext hands the
// set out: services() (the real ones of the process's controller, or the fictional ones when
// there is none / App.demo), servicesFor(controller), demoServices(). The view-model factories
// (QmlApp, *WindowHost, the QML default constructors) give a facade to the view model's
// constructor; the view models themselves never look at the controller.
//
// The set grows with the P15 sub-slices (library, transcript, summary, people, settings ...).
//
#include <QObject>
#include <QString>

#include <functional>

namespace tanara::recording { class IRecorderFacade; }
namespace tanara::library { class ILibraryFacade; class IImportFacade; }
namespace tanara::editing { class IShellFacade; }
namespace tanara::library { class IOverviewFacade; }

namespace tanara_qml {

class TrackActivitySource;

struct UiServices {
    // The recorder (devices, levels, recording, the finished meeting's notes, tags).
    tanara::recording::IRecorderFacade* recorder = nullptr;
    // The speech-activity frames of the tracks. A factory: the demo source depends on the
    // asking model's scenario (its demoState); the real source is shared.
    std::function<TrackActivitySource*(const QString& scenario, QObject* parent)> trackActivity;
    // The library column: the list, search and filters, "Waiting for you", the multi-select panel.
    tanara::library::ILibraryFacade* library = nullptr;
    // The shell: the action surface, gating, shared undo, the selected meeting's header and task bar.
    tanara::editing::IShellFacade* shell = nullptr;
    // The "Import audio file" window.
    tanara::library::IImportFacade* import = nullptr;
    // The Overview tab. A factory: the demo facade serves one design state (its scenario) and keeps
    // its edits, so every asking model gets its own; the real one is shared.
    std::function<tanara::library::IOverviewFacade*(const QString& scenario, QObject* parent)> overview;
};

} // namespace tanara_qml
