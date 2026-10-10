import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// A memó tartalomjegyzéke (S2): 236 px oszlop, jobb oldali elválasztóval. Szakaszonként
// „N. cím” + egyenközű időtartomány; a kiemelt (aktív) szakasz accentSoft háttérrel, 600-as
// betűvel. Kattintás / Enter: picked(index).
//   MemoToc { sections: vm.memo; activeIndex: vm.activeSection; onPicked: (i) => … }
Rectangle {
    id: root

    property var sections: []          // vm.memo: [{ title, sourceRange, … }]
    property int activeIndex: -1
    signal picked(int index)

    implicitWidth: 236
    color: "transparent"

    Rectangle {
        anchors { top: parent.top; bottom: parent.bottom; right: parent.right }
        width: 1
        color: Theme.border
    }

    ListView {
        id: list
        objectName: "memoToc"
        anchors { fill: parent; leftMargin: 12; rightMargin: 13; topMargin: 16; bottomMargin: 12 }
        clip: true
        spacing: 2
        boundsBehavior: Flickable.StopAtBounds
        model: root.sections
        currentIndex: root.activeIndex
        T.ScrollBar.vertical: TScrollBar {}
        header: TLabel {
            width: list.width
            leftPadding: 8
            bottomPadding: 8
            text: qsTr("Tartalom · %n szakasz", "", root.sections.length).toUpperCase()
            muted: true
            font.pixelSize: Theme.fontCaption
            font.weight: Theme.weightSemiBold
            font.letterSpacing: 0.69
            elide: Text.ElideRight
        }
        delegate: T.AbstractButton {
            id: entry
            required property var modelData
            required property int index
            readonly property bool active: index === root.activeIndex
            objectName: "memoTocEntry"
            width: list.width
            implicitHeight: entryCol.implicitHeight + 14
            hoverEnabled: true
            activeFocusOnTab: true
            Accessible.name: (index + 1) + ". " + modelData.title
            onClicked: root.picked(index)
            Keys.onReturnPressed: click()
            background: Rectangle {
                radius: Theme.radiusControl
                color: entry.active ? Theme.accentSoft : "transparent"
                Rectangle {
                    anchors.fill: parent
                    radius: parent.radius
                    color: Theme.stateLayer
                    opacity: entry.active ? 0 : entry.down ? Theme.pressedOpacity : entry.hovered ? Theme.hoverOpacity : 0
                }
                TFocusRing { visible: entry.visualFocus }
            }
            contentItem: Item {
                ColumnLayout {
                    id: entryCol
                    anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter
                              leftMargin: 8; rightMargin: 8 }
                    spacing: 2
                    TLabel {
                        Layout.fillWidth: true
                        text: (entry.index + 1) + ". " + (entry.modelData.title !== "" ? entry.modelData.title : qsTr("Egyéb"))
                        font.pixelSize: Theme.fontSmall
                        font.weight: entry.active ? Theme.weightSemiBold : Theme.weightRegular
                        elide: Text.ElideRight
                    }
                    TLabel {
                        visible: text !== ""
                        Layout.fillWidth: true
                        text: entry.modelData.sourceRange || ""
                        mono: true; muted: true
                        font.pixelSize: Theme.fontMicro
                    }
                }
            }
        }
    }
}
