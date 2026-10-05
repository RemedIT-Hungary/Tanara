import QtQuick

// Képernyőkép-burok a felvevőhöz: a RecorderView / RecorderPill elemet a design egy
// állapotában (kitalált eszközökkel) rajzolja ki, ablak nélkül.
//   tanara --qml-shot ki.png --qml-page RecorderPreview --size 420x600 --qml-prop 'demoState="R03"'
// demoState: R01 összecsukva · R02 kinyitva + cím-szerkesztés · R03 felvétel · R04 felvétel
// kinyitva · R05 pirula · R06 „Vége a megbeszélésnek?” · R07 bezárás-lap · R09 kész · R10 nincs eszköz
// · R03typing: felvétel közben a címke-mező nyitva, „q4” begépelve (T08c)
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
            // R02: a cím-mező szerkesztés közben; R07: a bezárás-lap nyitva.
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
