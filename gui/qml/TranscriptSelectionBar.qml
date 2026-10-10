import QtQuick

// A kijelölés sötét alsó sávja: „3 sor kijelölve · Áthelyezés:" + beszélő-chipek a
// billentyűjükkel (1, 2 …), „Új személy…" (utána: hozzá hasonló sorok), „Mégse". Fordított színekkel (text háttér).
Rectangle {
    id: root

    required property var vm                // TranscriptEditorViewModel
    readonly property alias newParticipantItem: newChip

    signal newParticipantRequested()

    implicitHeight: 44
    radius: Theme.radiusPopup
    color: Theme.text

    TShadow { radius: root.radius }

    TLabel {
        id: countLabel
        x: 14
        anchors.verticalCenter: parent.verticalCenter
        text: qsTr("%n sor kijelölve", "", root.vm.selectedCount)
        color: Theme.bg
        font.weight: Theme.weightSemiBold
    }
    TLabel {
        id: moveLabel
        anchors.left: countLabel.right
        anchors.leftMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        text: qsTr("Áthelyezés:")
        color: Theme.bg
        opacity: 0.75
        font.pixelSize: Theme.fontSmall
    }

    // A chipek vízszintesen görgethetők, ha nem férnek ki (sok beszélő).
    Flickable {
        anchors.left: moveLabel.right
        anchors.leftMargin: 10
        anchors.right: cancel.left
        anchors.rightMargin: 8
        height: 36
        anchors.verticalCenter: parent.verticalCenter
        contentWidth: chips.width
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Row {
            id: chips
            y: 4
            spacing: 6
            Repeater {
                model: root.vm.lanes
                Rectangle {
                    id: chip
                    required property var modelData
                    required property int index
                    height: 28
                    width: chipRow.width + 14
                    radius: 14
                    color: Theme.speakerSoft(modelData.colorIndex)
                    border.width: chipHover.hovered ? 2 : 0
                    border.color: Theme.speakerLine(modelData.colorIndex)
                    Row {
                        id: chipRow
                        x: 4
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 6
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 20; height: 20; radius: 10
                            color: Theme.speakerLine(chip.modelData.colorIndex)
                        }
                        TLabel {
                            anchors.verticalCenter: parent.verticalCenter
                            text: chip.modelData.name
                            color: Theme.speakerInk(chip.modelData.colorIndex)
                            font.pixelSize: Theme.fontSmall
                            font.weight: Theme.weightSemiBold
                        }
                        TLabel {
                            visible: chip.index < 9
                            anchors.verticalCenter: parent.verticalCenter
                            text: chip.index + 1
                            mono: true
                            color: Theme.speakerInk(chip.modelData.colorIndex)
                            opacity: 0.7
                            font.pixelSize: Theme.fontMicro
                        }
                    }
                    HoverHandler { id: chipHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: root.vm.moveSelectionToSpeaker(chip.modelData.key) }
                }
            }
            Item {
                id: newChip
                height: 28
                width: newRow.width + 20
                opacity: 0.85
                TDashedRect { anchors.fill: parent; radius: 14; color: Theme.bg }
                Row {
                    id: newRow
                    x: 10
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 5
                    TIcon { anchors.verticalCenter: parent.verticalCenter; name: "plus"; size: 13; color: Theme.bg }
                    TLabel {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Új személy…")
                        color: Theme.bg
                        font.pixelSize: Theme.fontSmall
                    }
                }
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: root.newParticipantRequested() }
            }
        }
    }

    Item {
        id: cancel
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        width: cancelLabel.width + 20
        height: 28
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusControl
            color: Theme.bg
            opacity: cancelHover.hovered ? 0.14 : 0
        }
        TLabel {
            id: cancelLabel
            anchors.centerIn: parent
            text: qsTr("Mégse")
            color: Theme.bg
            font.pixelSize: Theme.fontSmall
            font.weight: Theme.weightMedium
        }
        HoverHandler { id: cancelHover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: root.vm.clearSelection() }
    }
}
