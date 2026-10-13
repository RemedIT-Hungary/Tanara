import QtQuick
import QtQuick.Layouts

// The map dock's summary rows: "Sections" (memo: the sections as bars, the highlighted one
// accent) and "Source" (the statements' sources: accentLine, the selected / affected one accent).
// Same grid as the speaker bars: 104 px name column, the bar, 30 px ratio column. A click
// seeks there in the player. The shell's map dock can take it as its `extraRows`; until then
// the top of SummaryTab shows it (temporarily).
//   SummaryMapRows { vm: summaryTab.vm; player: playerController }
ColumnLayout {
    id: root

    required property var vm           // SummaryViewModel
    property var player: null
    property real labelWidth: 104
    property bool showSections: vm.section === "memo" && vm.sectionBands.length > 0
    property bool showSources: vm.sourceMarks.length > 0
    property bool showHint: false      // help for the control row (only in the temporary strip)

    readonly property real duration: Math.max(1, vm.durationMs)

    spacing: 4

    function seek(ms) {
        if (root.player && ms >= 0) root.player.seek(ms)
    }

    RowLayout {
        visible: root.showSections
        Layout.fillWidth: true
        Layout.preferredHeight: 16
        spacing: 10
        TLabel {
            Layout.preferredWidth: root.labelWidth
            text: qsTr("Szakaszok")
            muted: true
            font.pixelSize: Theme.fontCaption
            font.weight: Theme.weightSemiBold
            elide: Text.ElideRight
        }
        Item {
            id: bandArea
            objectName: "sectionBands"
            Layout.fillWidth: true
            Layout.preferredHeight: 14
            Repeater {
                model: root.showSections ? root.vm.sectionBands : []
                Rectangle {
                    id: band
                    required property var modelData
                    readonly property real x0: bandArea.width * modelData.startMs / root.duration
                    readonly property real x1: bandArea.width * modelData.endMs / root.duration
                    x: x0
                    width: Math.max(4, x1 - x0 - 2)
                    height: 14
                    radius: 3
                    color: modelData.active ? Theme.accent : Theme.sunken
                    Text {
                        anchors.centerIn: parent
                        visible: parent.width > implicitWidth + 4
                        text: band.modelData.label
                        color: band.modelData.active ? Theme.textOnAccent : Theme.textMuted
                        font.family: Theme.fontMono
                        font.pixelSize: 10
                        font.weight: Theme.weightSemiBold
                    }
                    TapHandler {
                        onTapped: {
                            root.vm.activeSection = band.modelData.index
                            root.seek(band.modelData.startMs)
                        }
                    }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                }
            }
        }
        Item { Layout.preferredWidth: 30 }
    }

    RowLayout {
        visible: root.showSources
        Layout.fillWidth: true
        Layout.preferredHeight: 16
        spacing: 10
        TLabel {
            Layout.preferredWidth: root.labelWidth
            text: qsTr("Forrás")
            color: Theme.accent
            font.pixelSize: Theme.fontCaption
            font.weight: Theme.weightSemiBold
            elide: Text.ElideRight
        }
        Item {
            id: markArea
            objectName: "sourceMarks"
            Layout.fillWidth: true
            Layout.preferredHeight: 14
            Repeater {
                model: root.showSources ? root.vm.sourceMarks : []
                Rectangle {
                    id: mark
                    required property var modelData
                    x: markArea.width * modelData.startMs / root.duration
                    width: Math.max(4, markArea.width * (modelData.endMs - modelData.startMs) / root.duration)
                    height: 14
                    radius: 2
                    color: modelData.active ? Theme.accent : Theme.accentLine
                    z: modelData.active ? 1 : 0
                    TapHandler { onTapped: root.seek(mark.modelData.startMs) }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                }
            }
        }
        Item { Layout.preferredWidth: 30 }
    }

    TLabel {
        visible: root.showHint && text !== "" && (root.showSources || root.showSections)
        Layout.leftMargin: root.labelWidth + 10
        text: root.vm.mapHint
        muted: true
        font.pixelSize: Theme.fontCaption
    }
}
