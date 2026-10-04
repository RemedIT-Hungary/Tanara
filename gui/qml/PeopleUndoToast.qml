import QtQuick

// A Személyek ablak alsó, középre igazított értesítése (P03): fordított színű (szöveg-színű
// háttér), opcionális „Visszavonás” gombbal. ~8 mp után eltűnik; amíg az egér rajta áll, marad.
Item {
    id: root

    property string text: ""
    property bool undoable: false
    property bool shown: false
    signal undoRequested()

    function show(text, undoable) {
        root.text = text
        root.undoable = undoable
        root.shown = true
        hideTimer.restart()
    }
    function hide() { root.shown = false; hideTimer.stop() }

    visible: opacity > 0
    opacity: shown ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
    implicitWidth: Math.min(row.implicitWidth + (undoable ? 22 : 28), maxWidth)
    implicitHeight: Math.max(44, label.implicitHeight + 16)
    property real maxWidth: 560

    Timer { id: hideTimer; interval: 8000; onTriggered: root.shown = false }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusPopup
        color: Theme.text
        TShadow { radius: parent.radius }
        HoverHandler { onHoveredChanged: if (root.shown) { hovered ? hideTimer.stop() : hideTimer.restart() } }
    }
    Row {
        id: row
        x: 14
        anchors.verticalCenter: parent.verticalCenter
        spacing: 14
        TLabel {
            id: label
            objectName: "toastText"
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, root.maxWidth - 28 - (root.undoable ? undo.width + 8 : 0))
            text: root.text
            color: Theme.bg
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.Wrap
            maximumLineCount: 3
            elide: Text.ElideRight
        }
        TButton {
            id: undo
            objectName: "toastUndo"
            visible: root.undoable
            anchors.verticalCenter: parent.verticalCenter
            height: 28
            text: qsTr("Visszavonás")
            font.pixelSize: Theme.fontSmall
            font.weight: Theme.weightSemiBold
            leftPadding: 10; rightPadding: 10
            onClicked: { root.hide(); root.undoRequested() }
            background: Rectangle {
                radius: 5
                color: Theme.alpha(Theme.bg, undo.down ? 0.26 : undo.hovered ? 0.2 : 0.12)
                TFocusRing { visible: undo.visualFocus; targetRadius: 5 }
            }
            contentItem: TLabel {
                text: undo.text
                color: Theme.bg
                font: undo.font
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
    }
}
