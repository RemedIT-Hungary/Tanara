import QtQuick

// A Címkék ablak listájának egy sora (T10): 28 px-es sunken csempe „#” jellel, név 14/500
// (kijelölve 600, accentSoft háttér; a keresés találata kiemelve), alatta „14 megbeszélés · okt. 2.”.
Item {
    id: root

    property string name: ""
    property string meta: ""
    property string before: ""
    property string match: ""
    property string after: ""
    property bool selected: false
    signal clicked()

    implicitHeight: 47
    Accessible.role: Accessible.ListItem
    Accessible.name: name
    Accessible.selected: selected

    Rectangle {
        anchors.fill: parent
        anchors.topMargin: 1
        anchors.bottomMargin: 1
        radius: Theme.radiusControl
        color: root.selected ? Theme.accentSoft : "transparent"

        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: Theme.stateLayer
            opacity: root.selected ? 0 : tap.pressed ? Theme.pressedOpacity : hover.hovered ? Theme.hoverOpacity : 0
        }
        HoverHandler { id: hover }
        TapHandler { id: tap; onTapped: root.clicked() }

        Rectangle {
            id: tile
            x: 8
            anchors.verticalCenter: parent.verticalCenter
            width: 28; height: 28
            radius: Theme.radiusControl
            color: Theme.sunken
            TLabel {
                anchors.centerIn: parent
                text: "#"
                mono: true
                muted: true
                font.pixelSize: Theme.fontSmall
                font.weight: Theme.weightMedium
            }
        }

        Column {
            anchors.left: tile.right
            anchors.leftMargin: 10
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2

            Row {
                width: parent.width
                clip: true
                TLabel {
                    text: root.match !== "" ? root.before : root.name
                    font.weight: root.selected ? Theme.weightSemiBold : Theme.weightMedium
                    elide: root.match !== "" ? Text.ElideNone : Text.ElideRight
                    width: root.match !== "" ? implicitWidth : Math.min(implicitWidth, parent.width)
                }
                Rectangle {
                    visible: root.match !== ""
                    width: matchLabel.implicitWidth
                    height: matchLabel.implicitHeight
                    radius: 2
                    color: Theme.warnSoft
                    TLabel {
                        id: matchLabel
                        text: root.match
                        font.weight: root.selected ? Theme.weightSemiBold : Theme.weightMedium
                    }
                }
                TLabel {
                    visible: root.match !== ""
                    text: root.after
                    font.weight: root.selected ? Theme.weightSemiBold : Theme.weightMedium
                }
            }
            TLabel {
                width: parent.width
                text: root.meta
                muted: true
                font.pixelSize: Theme.fontCaption
                elide: Text.ElideRight
            }
        }
    }
}
