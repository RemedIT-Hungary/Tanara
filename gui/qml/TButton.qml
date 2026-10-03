import QtQuick
import QtQuick.Templates as T

// Gomb. variant: "primary" | "secondary" | "ghost" | "danger" (teli, megerősítő romboló
// művelet) | "dangerSoft" | "dangerGhost" | "record" (csak felvétel-indításra).
// size: "normal" (34 px) | "small" (28 px, eszköztár). Opcionális ikon elöl / hátul.
//   TButton { text: qsTr("Átírás indítása"); variant: "primary"; iconName: "audio-lines" }
T.Button {
    id: control

    property string variant: "secondary"
    property string size: "normal"
    property string iconName: ""
    property string trailingIconName: ""
    property real iconSize: small ? 15 : 16
    property real radius: Theme.radiusControl
    property bool muted: false                       // ghost: halvány felirat (oldalsáv-lábléc)
    property int horizontalAlignment: Qt.AlignHCenter
    property string toolTipText: ""
    // Állapot-felülbírálás (a galéria ezzel mutatja a hover / fókusz állapotot).
    property bool stateHovered: hovered
    property bool stateFocused: visualFocus

    readonly property bool small: size === "small"
    readonly property bool solid: variant === "primary" || variant === "danger" || variant === "record"
    readonly property bool ghost: variant === "ghost" || variant === "dangerGhost"
    readonly property color foreground: {
        if (!enabled) return Theme.textMuted
        switch (variant) {
        case "primary": return Theme.textOnAccent
        case "danger":
        case "record": return "#ffffff"
        case "dangerSoft":
        case "dangerGhost": return Theme.dangerInk
        default: return muted ? Theme.textMuted : Theme.text
        }
    }
    readonly property color fill: {
        if (!enabled) return ghost ? "transparent" : Theme.sunken
        switch (variant) {
        case "primary": return Theme.accent
        case "danger": return Theme.danger
        case "record": return Theme.rec
        case "dangerSoft": return Theme.dangerSoft
        case "secondary": return Theme.raised
        default: return "transparent"
        }
    }

    implicitHeight: small ? Theme.controlHeightSmall : Theme.controlHeight
    implicitWidth: Math.max(implicitHeight, implicitContentWidth + leftPadding + rightPadding)
    leftPadding: small ? 10 : 14
    rightPadding: small ? 10 : 14
    spacing: variant === "record" ? 9 : 8
    hoverEnabled: true
    opacity: !enabled && ghost ? 0.6 : 1

    font.family: Theme.fontSans
    font.pixelSize: small ? Theme.fontSmall : Theme.fontBody
    font.weight: solid || variant === "dangerSoft" ? Theme.weightSemiBold : Theme.weightMedium

    Keys.onReturnPressed: click()
    Keys.onEnterPressed: click()

    contentItem: Item {
        implicitWidth: row.implicitWidth
        implicitHeight: row.implicitHeight
        Row {
            id: row
            spacing: control.spacing
            anchors.verticalCenter: parent.verticalCenter
            x: control.horizontalAlignment === Qt.AlignLeft ? 0
             : control.horizontalAlignment === Qt.AlignRight ? parent.width - width
             : Math.round((parent.width - width) / 2)
            Rectangle {
                visible: control.variant === "record"
                width: 10; height: 10; radius: 5
                color: control.foreground
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
                font: control.font
                color: control.foreground
                anchors.verticalCenter: parent.verticalCenter
            }
            TIcon {
                visible: control.trailingIconName !== ""
                name: control.trailingIconName
                size: control.iconSize
                color: control.foreground
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    background: Rectangle {
        radius: control.radius
        color: control.fill
        border.width: control.variant === "secondary" && control.enabled ? 1 : 0
        border.color: Theme.borderStrong

        // Letiltva: olvasható felirat süllyesztett háttéren, szaggatott kerettel.
        TDashedRect {
            anchors.fill: parent
            visible: !control.enabled && !control.ghost
            radius: control.radius
        }
        Rectangle {
            anchors.fill: parent
            radius: control.radius
            color: Theme.stateLayer
            visible: control.enabled
            opacity: control.down ? Theme.pressedOpacity : control.stateHovered ? Theme.hoverOpacity : 0
            Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
        }
        TFocusRing { visible: control.stateFocused; targetRadius: control.radius }
    }

    TToolTip {
        visible: control.toolTipText !== "" && control.hovered
        text: control.toolTipText
    }
}
