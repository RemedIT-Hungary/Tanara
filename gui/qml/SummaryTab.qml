import QtQuick
import QtQuick.Layouts

// Összefoglaló fül. A SummaryViewModel.view szerint három nézet egyike látszik:
//   "empty"   M06 — még nincs összefoglaló (SummaryEmptyView)
//   "summary" M07 — kész összefoglaló:
//       forrásos gyors összefoglaló (v3, vm.hasStatements): SummaryExecView (S1/S3) vagy a
//       memó (section "memo") SummaryMemoView (S2), fölötte az eszköz-sor (SummaryToolsRow);
//       régi (állítások nélküli) összefoglaló: SummaryDocView, változatlanul;
//   "topics"  M08 — témánkénti elemzés (SummaryTopicsView)
// Képernyőképhez: --qml-prop 'demoState="stale|done|memo|memoShort|oldSummary|oldMemo|running|
// topicsDoc|empty|emptyBlocked|emptyRunning|emptyRunningParts|emptyRunningMerge|emptyError|
// emptyErrorKept|emptyNote|noteOpen|noteChanged|topics"' (alapértelmezés: "stale"); v3:
// "sourcesOn|sourcesOff|sourcePopover|memoSections|staleTargeted|noSources".
//
// A héj (U1) bekötése: az eszköz-sort (SummaryToolsRow { vm: summaryTab.vm … }) a fejléc alatti
// eszköz-sorba, a térkép-sorokat (SummaryMapRows { vm: summaryTab.vm; player: … }) a dokk
// extraRows helyére teszi, és embedToolsRow / embedMapRows / showSectionSwitch = false. Addig
// ezek IDEIGLENESEN itt, a fül tetején látszanak.
Item {
    id: root

    property string meetingId: ""      // a kijelölt megbeszélés; "" = nincs
    property var player: null          // PlayerController vagy null
    property var shell: null           // ShellActions vagy null
    property string demoState: ""
    // A témánkénti munkaterület (M08) nyitva van-e — kívülről is váltható.
    property alias topicsOpen: summaryVm.topicsOpen
    // A kész összefoglaló látható formája: "exec" (Vezetői összefoglaló) | "memo" (Memó) —
    // a héj fülei állítják.
    property alias section: summaryVm.section
    readonly property alias vm: summaryVm

    // Ideiglenes beágyazások (amíg a héj eszköz-sora / térkép-dokkja nem veszi át).
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

    // A Beállítások bezárása után a kapuzás újraolvasása.
    onVisibleChanged: if (visible) summaryVm.refresh()
    Connections {
        target: Qt.application
        function onStateChanged() { if (Qt.application.state === Qt.ApplicationActive && root.visible) summaryVm.refresh() }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // IDEIGLENES: az U1 eszköz-sora veszi át.
        SummaryToolsRow {
            objectName: "summaryToolsRow"
            visible: root.embedToolsRow && root.sourcesLayout
            Layout.fillWidth: true
            vm: summaryVm
            meetingId: root.meetingId
            shell: root.shell
            showSectionSwitch: root.showSectionSwitch
        }
        // IDEIGLENES: az U1 térkép-dokkjának extraRows helye veszi át.
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
                // Képernyőképhez: a felugró nyitva.
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

    // A demó „sourcePopover” állapota: az első mondat első chipjén nyitva (a bekezdés
    // elrendezése után).
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
