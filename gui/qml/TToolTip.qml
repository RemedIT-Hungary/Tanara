import QtQuick
import QtQuick.Templates as T

// Buborék-súgó. A vezérlők (TButton, TIconButton, TStatusIcon…) a toolTipText property-n
// át maguk példányosítják; saját elemhez:
//   HoverHandler { id: hh }   TToolTip { visible: hh.hovered; text: qsTr("…") }
T.ToolTip {
    id: control

    x: parent ? Math.round((parent.width - implicitWidth) / 2) : 0
    y: -implicitHeight - 6
    implicitWidth: Math.min(320, contentItem.implicitWidth + leftPadding + rightPadding)
    implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
    leftPadding: 8; rightPadding: 8; topPadding: 5; bottomPadding: 5
    margins: 6
    delay: 500
    timeout: -1
    popupType: T.Popup.Item
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutsideParent | T.Popup.CloseOnReleaseOutsideParent

    contentItem: Text {
        text: control.text
        color: Theme.bg
        font.family: Theme.fontSans
        font.pixelSize: Theme.fontCaption
        wrapMode: Text.Wrap
    }
    background: Rectangle {
        color: Theme.text
        radius: 5
    }
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationFast } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationFast } }
}
