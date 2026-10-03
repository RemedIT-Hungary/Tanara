import QtQuick
import QtQuick.Templates as T

// Keresőmező (32 px): süllyesztett háttér, nagyító, jobb oldalt billentyű-tipp (hint),
// szöveg esetén törlő ×. Fókuszban raised háttér + accent keret + gyűrű.
//   TSearchField { placeholderText: qsTr("Keresés"); hint: "Ctrl+F" }
T.TextField {
    id: control

    property string hint: ""
    property bool stateFocused: activeFocus

    implicitWidth: 220
    implicitHeight: 32
    leftPadding: 33
    rightPadding: clearButton.visible ? 30 : hintLabel.visible ? hintLabel.width + 18 : 10
    topPadding: 0; bottomPadding: 0
    verticalAlignment: TextInput.AlignVCenter
    selectByMouse: true

    color: Theme.text
    placeholderTextColor: Theme.textMuted
    selectionColor: Theme.accent
    selectedTextColor: Theme.textOnAccent
    font.family: Theme.fontSans
    font.pixelSize: Theme.fontSmall

    Keys.onEscapePressed: event => { if (length > 0) clear(); else event.accepted = false }

    TIcon {
        x: 10
        anchors.verticalCenter: parent.verticalCenter
        name: "search"
        size: 15
        color: Theme.textMuted
    }
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
    Text {
        id: hintLabel
        visible: control.hint !== "" && !control.length && !control.stateFocused
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        text: control.hint
        color: Theme.textMuted
        font.family: Theme.fontMono
        font.pixelSize: Theme.fontMicro
    }
    Item {
        id: clearButton
        visible: control.length > 0
        width: 24; height: 24
        anchors.right: parent.right
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        TIcon { anchors.centerIn: parent; name: "x"; size: 14; color: Theme.textMuted }
        TapHandler { onTapped: { control.clear(); control.forceActiveFocus() } }
    }

    background: Rectangle {
        radius: Theme.radiusControl
        color: control.stateFocused ? Theme.raised : Theme.sunken
        border.width: 1
        border.color: control.stateFocused ? Theme.accent : Theme.border
        TFocusRing { visible: control.stateFocused; border.color: Theme.accentSoft }
    }
}
