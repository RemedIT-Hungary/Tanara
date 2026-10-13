import QtQuick
import QtQuick.Layouts

// Summary tab. SummaryViewModel.view selects one of three views:
//   "empty"   no summary yet (SummaryEmptyView)
//   "summary" finished summary: sourced quick summary (vm.hasStatements): SummaryExecView or,
//       for the memo (section "memo"), SummaryMemoView, with the tools row (SummaryToolsRow)
//       above; an old summary without statements: SummaryDocView, unchanged;
//   "topics"  per-topic analysis (SummaryTopicsView)
// For screenshots: --qml-prop 'demoState="stale|done|memo|memoShort|oldSummary|oldMemo|running|
// topicsDoc|empty|emptyBlocked|emptyRunning|emptyRunningParts|emptyRunningMerge|emptyError|
// emptyErrorKept|emptyNote|noteOpen|noteChanged|topics"' (default: "stale"), plus
// "sourcesOn|sourcesOff|sourcePopover|memoSections|staleTargeted|noSources".
//
// The shell wires it up by placing the tools row (SummaryToolsRow { vm: summaryTab.vm ... }) in
// the tools row under the header and the map rows (SummaryMapRows { vm: summaryTab.vm; player: ... })
// in the dock's extraRows, and setting embedToolsRow / embedMapRows / showSectionSwitch = false.
// Until then they show TEMPORARILY here, at the top of the tab.
Item {
    id: root

    property string meetingId: ""      // the selected meeting; "" = none
    property var player: null          // PlayerController or null
    property var shell: null           // ShellActions or null
    property string demoState: ""
    // Whether the per-topic workspace is open - can also be toggled from outside.
    property alias topicsOpen: summaryVm.topicsOpen
    // The finished summary's visible form: "exec" (Executive summary) | "memo" (Memo) -
    // set by the shell's tabs.
    property alias section: summaryVm.section
    readonly property alias vm: summaryVm
    // Rows of the shell's map dock: Source marks and Memo sections ({ startMs, endMs, active }).
    readonly property var sourceMarks: summaryVm.sourceMarks
    readonly property var sectionBands: summaryVm.sectionBands

    // Temporary embeddings (until the shell's tools row / map dock takes them over).
    property bool embedToolsRow: true
    property bool embedMapRows: true
    property bool showSectionSwitch: true

    readonly property bool sourcesLayout: summaryVm.view === "summary" && summaryVm.hasStatements
    readonly property bool memoLayout: sourcesLayout && summaryVm.section === "memo" && summaryVm.memoState === "ready"

    SummaryViewModel {
        id: summaryVm
        meetingId: root.meetingId
        demoState: root.demoState
    }

    // Re-reads the gating after Settings closes.
    onVisibleChanged: if (visible) summaryVm.refresh()
    Connections {
        target: Qt.application
        function onStateChanged() { if (Qt.application.state === Qt.ApplicationActive && root.visible) summaryVm.refresh() }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // TEMPORARY: the shell's tools row takes over.
        SummaryToolsRow {
            objectName: "summaryToolsRow"
            visible: root.embedToolsRow && root.sourcesLayout
            Layout.fillWidth: true
            vm: summaryVm
            meetingId: root.meetingId
            shell: root.shell
            showSectionSwitch: root.showSectionSwitch
        }
        // TEMPORARY: the shell's map dock takes the extraRows place.
        Rectangle {
            visible: root.embedMapRows && root.sourcesLayout && (mapRows.showSources || mapRows.showSections)
            Layout.fillWidth: true
            implicitHeight: mapRows.implicitHeight + 14
            color: Theme.surface
            SummaryMapRows {
                id: mapRows
                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter
                          leftMargin: 24; rightMargin: 24 }
                vm: summaryVm
                player: root.player
                showHint: true
            }
            Rectangle { anchors { left: parent.left; right: parent.right; bottom: parent.bottom } height: 1; color: Theme.border }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Loader {
                anchors.fill: parent
                active: summaryVm.view === "empty"
                visible: active
                sourceComponent: SummaryEmptyView { vm: summaryVm; meetingId: root.meetingId; shell: root.shell }
            }
            Loader {
                anchors.fill: parent
                active: summaryVm.view === "summary" && !root.sourcesLayout
                visible: active
                sourceComponent: SummaryDocView { vm: summaryVm; meetingId: root.meetingId; shell: root.shell }
            }
            Loader {
                id: execLoader
                anchors.fill: parent
                active: root.sourcesLayout && !root.memoLayout
                visible: active
                sourceComponent: SummaryExecView { vm: summaryVm; meetingId: root.meetingId; shell: root.shell; player: root.player }
                // For screenshots: the popover is open.
                onLoaded: if (root.demoState === "sourcePopover") demoPopoverTimer.start()
            }
            Loader {
                anchors.fill: parent
                active: root.memoLayout
                visible: active
                sourceComponent: SummaryMemoView { vm: summaryVm; meetingId: root.meetingId; shell: root.shell; player: root.player }
            }
            Loader {
                anchors.fill: parent
                active: summaryVm.view === "topics"
                visible: active
                sourceComponent: SummaryTopicsView { vm: summaryVm; meetingId: root.meetingId; shell: root.shell }
            }
        }
    }

    // The demo "sourcePopover" state: open on the first sentence's first chip (after the
    // paragraph's layout).
    Timer {
        id: demoPopoverTimer
        interval: 60
        onTriggered: {
            const view = execLoader.item
            const chip = view ? root.findChip(view) : null
            if (chip) view.openSources("s1", chip)
        }
    }
    function findChip(item) {
        const stack = [item]
        while (stack.length) {
            const it = stack.pop()
            if (it.objectName === "timeChip" && it.visible) return it
            for (let i = it.children.length - 1; i >= 0; --i) stack.push(it.children[i])
        }
        return null
    }
}
