import QtQuick

// The split button's ▾ menu (S5): "Hol fusson most?" with the step's routes (RouteOptionList) and
// "Ez legyen az alapértelmezett". vm: OverviewViewModel · step: "transcript" | "summary".
// A row: vm.start(step, route) -> the view's startRequested handler calls the shell; then, if checked,
// the route becomes the default (vm.setDefaultRoute). openFor(): asks for the Cloud estimate and moves the keyboard to the first row.
TPopover {
    id: root

    property var vm: null
    property string step: ""
    readonly property var options: {
        if (!root.vm) return []
        for (const s of root.vm.steps)
            if (s.key === root.step) return s.routeOptions || []
        return []
    }

    function openFor() {
        if (root.vm) root.vm.prepareRoutes(root.step)
        list.makeDefault = false
        root.open()
        list.focusFirst()
    }

    width: 320
    padding: 6

    RouteOptionList {
        id: list
        width: root.availableWidth
        options: root.options
        onChosen: (route, makeDefault) => {
            root.close()
            if (!root.vm) return
            // The run first (the default's write reloads the model), then the default.
            if (root.vm.start(root.step, route) && makeDefault) root.vm.setDefaultRoute(root.step, route)
        }
    }
}
