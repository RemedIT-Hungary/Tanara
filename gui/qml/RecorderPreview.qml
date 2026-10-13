import QtQuick

// Screenshot shell for the recorder: it draws the RecorderView / RecorderPill in one design
// state (with made-up devices), without a window.
//   tanara --qml-shot out.png --qml-page RecorderPreview --size 420x600 --qml-prop 'demoState="R03"'
// demoState: R01 collapsed · R02 expanded + title editing · R03 recording · R04 recording
// expanded · R05 pill · R06 "End of the meeting?" · R07 close sheet · R09 done · R10 no device
// · R03typing: tag field open while recording, "q4" typed
Rectangle {
    id: root
    property string demoState: "R01"
    color: Theme.sunken

    RecorderViewModel { id: model; demoState: root.demoState === "R03typing" ? "R03" : root.demoState }

    Item {
        x: 20; y: 20
        width: root.demoState === "R05" ? pill.width : view.width
        height: root.demoState === "R05" ? pill.height : view.height
        TShadow { radius: root.demoState === "R05" ? 20 : Theme.radiusDialog }

        RecorderView {
            id: view
            visible: root.demoState !== "R05"
            vm: model
            expanded: root.demoState === "R02" || root.demoState === "R04"
            // R02: the title field while editing; R07: the close sheet open.
            Timer {
                interval: 1
                running: true
                onTriggered: {
                    if (root.demoState === "R02") view.beginTitleEdit()
                    if (root.demoState === "R07") view.sheetOpen = true
                    if (root.demoState === "R03typing") { model.removeTag("t-q4"); view.beginTagInput("q4") }
                }
            }
        }
        RecorderPill {
            id: pill
            visible: root.demoState === "R05"
            vm: model
        }
    }
}
