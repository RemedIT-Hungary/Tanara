import QtQuick
import QtQuick.Templates as T

// Tooltip bubble. Controls (TButton, TIconButton, TStatusIcon...) instantiate it themselves through
// their toolTipText property; for your own item:
//   HoverHandler { id: hh }   TToolTip { visible: hh.hovered; text: qsTr("…") }
T.ToolTip {
    id: control

    // Does the bubble fit INSIDE the window above or below the parent? In a small window (e.g. the
    // recorder's pill mode, 40 px high) it does not: a bubble forced inside the window would slide
    // onto the button and cover it (it could not be clicked). Then it appears as a separate popup
    // window (Popup.Window) that may extend beyond the window; where the platform cannot do this,
    // Qt falls back to Popup.Item, so there it disappears on its own after a short time (timeout).
    readonly property real gap: 6
    readonly property bool fitsAbove: parent ? parent.mapToItem(null, 0, 0).y - implicitHeight - gap >= 0 : true
    readonly property bool fitsBelow: {
        if (!parent) return true
        const win = parent.Window.window
        if (!win) return true
        return parent.mapToItem(null, 0, parent.height).y + gap + implicitHeight <= win.height
    }
    readonly property bool fitsInWindow: fitsAbove || fitsBelow

    x: parent ? Math.round((parent.width - implicitWidth) / 2) : 0
    y: (fitsAbove || !fitsBelow) ? -implicitHeight - gap : (parent ? parent.height + gap : 0)
    implicitWidth: Math.min(320, contentItem.implicitWidth + leftPadding + rightPadding)
    implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
    leftPadding: 8; rightPadding: 8; topPadding: 5; bottomPadding: 5
    margins: 6
    delay: 500
    timeout: fitsInWindow ? -1 : 2500
    popupType: fitsInWindow ? T.Popup.Item : T.Popup.Window
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
