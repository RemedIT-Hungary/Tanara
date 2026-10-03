import QtQuick
import QtQuick.Layouts

// Egy futó feladat egy szakasza (M04 szakasz-lista): állapot-kör, név, jobbra a VALÓS haladás.
// state: "waiting" | "running" | "done" | "failed" | "skipped". Százalék csak akkor látszik,
// ha a szakasz tényleg mér (percent >= 0); különben futó (határozatlan) csík.
//   JobStageRow { label: qsTr("Feltöltés"); stageState: "running"; percent: 62 }
RowLayout {
    id: root

    property string label: ""
    property string stageState: "waiting"
    property int percent: -1
    property string detail: ""
    // Futó szakasz, amely nem kap saját csíkot (pl. a beszélő-szétválasztás az átírással egy
    // menetben fut): csak ez a megjegyzés látszik.
    property string runningNote: ""
    property real labelWidth: 220

    readonly property bool running: stageState === "running"
    readonly property bool done: stageState === "done"
    readonly property bool failed: stageState === "failed"
    readonly property bool skipped: stageState === "skipped"

    spacing: 12

    Rectangle {
        implicitWidth: 20; implicitHeight: 20; radius: 10
        color: root.done ? Theme.successSoft : root.failed ? Theme.dangerSoft : Theme.raised
        border.width: 1.5
        border.color: root.done ? Theme.successInk
                    : root.failed ? Theme.dangerInk
                    : root.running ? Theme.accent : Theme.borderStrong
        TIcon {
            anchors.centerIn: parent
            visible: root.done || root.failed || root.skipped
            name: root.done ? "check" : root.failed ? "x" : "minus"
            size: 12
            color: root.done ? Theme.successInk : root.failed ? Theme.dangerInk : Theme.textMuted
        }
    }
    TLabel {
        Layout.preferredWidth: root.labelWidth
        text: root.label
        elide: Text.ElideRight
        font.weight: root.running ? Theme.weightSemiBold : Theme.weightRegular
        color: root.running || root.done ? Theme.text : Theme.textMuted
    }
    // Futó szakasz: csík (+ százalék, ha van valós mérés).
    RowLayout {
        visible: root.running && root.runningNote === ""
        Layout.fillWidth: true
        spacing: 10
        TProgressBar {
            Layout.fillWidth: true
            thickness: 6
            indeterminate: root.percent < 0
            value: root.percent < 0 ? 0 : root.percent / 100
        }
        TLabel {
            visible: root.percent >= 0
            text: root.percent + "%"
            mono: true; muted: true
            font.pixelSize: Theme.fontCaption
        }
        TLabel {
            visible: root.percent < 0 && root.detail !== ""
            text: root.detail
            muted: true
            font.pixelSize: Theme.fontSmall
        }
    }
    TLabel {
        visible: !(root.running && root.runningNote === "")
        Layout.fillWidth: true
        text: root.running ? root.runningNote
            : root.detail !== "" && (root.done || root.failed) ? root.detail
            : root.skipped ? qsTr("kihagyva")
            : root.done ? "" : root.failed ? qsTr("nem sikerült") : qsTr("vár")
        muted: true
        font.pixelSize: Theme.fontSmall
        elide: Text.ElideRight
    }
}
