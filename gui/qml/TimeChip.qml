import QtQuick
import QtQuick.Templates as T

// Monospace time chip for a source reference or a memo section's time range (with a play
// icon). Default: accentSoft background + accent label; active (the selected claim's):
// accent + textOnAccent; warn (the speaker changed in its source): warnSoft + warnInk + warnLine.
//   TimeChip { text: "12:41"; warn: true; onClicked: … }
//   TimeChip { text: "31:10–44:05"; iconName: "play"; large: true }
T.AbstractButton {
    id: control

    property bool active: false
    property bool warn: false
    property string iconName: ""
    property bool large: false              // 20 px high, 11.5 font (section header)
    property string toolTipText: ""
    property bool stateHovered: hovered
    property bool stateFocused: visualFocus

    readonly property color ink: active ? Theme.textOnAccent : warn ? Theme.warnInk : Theme.accent

    implicitHeight: large ? 20 : 18
    implicitWidth: row.implicitWidth + leftPadding + rightPadding
    leftPadding: large ? 7 : 5
    rightPadding: large ? 7 : 5
    hoverEnabled: true
    activeFocusOnTab: true
    focusPolicy: Qt.StrongFocus
    font.family: Theme.fontMono
    font.pixelSize: large ? 12 : 11
    font.weight: Theme.weightMedium

    Accessible.role: Accessible.Button
    Accessible.name: toolTipText !== "" ? toolTipText : text
    Keys.onReturnPressed: click()
    Keys.onEnterPressed: click()

    background: Rectangle {
        radius: 4
        color: control.active ? Theme.accent : control.warn ? Theme.warnSoft : Theme.accentSoft
        border.width: control.warn && !control.active ? 1 : 0
        border.color: Theme.warnLine
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: Theme.stateLayer
            opacity: control.down ? Theme.pressedOpacity : control.stateHovered ? Theme.hoverOpacity : 0
        }
        TFocusRing { visible: control.stateFocused; targetRadius: 4 }
    }
    contentItem: Item {
        implicitWidth: row.implicitWidth
        implicitHeight: row.implicitHeight
        Row {
            id: row
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4
            TIcon {
                visible: control.iconName !== ""
                name: control.iconName
                size: 11
                color: control.ink
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: control.text
                font: control.font
                color: control.ink
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    TToolTip {
        visible: control.toolTipText !== "" && control.hovered
        text: control.toolTipText
    }
}
