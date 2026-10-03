import QtQuick
import QtQuick.Templates as T

// Chip (24 px, 12/600). Fajtái:
//   szűrő:         TChip { text: qsTr("Bizonytalan 12"); checkable: true }  (be: accent stílus)
//   eltávolítható: TChip { text: …; removable: true; onRemoved: … }         (× a végén)
//   személy:       TChip { text: "Varga Nóra"; speakerIndex: 3 }            (beszélőszín + pötty)
//   hozzáadás:     TChip { text: qsTr("Szűrő"); dashed: true }              (szaggatott, + ikon)
T.AbstractButton {
    id: control

    property int speakerIndex: -1
    property bool removable: false
    property bool dashed: false
    property string iconName: dashed ? "plus" : ""
    property string tone: "accent"                   // "accent" | "neutral" (ha nincs speakerIndex)
    property bool stateHovered: hovered
    property bool stateFocused: visualFocus
    signal removed()

    readonly property bool person: speakerIndex >= 0
    readonly property bool active: !checkable || checked
    readonly property color ink: dashed ? Theme.textMuted
                               : person ? Theme.speakerInk(speakerIndex)
                               : !active || tone === "neutral" ? Theme.textMuted : Theme.accent

    implicitHeight: 24
    implicitWidth: row.implicitWidth + leftPadding + rightPadding
    leftPadding: person ? 4 : 9
    rightPadding: removable ? 6 : 9
    hoverEnabled: true
    font.family: Theme.fontSans
    font.pixelSize: Theme.fontCaption
    font.weight: dashed ? Theme.weightRegular : Theme.weightSemiBold

    Keys.onReturnPressed: click()
    Keys.onDeletePressed: if (removable) removed()

    contentItem: Item {
        Row {
            id: row
            anchors.verticalCenter: parent.verticalCenter
            spacing: control.person ? 5 : 4
            Rectangle {
                visible: control.person
                width: 16; height: 16; radius: 8
                color: control.person ? Theme.speakerLine(control.speakerIndex) : "transparent"
                anchors.verticalCenter: parent.verticalCenter
            }
            TIcon {
                visible: control.iconName !== ""
                name: control.iconName
                size: 12
                color: control.ink
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: control.text
                font: control.font
                color: control.ink
                anchors.verticalCenter: parent.verticalCenter
            }
            Item {
                visible: control.removable
                width: 12; height: 12
                anchors.verticalCenter: parent.verticalCenter
                TIcon { anchors.fill: parent; name: "x"; size: 12; color: control.ink }
                TapHandler { margin: 4; onTapped: control.removed() }
            }
        }
    }

    background: Rectangle {
        id: bg
        radius: height / 2
        color: control.dashed ? "transparent"
             : control.person ? Theme.speakerSoft(control.speakerIndex)
             : !control.active ? "transparent"
             : control.tone === "neutral" ? Theme.sunken : Theme.accentSoft
        border.width: !control.dashed && !control.person && !control.active ? 1 : 0
        border.color: Theme.borderStrong

        TDashedRect { anchors.fill: parent; visible: control.dashed; radius: height / 2 }
        Rectangle {
            anchors.fill: parent
            radius: bg.radius
            color: Theme.stateLayer
            opacity: control.down ? Theme.pressedOpacity : control.stateHovered ? Theme.hoverOpacity : 0
        }
        TFocusRing { visible: control.stateFocused; targetRadius: bg.radius }
    }
}
