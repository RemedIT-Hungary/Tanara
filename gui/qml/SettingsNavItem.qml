import QtQuick
import QtQuick.Templates as T

// A Beállítások bal oldali navigációjának egy eleme: 36 px, sugár 6, 16 px ikon + 14 px
// felirat. Aktív: accentSoft háttér, 600; inaktív: halvány. `warn`: 8 px-es figyelmeztető
// pötty a sor végén (pl. hiányzó szolgáltató).
T.AbstractButton {
    id: control

    property string iconName: ""
    property bool current: false
    property bool warn: false
    property string warnText: ""

    implicitWidth: 188
    implicitHeight: 36
    hoverEnabled: true
    activeFocusOnTab: true
    Accessible.role: Accessible.PageTab
    Accessible.name: warn && warnText !== "" ? text + " — " + warnText : text
    Keys.onReturnPressed: click()
    Keys.onSpacePressed: click()

    background: Rectangle {
        radius: Theme.radiusControl
        color: control.current ? Theme.accentSoft
             : control.hovered ? Theme.alpha(Theme.stateLayer, Theme.hoverOpacity) : "transparent"
        TFocusRing { visible: control.visualFocus }
    }
    contentItem: Item {
        TIcon {
            x: 10
            anchors.verticalCenter: parent.verticalCenter
            name: control.iconName
            size: 16
            color: control.current ? Theme.text : Theme.textMuted
        }
        TLabel {
            x: 36
            width: parent.width - 36 - (dot.visible ? 22 : 10)
            anchors.verticalCenter: parent.verticalCenter
            text: control.text
            color: control.current ? Theme.text : Theme.textMuted
            font.weight: control.current ? Theme.weightSemiBold : Theme.weightRegular
            elide: Text.ElideRight
        }
        Rectangle {
            id: dot
            visible: control.warn
            width: 8; height: 8; radius: 4
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.warn
        }
    }
    TToolTip {
        visible: control.warn && control.warnText !== "" && control.hovered
        text: control.warnText
    }
}
