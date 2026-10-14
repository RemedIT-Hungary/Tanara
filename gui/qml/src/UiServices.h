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

namespace tanara_qml {

class TrackActivitySource;

struct UiServices {
    // The recorder (devices, levels, recording, the finished meeting's notes, tags).
    tanara::recording::IRecorderFacade* recorder = nullptr;
    // The speech-activity frames of the tracks. A factory: the demo source depends on the
    // asking model's scenario (its demoState); the real source is shared.
    std::function<TrackActivitySource*(const QString& scenario, QObject* parent)> trackActivity;
};

} // namespace tanara_qml
