import QtQuick
import QtQuick.Layouts

// FELDOLGOZÁS lépései (design/handoff-v3 V1/V5/V6): 22 px-es kör — kész = successSoft + pipa;
// figyelmet kér = warnSoft „!”, a sor raised + warnLine keret; fut = 2 px accent gyűrű + 4 px
// haladás-csík; vár = szaggatott borderStrong kör. A párhuzamos lépéseken mono „‖ párhuzamosan”.
// steps: [{ key, title, sub, state, parallel, progress }] (OverviewViewModel.steps).
ColumnLayout {
    id: root

    property var steps: []

    spacing: 2

    Repeater {
        model: root.steps
        Rectangle {
            id: stepRow
            required property var modelData
            readonly property string state: modelData.state
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
                        color: stepRow.state === "waiting" ? Theme.textMuted : Theme.text
                        font.pixelSize: 13
                        font.weight: Theme.weightSemiBold
                    }
                    Rectangle {
                        visible: stepRow.modelData.parallel === true
                        implicitWidth: parLabel.implicitWidth + 10
                        implicitHeight: 15
                        radius: 3
                        color: Theme.sunken
                        TLabel {
                            id: parLabel
                            anchors.centerIn: parent
                            text: qsTr("‖ párhuzamosan")
                            mono: true
                            muted: true
                            font.pixelSize: 10
                            font.weight: Theme.weightMedium
                        }
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
            }
        }
    }
}
