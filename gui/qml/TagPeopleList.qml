import QtQuick
import QtQuick.Templates as T

// The "People" list of the Tags window (successor of "Typical participants"; vault UX plan ch. 2).
// Header: PEOPLE + count + "+ Person". Row (34 px): 26 px avatar, name, "11 / 14" (mono,
// muted), status on the right:
//   applied     -> "# tag" (muted), x on hover (remove from the person);
//   learned only -> dashed "+ Apply" chip, x = "does not belong" (reject).
// "0 / 14 - tag" is a pre-added person not yet seen in any meeting.
// Click on a row: personClicked (the People window opens on that person). When empty, an explanatory line.
//   TagPeopleList { people: vm.detail.people; onAcceptRequested: (n) => vm.addPerson(n) }
Column {
    id: root

    // [{ name, monogram, countText, manual }]
    property var people: []
    property string emptyText: ""
    property bool busy: false
    readonly property int rowHeight: 34
    readonly property int visibleRows: 8
    property alias addButton: addButton

    signal addRequested()
    signal personClicked(string name)
    signal acceptRequested(string name)
    signal rejectRequested(string name)
    signal removeRequested(string name)

    spacing: 8

    Item {
        width: parent.width
        height: 24
        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            TSectionLabel { id: label; text: qsTr("Személyek") }
            TLabel {
                anchors.baseline: label.baseline
                visible: root.people.length > 0
                text: String(root.people.length)
                muted: true
                font.pixelSize: Theme.fontCaption
            }
        }
        TButton {
            id: addButton
            objectName: "tagAddPerson"
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            height: 24
            variant: "ghost"
            text: qsTr("Személy")
            iconName: "plus"
            iconSize: 13
            spacing: 4
            font.pixelSize: Theme.fontCaption
            leftPadding: 6; rightPadding: 8
            toolTipText: qsTr("Személy hozzáadása ehhez a címkéhez")
            onClicked: root.addRequested()
        }
    }

    TLabel {
        objectName: "tagPeopleEmpty"
        visible: root.people.length === 0
        width: parent.width
        text: root.busy ? qsTr("Megbeszélések számolása…") : root.emptyText
        muted: true
        font.pixelSize: Theme.fontSmall
        cssLineHeight: 1.45
        wrapMode: Text.Wrap
    }

    ListView {
        id: list
        objectName: "tagPeopleList"
        visible: count > 0
        width: parent.width
        height: Math.min(count, root.visibleRows) * root.rowHeight
        interactive: count > root.visibleRows
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: root.people
        T.ScrollBar.vertical: TScrollBar {}

        delegate: T.AbstractButton {
            id: row
            required property var modelData
            required property int index
            objectName: "tagPersonRow"
            width: list.width
            height: root.rowHeight
            hoverEnabled: true
            activeFocusOnTab: true
            Accessible.role: Accessible.Link
            Accessible.name: modelData.name
            onClicked: root.personClicked(modelData.name)
            Keys.onReturnPressed: click()

            background: Rectangle {
                radius: Theme.radiusControl
                color: Theme.stateLayer
                opacity: row.down ? Theme.pressedOpacity : row.hovered ? Theme.hoverOpacity : 0
                TFocusRing { visible: row.visualFocus; targetRadius: Theme.radiusControl }
            }

            Rectangle {
                id: avatar
                x: 4
                anchors.verticalCenter: parent.verticalCenter
                width: 26; height: 26; radius: 13
                color: Theme.sunken
                border.width: 1
                border.color: Theme.border
                TLabel {
                    anchors.centerIn: parent
                    text: row.modelData.monogram
                    font.pixelSize: 10
                    font.weight: Theme.weightBold
                }
            }
            TLabel {
                id: nameLabel
                anchors.left: avatar.right
                anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, stateBox.x - x - countLabel.implicitWidth - 20)
                text: row.modelData.name
                font.pixelSize: Theme.fontSmall
                elide: Text.ElideRight
            }
            TLabel {
                id: countLabel
                anchors.left: nameLabel.right
                anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                text: row.modelData.countText
                mono: true
                muted: true
                font.pixelSize: Theme.fontCaption
            }

            // Right side: status.
            Item {
                id: stateBox
                anchors.right: parent.right
                anchors.rightMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                width: row.modelData.manual ? manualRow.width : learnedRow.width
                height: 24

                Row {
                    id: manualRow
                    visible: row.modelData.manual
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4
                    TLabel {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "#"
                        mono: true
                        muted: true
                        font.pixelSize: Theme.fontCaption
                    }
                    TLabel {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("címkéje")
                        muted: true
                        font.pixelSize: Theme.fontCaption
                    }
                    // On hover: remove (always reserves the space so the row does not jump).
                    TIconButton {
                        objectName: "tagPersonRemove"
                        anchors.verticalCenter: parent.verticalCenter
                        width: 22; height: 22
                        variant: "flat"
                        iconName: "x"
                        iconSize: 12
                        iconColor: Theme.textMuted
                        opacity: row.hovered || hovered || visualFocus ? 1 : 0
                        toolTipText: qsTr("Levétel: %1").arg(row.modelData.name)
                        onClicked: root.removeRequested(row.modelData.name)
                    }
                }

                Row {
                    id: learnedRow
                    visible: !row.modelData.manual
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    TagChip {
                        objectName: "tagPersonAccept"
                        kind: "suggested"
                        text: qsTr("Ráteszem")
                        toolTipText: qsTr("A címke rákerül: %1").arg(row.modelData.name)
                        Accessible.name: qsTr("Ráteszem: %1").arg(row.modelData.name)
                        onClicked: root.acceptRequested(row.modelData.name)
                        onRemoveRequested: root.rejectRequested(row.modelData.name)
                    }
                }
            }
        }
    }
}
