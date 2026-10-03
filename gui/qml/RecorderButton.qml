import QtQuick
import QtQuick.Templates as T

// A felvevő gombjai (a design saját méreteivel). kind:
//   "inverted"  — text háttér / bg felirat (Leállítás: semleges, NEM piros)
//   "primary"   — accent        "secondary" — raised + borderStrong keret
//   "outline"   — csak keret    "dangerOutline" — dangerLine keret, dangerInk felirat
//   "ghost"     — keret nélkül  "record" — rec háttér, fehér felirat
// mark: "" | "square" (leállítás-jel) | "dot" (felvétel-pötty, markColor színnel)
T.Button {
    id: control
    property string kind: "secondary"
    property string iconName: ""
    property real iconSize: 14
    property string mark: ""
    property real markSize: 10
    property color markColor: foreground
    property string hint: ""               // billentyű-tipp a felirat után (mono)
    property real radius: 6
    property real fontSize: 13
    property int fontWeight: Theme.weightSemiBold
    property string toolTipText: ""

    readonly property color foreground: !enabled ? Theme.textMuted
        : kind === "inverted" ? Theme.bg
        : kind === "primary" ? Theme.textOnAccent
        : kind === "record" ? "#ffffff"
        : kind === "dangerOutline" ? Theme.dangerInk : Theme.text
    readonly property color fill: !enabled && (kind === "inverted" || kind === "record") ? Theme.sunken
        : kind === "inverted" ? Theme.text
        : kind === "primary" ? Theme.accent
        : kind === "record" ? Theme.rec
        : kind === "secondary" ? Theme.raised : "transparent"

    implicitHeight: 32
    implicitWidth: row.implicitWidth + leftPadding + rightPadding
    leftPadding: 12
    rightPadding: 12
    hoverEnabled: true
    Accessible.name: text !== "" ? text : toolTipText

    Keys.onReturnPressed: click()
    Keys.onEnterPressed: click()

    contentItem: Item {
        Row {
            id: row
            anchors.centerIn: parent
            spacing: control.mark === "dot" && control.kind === "record" ? 10 : 7
            Rectangle {
                visible: control.mark !== ""
                width: control.markSize; height: control.markSize
                radius: control.mark === "dot" ? width / 2 : 2
                color: control.enabled ? control.markColor : Theme.textMuted
                anchors.verticalCenter: parent.verticalCenter
            }
            TIcon {
                visible: control.iconName !== ""
                name: control.iconName
                size: control.iconSize
                color: control.foreground
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                visible: text !== ""
                text: control.text
                color: control.foreground
                font.family: Theme.fontSans
                font.pixelSize: control.fontSize
                font.weight: control.fontWeight
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                visible: control.hint !== ""
                text: control.hint
                color: control.foreground
                opacity: 0.8
                font.family: Theme.fontMono
                font.pixelSize: 11
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    background: Rectangle {
        radius: control.radius
        color: control.fill
        border.width: control.kind === "secondary" || control.kind === "outline"
                      || control.kind === "dangerOutline" ? 1 : 0
        border.color: control.kind === "dangerOutline" ? Theme.dangerLine : Theme.borderStrong
        Rectangle {
            anchors.fill: parent
            radius: control.radius
            color: control.kind === "inverted" ? Theme.bg : Theme.stateLayer
            visible: control.enabled
            opacity: control.down ? Theme.pressedOpacity * 1.5
                   : control.hovered ? Theme.hoverOpacity * 1.5 : 0
        }
        TFocusRing { visible: control.visualFocus; targetRadius: control.radius }
    }

    TToolTip {
        visible: control.toolTipText !== "" && control.hovered
        text: control.toolTipText
    }
}
