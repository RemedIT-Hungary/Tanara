import QtQuick
import QtQuick.Layouts

// Sweep mode: the section summary above the "Just now" row, when playback has reached the end
// of a section (12 minutes or 20 doubtful lines): "Section done · 12:00-24:00" -- decided,
// not identifiable, later, came from the background; the new samples per speaker; the lines
// marked doubtful again behind you; [Next section] [Look at them again (N)] [Skipped (N)] x.
// summary: TranscriptEditorViewModel.sweepSectionSummary (empty map: hidden).
Rectangle {
    id: root

    property var summary: ({})
    readonly property bool shown: summary.index !== undefined
    signal nextRequested()
    signal behindRequested()
    signal laterRequested()
    signal dismissRequested()

    visible: shown
    implicitHeight: shown ? column.implicitHeight + 24 : 0
    color: Theme.raised

    TDivider { anchors { left: parent.left; right: parent.right; top: parent.top } }

    component Stat: Rectangle {
        id: stat
        property int value: 0
        property string label: ""
        property string tone: "neutral"
        implicitWidth: statRow.implicitWidth + 16
        implicitHeight: 26
        radius: Theme.radiusControl
        color: tone === "success" ? Theme.successSoft : tone === "warn" ? Theme.warnSoft : Theme.sunken
        Row {
            id: statRow
            anchors.centerIn: parent
            spacing: 6
            TLabel {
                anchors.verticalCenter: parent.verticalCenter
                text: String(stat.value)
                mono: true
                color: stat.tone === "success" ? Theme.successInk : stat.tone === "warn" ? Theme.warnInk : Theme.text
                font.weight: Theme.weightSemiBold
            }
            TLabel {
                anchors.verticalCenter: parent.verticalCenter
                text: stat.label
                muted: stat.tone === "neutral"
                color: stat.tone === "success" ? Theme.successInk : stat.tone === "warn" ? Theme.warnInk : Theme.textMuted
                font.pixelSize: Theme.fontCaption
            }
        }
    }

    ColumnLayout {
        id: column
        x: Theme.space5
        y: 12
        width: root.width - 2 * Theme.space5
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            TIcon { name: "circle-check"; size: 16; color: Theme.success }
            TLabel {
                objectName: "sweepSummaryTitle"
                text: qsTr("Szakasz kész · %1").arg(root.summary.label || "")
                font.weight: Theme.weightSemiBold
            }
            Flow {
                Layout.fillWidth: true
                spacing: 6
                Stat { objectName: "sweepSummaryDecided"; value: root.summary.decided || 0; label: qsTr("eldöntve"); tone: "success" }
                Stat { value: root.summary.ignored || 0; label: qsTr("nem azonosítható") }
                Stat { value: root.summary.later || 0; label: qsTr("később") }
                Stat { visible: (root.summary.background || 0) > 0; value: root.summary.background || 0; label: qsTr("a háttérből jött"); tone: "warn" }
            }
            TIconButton {
                objectName: "sweepSummaryClose"
                variant: "flat"
                size: "small"
                iconName: "x"
                toolTipText: qsTr("Bezárás")
                onClicked: root.dismissRequested()
            }
        }
        TLabel {
            visible: (root.summary.samplesText || "") !== ""
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: qsTr("Új minták: %1").arg(root.summary.samplesText || "")
            font.pixelSize: Theme.fontSmall
        }
        Rectangle {
            visible: (root.summary.rebehind || 0) > 0
            Layout.fillWidth: true
            implicitHeight: behindRow.implicitHeight + 10
            radius: Theme.radiusControl
            color: Theme.warnSoft
            RowLayout {
                id: behindRow
                x: 10
                width: parent.width - 20
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8
                TIcon { name: "rotate-ccw"; size: 14; color: Theme.warnInk }
                TLabel {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: qsTr("A háttér-elemzés %n már elhagyott sort újra kétesnek jelölt; a soraid nem változtak.", "", root.summary.rebehind || 0)
                    color: Theme.warnInk
                    font.pixelSize: Theme.fontSmall
                }
            }
        }
        Row {
            spacing: 8
            TButton {
                objectName: "sweepNextSection"
                variant: "primary"
                size: "small"
                text: root.summary.last === true ? qsTr("Kész")
                                                 : qsTr("Következő szakasz (%1)").arg(root.summary.nextLabel || "")
                onClicked: root.summary.last === true ? root.dismissRequested() : root.nextRequested()
            }
            TButton {
                objectName: "sweepReviewBehind"
                visible: (root.summary.rebehind || 0) > 0
                size: "small"
                text: qsTr("Visszanézem őket (%1)").arg(root.summary.rebehind || 0)
                onClicked: root.behindRequested()
            }
            TButton {
                objectName: "sweepReviewLater"
                visible: (root.summary.later || 0) > 0
                size: "small"
                variant: "ghost"
                text: qsTr("Kihagyottak (%1)").arg(root.summary.later || 0)
                onClicked: root.laterRequested()
            }
        }
    }
}
