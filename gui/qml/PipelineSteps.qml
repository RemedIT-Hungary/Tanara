import QtQuick
import QtQuick.Layouts

// PROCESSING steps: 22 px circle - done = successSoft + check; needs attention = warnSoft "!", row
// raised + warnLine border; running = 2 px accent ring + 4 px progress bar; waiting = dashed
// borderStrong circle. The model's `parallel` flag is not shown (developer hint).
// stepModel: OverviewViewModel.stepModel (PipelineStepModel; roles key, title, sub, state, parallel, progress,
// note, detail, ...). It is keyed and updated in place, so a delegate (and the panel loaded into it) lives as
// long as its step: progress ticks and state changes never rebuild the rows.
// mixdownContent / voiceContent / transcriptContent / summaryContent: content embedded below the step (before
// the transcript, "Start transcription" in the "transcript" step; "Start mixdown" for manual mixdown; "Create
// summary" / "Cancel" / "Again" in the "summary" step).
ColumnLayout {
    id: root

    property var stepModel: null
    property Component mixdownContent: null
    property Component voiceContent: null
    property Component transcriptContent: null
    property Component summaryContent: null

    spacing: 2

    Repeater {
        model: root.stepModel
        Rectangle {
            id: stepRow
            required property var model
            readonly property string stepState: model.state
            readonly property Component extra: model.key === "transcript" ? root.transcriptContent
                                              : model.key === "mixdown" ? root.mixdownContent
                                              : model.key === "voice" ? root.voiceContent
                                              : model.key === "summary" ? root.summaryContent : null
            objectName: "step_" + model.key
            Layout.fillWidth: true
            Layout.leftMargin: -8
            Layout.rightMargin: -8
            implicitHeight: stepContent.implicitHeight + 14
            radius: 7
            color: stepState === "attention" ? Theme.raised : "transparent"
            border.width: stepState === "attention" ? 1 : 0
            border.color: Theme.warnLine

            Item {
                id: circle
                x: 8; y: 7
                width: 22; height: 22
                Rectangle {
                    anchors.fill: parent
                    radius: 11
                    visible: stepRow.stepState !== "waiting"
                    color: stepRow.stepState === "done" ? Theme.successSoft
                         : stepRow.stepState === "attention" ? Theme.warnSoft : "transparent"
                    border.width: stepRow.stepState === "running" ? 2 : 0
                    border.color: Theme.accent
                }
                TDashedRect {
                    anchors.fill: parent
                    visible: stepRow.stepState === "waiting"
                    radius: 11
                    color: Theme.borderStrong
                }
                TIcon {
                    anchors.centerIn: parent
                    visible: stepRow.stepState === "done"
                    name: "check"
                    size: 12
                    strokeWidth: 2.5
                    color: Theme.successInk
                }
                TLabel {
                    anchors.centerIn: parent
                    visible: stepRow.stepState === "attention"
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
                        text: stepRow.model.title
                        color: stepRow.stepState === "waiting" && !stepRow.extra ? Theme.textMuted : Theme.text
                        font.pixelSize: 13
                        font.weight: Theme.weightSemiBold
                    }
                    Item { Layout.fillWidth: true }
                }
                TLabel {
                    Layout.fillWidth: true
                    visible: text !== ""
                    text: stepRow.model.sub
                    muted: true
                    font.pixelSize: Theme.fontCaption
                    elide: Text.ElideRight
                }
                // An optional second line (D5: "Beszélők: helyi elkülönítés" under the Transcript step).
                TLabel {
                    Layout.fillWidth: true
                    visible: text !== ""
                    text: stepRow.model.note || ""
                    color: stepRow.stepState === "attention" ? Theme.warnInk : Theme.textMuted
                    font.pixelSize: Theme.fontCaption
                    wrapMode: Text.Wrap
                }
                // The technical line of a failed step (the summary step: "kapcsolat · openai-compat · <url> · ...").
                TLabel {
                    Layout.fillWidth: true
                    visible: text !== ""
                    text: stepRow.model.detail || ""
                    mono: true
                    muted: true
                    font.pixelSize: Theme.fontMicro
                    wrapMode: Text.Wrap
                }
                TProgressBar {
                    visible: stepRow.model.progress >= 0 || stepRow.model.progress === -2
                    Layout.fillWidth: true
                    Layout.topMargin: 2
                    thickness: 4
                    indeterminate: stepRow.model.progress === -2
                    value: Math.max(0, stepRow.model.progress) / 100
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
