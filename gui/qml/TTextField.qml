import QtQuick
import QtQuick.Templates as T

// Egysoros beviteli mező (34 px). hasError: danger keret (a hibaszöveget a hívó teszi alá:
// TLabel 12 px, Theme.dangerInk). mono: kulcsokhoz, azonosítókhoz.
T.TextField {
    id: control

    property bool hasError: false
    property bool mono: false
    property bool stateFocused: activeFocus

    implicitWidth: 220
    implicitHeight: Theme.controlHeight
    leftPadding: 10; rightPadding: 10; topPadding: 0; bottomPadding: 0
    verticalAlignment: TextInput.AlignVCenter
    selectByMouse: true

    color: enabled ? Theme.text : Theme.textMuted
    placeholderTextColor: Theme.textMuted
    selectionColor: Theme.accent
    selectedTextColor: Theme.textOnAccent
    font.family: mono ? Theme.fontMono : Theme.fontSans
    font.pixelSize: mono ? Theme.fontSmall : Theme.fontBody

    Text {
        x: control.leftPadding
        width: control.width - control.leftPadding - control.rightPadding
        height: control.height
        visible: !control.length && !control.preeditText
        text: control.placeholderText
        font: control.font
        color: control.placeholderTextColor
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
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
