import QtQuick
import QtQuick.Layouts

// PROCESSING steps: 22 px circle - done = successSoft + check; needs attention = warnSoft "!", row
// raised + warnLine border; running = 2 px accent ring + 4 px progress bar; waiting = dashed
// borderStrong circle. The model's `parallel` flag is not shown (developer hint).
// steps: [{ key, title, sub, state, parallel, progress, fixTarget? }] (OverviewViewModel.steps).
// mixdownContent / voiceContent / transcriptContent: content embedded below the step (before the
// transcript, "Start transcription" in the "transcript" step; "Start mixdown" for manual mixdown).
ColumnLayout {
    id: root

    property var steps: []
    property Component mixdownContent: null
    property Component voiceContent: null
    property Component transcriptContent: null

    spacing: 2

    Repeater {
        model: root.steps
        Rectangle {
            id: stepRow
            required property var modelData
            readonly property string state: modelData.state
            readonly property Component extra: modelData.key === "transcript" ? root.transcriptContent
                                              : modelData.key === "mixdown" ? root.mixdownContent
                                              : modelData.key === "voice" ? root.voiceContent : null
            objectName: "step_" + modelData.key
            Layout.fillWidth: true
            Layout.leftMargin: -8
            Layout.rightMargin: -8
            implicitHeight: stepContent.implicitHeight + 14
            radius: 7
            color: state === "attention" ? Theme.raised : "transparent"
            border.width: state === "attention" ? 1 : 0
            border.color: Theme.warnLine

            Item {
                id: circle
                x: 8; y: 7
                width: 22; height: 22
                Rectangle {
                    anchors.fill: parent
                    radius: 11
                    visible: stepRow.state !== "waiting"
                    color: stepRow.state === "done" ? Theme.successSoft
                         : stepRow.state === "attention" ? Theme.warnSoft : "transparent"
                    border.width: stepRow.state === "running" ? 2 : 0
                    border.color: Theme.accent
                }
                TDashedRect {
                    anchors.fill: parent
                    visible: stepRow.state === "waiting"
                    radius: 11
                    color: Theme.borderStrong
                }
                TIcon {
                    anchors.centerIn: parent
                    visible: stepRow.state === "done"
                    name: "check"
                    size: 12
                    strokeWidth: 2.5
                    color: Theme.successInk
                }
                TLabel {
                    anchors.centerIn: parent
                    visible: stepRow.state === "attention"
                    text: "!"
                    mono: true
                    color: Theme.warnInk
                    font.pixelSize: Theme.fontMicro
                    font.weight: Theme.weightBold
                }
            }
            ColumnLayout {
                id: stepContent
                x: 40; y: 9
                width: stepRow.width - 48
                spacing: 3
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    TLabel {
                        text: stepRow.modelData.title
                        color: stepRow.state === "waiting" && !stepRow.extra ? Theme.textMuted : Theme.text
                        font.pixelSize: 13
                        font.weight: Theme.weightSemiBold
                    }
                    Item { Layout.fillWidth: true }
                }
                TLabel {
                    Layout.fillWidth: true
                    visible: text !== ""
                    text: stepRow.modelData.sub
                    muted: true
                    font.pixelSize: Theme.fontCaption
                    elide: Text.ElideRight
                }
                TProgressBar {
                    visible: stepRow.modelData.progress >= 0 || stepRow.modelData.progress === -2
                    Layout.fillWidth: true
                    Layout.topMargin: 2
                    thickness: 4
                    indeterminate: stepRow.modelData.progress === -2
                    value: Math.max(0, stepRow.modelData.progress) / 100
                }
                Loader {
                    active: stepRow.extra !== null
                    visible: active
                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    sourceComponent: stepRow.extra
                }
            }
        }
    }
}
