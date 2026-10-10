import QtQuick
import QtQuick.Layouts

// A térkép-dokk összefoglaló-sorai (S1–S3): „Szakaszok” (memó: a szakaszok sávként, a kiemelt
// accent) és „Forrás” (az állítások forrásai: accentLine, a kijelölt / érintett accent).
// A beszélő-sávokkal azonos rácsban: 104 px név-oszlop, a sáv, 30 px arány-oszlop. Kattintás
// = ugrás oda a lejátszóban. Az U1 térkép-dokkja az `extraRows` helyére teheti; addig a
// SummaryTab teteje mutatja (ideiglenesen).
//   SummaryMapRows { vm: summaryTab.vm; player: playerController }
ColumnLayout {
    id: root

    required property var vm           // SummaryViewModel
    property var player: null
    property real labelWidth: 104
    property bool showSections: vm.section === "memo" && vm.sectionBands.length > 0
    property bool showSources: vm.sourceMarks.length > 0
    property bool showHint: false      // a vezérlősor súgója (csak az ideiglenes csíkban)

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
