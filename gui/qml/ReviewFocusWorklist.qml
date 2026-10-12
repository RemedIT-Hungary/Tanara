import QtQuick
import QtQuick.Layouts

// A munkalista időrendi csíkja az Átnézendő fókusz-nézet alján: egy cella egy sor, a lista
// sorrendjében (■ eldöntve a beszélő színével · halványabb: nem minta · □ hátra · ◇ később ·
// ? nem azonosítható · ✓ már eldőlt). A kurzor cellája keretes; kattintás: oda ugrik.
//   queue: TranscriptEditorViewModel.reviewQueue (ReviewQueueModel)
Rectangle {
    id: root

    required property var queue
    signal jump(int index)

    readonly property int count: queue ? queue.count : 0
    // Cella-szélesség: sok sornál keskenyebb (a csík nem görget, mindig az egész kör látszik).
    readonly property real cellWidth: count > 0 ? Math.max(3, Math.min(14, (strip.width + 3) / count - 3)) : 14

    objectName: "reviewFocusWorklist"
    implicitHeight: 44
    color: Theme.surface

    TDivider { anchors { left: parent.left; right: parent.right; top: parent.top } }

    RowLayout {
        anchors { fill: parent; leftMargin: 24; rightMargin: 16 }
        spacing: 12
        TSectionLabel { text: qsTr("Munkalista") }
        Item {
            id: strip
            Layout.fillWidth: true
            Layout.preferredHeight: 24
            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: root.cellWidth >= 6 ? 3 : 1
                Repeater {
                    model: root.queue
                    delegate: Item {
                        id: cell
                        required property int index
                        required property var model
                        readonly property string rowState: model.state || ""
                        readonly property bool current: model.current === true
                        readonly property int colorIndex: model.colorIndex || 0
                        readonly property string sampleState: model.sampleState || ""
                        objectName: "reviewFocusCell"
                        width: root.cellWidth
                        height: 18
                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: -3
                            radius: 4
                            color: "transparent"
                            border.width: 2
                            border.color: Theme.accent
                            visible: cell.current
                        }
                        Rectangle {
                            anchors.fill: parent
                            radius: 2
                            visible: cell.rowState !== "later"
                            color: cell.rowState === "decided" ? Theme.speakerLine(cell.colorIndex)
                                 : cell.rowState === "ignored" ? Theme.sunken
                                 : cell.rowState === "alreadyDecided" ? Theme.successSoft : "transparent"
                            border.width: cell.rowState === "decided" ? 0 : 1
                            border.color: cell.rowState === "alreadyDecided" ? Theme.successLine : Theme.borderStrong
                            opacity: cell.rowState === "decided" && cell.sampleState === "noSample" ? 0.5 : 1
                            TLabel {
                                anchors.centerIn: parent
                                visible: root.cellWidth >= 9 && (cell.rowState === "ignored" || cell.rowState === "alreadyDecided")
                                text: cell.rowState === "ignored" ? "?" : "✓"
                                color: cell.rowState === "ignored" ? Theme.textMuted : Theme.successInk
                                font.pixelSize: 10
                                font.weight: Theme.weightBold
                            }
                        }
                        Rectangle {
                            visible: cell.rowState === "later"
                            anchors.centerIn: parent
                            width: Math.min(9, root.cellWidth)
                            height: width
                            rotation: 45
                            color: "transparent"
                            border.width: 1.5
                            border.color: Theme.textMuted
                        }
                        HoverHandler { id: cellHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: root.jump(cell.index) }
                        TToolTip {
                            visible: cellHover.hovered
                            text: (cell.model.timeLabel || "") + " · " + (cell.model.speakerName || "")
                        }
                    }
                }
            }
        }
        Row {
            spacing: 12
            Repeater {
                model: [
                    { kind: "decided", text: qsTr("eldöntve") }, { kind: "open", text: qsTr("hátra") },
                    { kind: "later", text: qsTr("később") }, { kind: "ignored", text: qsTr("nem azon.") }
                ]
                delegate: Row {
                    required property var modelData
                    spacing: 5
                    Item {
                        width: 10
                        height: 12
                        anchors.verticalCenter: parent.verticalCenter
                        Rectangle {
                            anchors.fill: parent
                            radius: 2
                            visible: modelData.kind !== "later"
                            color: modelData.kind === "decided" ? Theme.speakerLine(1)
                                 : modelData.kind === "ignored" ? Theme.sunken : "transparent"
                            border.width: modelData.kind === "decided" ? 0 : 1
                            border.color: Theme.borderStrong
                        }
                        Rectangle {
                            visible: modelData.kind === "later"
                            anchors.centerIn: parent
                            width: 7
                            height: 7
                            rotation: 45
                            color: "transparent"
                            border.width: 1.5
                            border.color: Theme.textMuted
                        }
                    }
                    TLabel {
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.text
                        muted: true
                        font.pixelSize: Theme.fontCaption
                    }
                }
            }
        }
    }
}
