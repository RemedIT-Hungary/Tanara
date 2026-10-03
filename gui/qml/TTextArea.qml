import QtQuick
import QtQuick.Templates as T

// Többsoros mező (pl. „Miről szólt a megbeszélés?”). A tartalommal nő; hosszú szöveghez
// tedd Flickable-be (TextArea.flickable).
T.TextArea {
    id: control

    property bool hasError: false
    property bool stateFocused: activeFocus

    implicitWidth: 320
    implicitHeight: Math.max(64, contentHeight + topPadding + bottomPadding)
    leftPadding: 12; rightPadding: 12; topPadding: 10; bottomPadding: 10
    wrapMode: TextEdit.Wrap
    selectByMouse: true

    color: enabled ? Theme.text : Theme.textMuted
    placeholderTextColor: Theme.textMuted
    selectionColor: Theme.accent
    selectedTextColor: Theme.textOnAccent
    font.family: Theme.fontSans
    font.pixelSize: Theme.fontBody

    Text {
        x: control.leftPadding
        y: control.topPadding
        width: control.width - control.leftPadding - control.rightPadding
        visible: !control.length && !control.preeditText
        text: control.placeholderText
        font: control.font
        color: control.placeholderTextColor
        wrapMode: Text.Wrap
    }

    background: Rectangle {
        radius: Theme.radiusControl
        color: control.enabled ? Theme.raised : Theme.sunken
        border.width: 1
        border.color: control.hasError ? Theme.danger
                    : control.stateFocused ? Theme.accent
                    : control.enabled ? Theme.borderStrong : Theme.border
        TFocusRing {
            visible: control.stateFocused
            border.color: control.hasError ? Theme.dangerSoft : Theme.accentSoft
        }
    }
}
