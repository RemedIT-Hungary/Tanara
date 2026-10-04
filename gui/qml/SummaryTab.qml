import QtQuick

// Összefoglaló fül. A SummaryViewModel.view szerint három nézet egyike látszik:
//   "empty"   M06 — még nincs összefoglaló (SummaryEmptyView)
//   "summary" M07 — kész összefoglaló (vezetői összefoglaló + memó), elavult-sávval (SummaryDocView)
//   "topics"  M08 — témánkénti elemzés (SummaryTopicsView)
// Képernyőképhez: --qml-prop 'demoState="stale|done|memo|memoShort|oldSummary|oldMemo|running|
// topicsDoc|empty|emptyBlocked|emptyRunning|emptyRunningParts|emptyRunningMerge|emptyError|
// emptyErrorKept|topics"' (alapértelmezés: "stale").
Item {
    id: root

    property string meetingId: ""      // a kijelölt megbeszélés; "" = nincs
    property var player: null          // PlayerController vagy null (az időbélyegek a shell.seekTo-n át ugranak)
    property var shell: null           // ShellActions vagy null
    property string demoState: ""
    // A témánkénti munkaterület (M08) nyitva van-e — kívülről is váltható.
    property alias topicsOpen: summaryVm.topicsOpen

    SummaryViewModel {
        id: summaryVm
        meetingId: root.meetingId
        demoState: root.demoState
    }

    // A Beállítások bezárása után a kapuzás újraolvasása.
    onVisibleChanged: if (visible) summaryVm.refresh()
    Connections {
        target: Qt.application
        function onStateChanged() { if (Qt.application.state === Qt.ApplicationActive && root.visible) summaryVm.refresh() }
    }

    Loader {
        anchors.fill: parent
        active: summaryVm.view === "empty"
        visible: active
        sourceComponent: SummaryEmptyView { vm: summaryVm; meetingId: root.meetingId; shell: root.shell }
    }
    Loader {
        anchors.fill: parent
        active: summaryVm.view === "summary"
        visible: active
        sourceComponent: SummaryDocView { vm: summaryVm; meetingId: root.meetingId; shell: root.shell }
    }
    Loader {
        anchors.fill: parent
        active: summaryVm.view === "topics"
        visible: active
        sourceComponent: SummaryTopicsView { vm: summaryVm; meetingId: root.meetingId; shell: root.shell }
    }
}
