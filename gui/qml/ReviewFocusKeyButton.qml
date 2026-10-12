import QtQuick
import QtQuick.Templates as T

// Az Átnézendő fókusz-nézet (ReviewFocusView) döntés-gombja billentyű-felirattal:
// [Enter  Fehér Gábor mondta]. nonSample: a Shift lenyomva → „· nem minta” utótag (ha a hely
// engedi: showSuffix) és szaggatott keret — a minta-ítélet így a gomb előtt látszik.
// A gomb sosem veszi el a fókuszt (a billentyűk a szerkesztőé maradnak).
T.Button {
    id: control

    property string keyText: ""
    property string variant: "secondary"      // primary | secondary | ghost
    property bool nonSample: false
    property bool showSuffix: true
    property bool dimmed: false               // halvány (pl. „Jó így”, ha a javaslat az alapértelmezés)
    property string toolTipText: ""
    readonly property string label: text + (nonSample && showSuffix ? qsTr(" · nem minta") : "")

    readonly property bool primary: variant === "primary"
    readonly property color foreground: !enabled ? Theme.textMuted
                                      : primary ? Theme.textOnAccent : Theme.text

    implicitHeight: Theme.controlHeight
    implicitWidth: row.implicitWidth + 20
    hoverEnabled: true
    focusPolicy: Qt.NoFocus
    opacity: dimmed ? 0.5 : 1
    font.family: Theme.fontSans
    font.pixelSize: Theme.fontBody
    font.weight: primary ? Theme.weightSemiBold : Theme.weightMedium

    contentItem: Item {
        Row {
            id: row
            anchors.centerIn: parent
            spacing: 8
            Rectangle {
                visible: control.keyText !== ""
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(20, keyLabel.implicitWidth + 10)
                height: 20
                radius: 4
                color: control.primary ? Theme.alpha(Theme.textOnAccent, 0.18) : Theme.sunken
                border.width: 1
                border.color: control.primary ? Theme.alpha(Theme.textOnAccent, 0.35) : Theme.border
                Text {
                    id: keyLabel
                    anchors.centerIn: parent
                    text: control.keyText
                    color: control.foreground
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontMicro
                    font.weight: Theme.weightSemiBold
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: control.label
                font: control.font
                color: control.foreground
            }
        }
    }

    background: Item {
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusControl
            color: !control.enabled ? Theme.sunken
                 : control.primary ? Theme.accent
                 : control.variant === "ghost" ? "transparent" : Theme.raised
            border.width: control.variant === "secondary" && !control.nonSample ? 1 : 0
            border.color: Theme.borderStrong
        }
        TDashedRect {
            anchors.fill: parent
            visible: control.nonSample && control.variant !== "ghost"
            radius: Theme.radiusControl
            color: control.primary ? Theme.textOnAccent : Theme.borderStrong
            lineWidth: 1.5
        }
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusControl
            color: Theme.stateLayer
            opacity: control.down ? Theme.pressedOpacity : control.hovered ? Theme.hoverOpacity : 0
        }
    }

    TToolTip {
        visible: control.toolTipText !== "" && control.hovered
        text: control.toolTipText
    }
}
