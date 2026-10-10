import QtQuick
import QtQuick.Templates as T

// Buborék-súgó. A vezérlők (TButton, TIconButton, TStatusIcon…) a toolTipText property-n
// át maguk példányosítják; saját elemhez:
//   HoverHandler { id: hh }   TToolTip { visible: hh.hovered; text: qsTr("…") }
T.ToolTip {
    id: control

    // Elfér-e a buborék az ablakon BELÜL a szülő fölött vagy alatt? Kis ablakban (pl. a
    // felvevő pirula-módja, 40 px magas) nem: ott az ablakon belülre kényszerített buborék a
    // gombra csúszna és kitakarná (nem lehet rákattintani). Ilyenkor külön popup-ablakként
    // jelenik meg (Popup.Window), ami kilóghat az ablakból; ahol ezt a platform nem tudja,
    // a Qt visszaesik Popup.Item-re, ezért ott rövid idő után magától eltűnik (timeout).
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
