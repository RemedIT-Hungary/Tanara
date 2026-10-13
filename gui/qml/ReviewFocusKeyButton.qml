import QtQuick
import QtQuick.Templates as T

// Az Átnézendő fókusz-nézet (ReviewFocusView) döntés-gombja billentyű-felirattal:
// [Enter  Fehér Gábor mondta]. nonSample: a Shift lenyomva → „· nem minta” utótag (ha a hely
// engedi: showSuffix) és szaggatott keret — a minta-ítélet így a gomb előtt látszik.
// A gomb sosem veszi el a fókuszt (a billentyűk a szerkesztőé maradnak).
// S7 (a fókusz-kártya fix gomb-sora): variant "calm" — színtelen gomb (panel + keret), csak az
// ikon színes; `iconName` / `iconColor` a művelet ikonja (→ áthelyezés, ✓ marad …), `subText`
// a második sor (alszöveg). Ilyenkor a cím a gomb szélességéhez igazodva elide-olódik (a gomb
// szélességét a hívó adja, a név sosem tolja el a többit); a „· nem minta” az alszövegre kerül.
T.Button {
    id: control

    property string keyText: ""
    property string variant: "secondary"      // primary | secondary | ghost | calm
    property bool nonSample: false
    property bool showSuffix: true
    property bool dimmed: false               // halvány (pl. a J, ha ugyanazt csinálja, mint az Enter)
    property string toolTipText: ""
    property string iconName: ""
    property color iconColor: Theme.textMuted
    property string subText: ""
    readonly property string label: text + (nonSample && showSuffix ? qsTr(" · nem minta") : "")
    readonly property string subLabel: subText + (nonSample ? qsTr(" · nem minta") : "")
    readonly property bool twoLine: subText !== ""

    readonly property bool primary: variant === "primary"
    readonly property color foreground: !enabled ? Theme.textMuted
                                      : primary ? Theme.textOnAccent : Theme.text

    implicitHeight: twoLine ? 48 : Theme.controlHeight
    implicitWidth: twoLine ? 160 : row.implicitWidth + 20
    hoverEnabled: true
    focusPolicy: Qt.NoFocus
    opacity: dimmed ? 0.5 : 1
    font.family: Theme.fontSans
    font.pixelSize: Theme.fontBody
    font.weight: primary || twoLine ? Theme.weightSemiBold : Theme.weightMedium

    component KeyCap: Rectangle {
        property alias text: capLabel.text
        width: Math.max(20, capLabel.implicitWidth + 10)
        height: 20
        radius: 4
        color: control.primary ? Theme.alpha(Theme.textOnAccent, 0.18) : Theme.sunken
        border.width: 1
        border.color: control.primary ? Theme.alpha(Theme.textOnAccent, 0.35) : Theme.border
        Text {
            id: capLabel
            anchors.centerIn: parent
            color: control.foreground
            font.family: Theme.fontMono
            font.pixelSize: Theme.fontMicro
            font.weight: Theme.weightSemiBold
        }
    }

    contentItem: Item {
        // Egysoros: [kulcs  felirat] középre.
        Row {
            id: row
            visible: !control.twoLine
            anchors.centerIn: parent
            spacing: 8
            KeyCap {
                visible: control.keyText !== ""
                anchors.verticalCenter: parent.verticalCenter
                text: control.keyText
            }
            TIcon {
                visible: control.iconName !== ""
                anchors.verticalCenter: parent.verticalCenter
                name: control.iconName
                size: 16
                strokeWidth: 2.2
                color: control.enabled ? control.iconColor : Theme.textMuted
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: control.label
                font: control.font
                color: control.foreground
            }
        }
        // Kétsoros (S7): [kulcs  ikon  Név / alszöveg], balra zárva, a cím elide-olódik.
        KeyCap {
            id: cap2
            visible: control.twoLine && control.keyText !== ""
            x: 10
            anchors.verticalCenter: parent.verticalCenter
            text: control.keyText
        }
        TIcon {
            id: icon2
            visible: control.twoLine && control.iconName !== ""
            x: (cap2.visible ? cap2.x + cap2.width : 2) + 8
            anchors.verticalCenter: parent.verticalCenter
            name: control.iconName
            size: 18
            strokeWidth: 2.2
            color: control.enabled ? control.iconColor : Theme.textMuted
        }
        Column {
            visible: control.twoLine
            x: (icon2.visible ? icon2.x + icon2.width : cap2.visible ? cap2.x + cap2.width : 2) + 8
            width: Math.max(0, parent.width - x - 8)
            anchors.verticalCenter: parent.verticalCenter
            spacing: 1
            Text {
                objectName: "keyButtonTitle"
                width: parent.width
                text: control.text
                elide: Text.ElideRight
                font: control.font
                color: control.foreground
            }
            Text {
                objectName: "keyButtonSub"
                width: parent.width
                text: control.subLabel
                elide: Text.ElideRight
                font.family: Theme.fontSans
                font.pixelSize: Theme.fontMicro
                color: control.nonSample ? Theme.warnInk : Theme.textMuted
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
            border.width: (control.variant === "secondary" || control.variant === "calm") && !control.nonSample ? 1 : 0
            border.color: control.variant === "calm" ? Theme.border : Theme.borderStrong
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
