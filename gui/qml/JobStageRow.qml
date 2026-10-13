import QtQuick
import QtQuick.Layouts

// One stage of a running job (the stage list): state circle, name, and the REAL progress on the
// right. state: "waiting" | "running" | "done" | "failed" | "skipped". The percent shows only
// when the stage really measures (percent >= 0); otherwise a running (indeterminate) strip.
//   JobStageRow { label: qsTr("Upload"); stageState: "running"; percent: 62 }
RowLayout {
    id: root

    property string label: ""
    property string stageState: "waiting"
    property int percent: -1
    property string detail: ""
    // A running stage that gets no strip of its own (e.g. speaker separation runs in one pass with
    // transcription): only this note shows.
    property string runningNote: ""
    // For a running, measuring stage this part-progress shows INSTEAD of the percent (e.g. "3 / 6 parts").
    property string progressText: ""
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
    // A running stage: a strip (+ percent, if there is a real measurement).
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
            text: root.progressText !== "" ? root.progressText : root.percent + "%"
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
