import QtQuick
import QtQuick.Templates as T

// Címke-chip (C01): szögletes (Theme.radiusTag), semleges, „#” jellel — így nem téveszthető
// össze a kerek, színes személy-chippel és a kis, kerek állapot-pirulával.
//   kind: "applied"   felrakott címke: raised, 1 px borderStrong (rámutatva / fókuszban text), „#”
//         "suggested" javasolt meglévő címke: szaggatott accent keret, „+”, mindig ×
//         "llmNew"    a nyelvi modell új neve: szaggatott accent, ✦ + „ÚJ” jelölés, ×
//         "overflow"  „+N”: sunken, keret nélkül
//         "partial"   tömeges kijelölésnél csak néhányon rajta: szaggatott borderStrong, „1/3”
//   compact: 22 px (felvevő), removable: × (felrakottnál csak rámutatva / fókuszban),
//   mini: 18 px / 11 px (a megjegyzés-kártyák jelzése), removeAlwaysVisible: a × mindig látszik
//   (többchipes mezőben), countText: szám a név után (mono 11, pl. „1/3”, „6×”). Legfeljebb 180 px; a hosszú név
//   elvágva, teljes alakja súgóban.
//   TagChip { text: "Nordvik"; removable: true; onClicked: …; onRemoveRequested: … }
T.AbstractButton {
    id: control

    property string kind: "applied"
    property bool compact: false
    property bool mini: false
    property bool removeAlwaysVisible: false
    property bool removable: kind === "suggested" || kind === "llmNew"
    property string countText: ""
    property string toolTipText: ""
    property real maxWidth: 180
    property bool stateHovered: hovered
    property bool stateFocused: visualFocus
    signal removeRequested()

    readonly property bool suggestion: kind === "suggested" || kind === "llmNew"
    readonly property bool overflow: kind === "overflow"
    readonly property bool showRemove: removable && (suggestion || removeAlwaysVisible || stateHovered || stateFocused || activeFocus)
    readonly property bool showGlyph: kind === "applied" || kind === "partial" || kind === "suggested"
    readonly property color ink: suggestion ? Theme.accent : overflow ? Theme.textMuted : Theme.text

    implicitHeight: mini ? 18 : compact ? 22 : 24
    implicitWidth: Math.min(maxWidth, row.implicitWidth + leftPadding + rightPadding)
    leftPadding: mini ? 5 : 7
    rightPadding: showRemove ? 5 : mini ? 6 : 8
    hoverEnabled: true
    activeFocusOnTab: true
    font.family: Theme.fontSans
    font.pixelSize: mini ? Theme.fontMicro : compact ? Theme.fontCaption : Theme.fontSmall
    font.weight: Theme.weightMedium
    Accessible.role: Accessible.Button
    Accessible.name: (kind === "suggested" ? qsTr("Javasolt címke: %1") : kind === "llmNew" ? qsTr("Új címke javaslat: %1")
                                                                                         : "%1").arg(text)

    Keys.onReturnPressed: click()
    Keys.onEnterPressed: click()
    Keys.onDeletePressed: if (removable) removeRequested()
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Backspace && removable && !suggestion) {
            removeRequested()
            event.accepted = true
        }
    }

    contentItem: Item {
        implicitWidth: row.implicitWidth
        implicitHeight: row.implicitHeight
        Row {
            id: row
            anchors.verticalCenter: parent.verticalCenter
            spacing: control.mini ? 3 : 4

            Text {
                visible: control.showGlyph
                anchors.verticalCenter: parent.verticalCenter
                text: control.kind === "suggested" ? "+" : "#"
                color: control.suggestion ? Theme.accent : Theme.textMuted
                font.family: Theme.fontMono
                font.pixelSize: control.mini ? Theme.fontMicro : Theme.fontCaption
            }
            TIcon {
                visible: control.kind === "llmNew"
                anchors.verticalCenter: parent.verticalCenter
                name: "sparkles"
                size: 12
                color: Theme.accent
            }
            Text {
                id: label
                anchors.verticalCenter: parent.verticalCenter
                // A név a maximális szélességből annyit kap, amennyi a többi rész után marad.
                width: Math.min(implicitWidth,
                                Math.max(24, control.maxWidth - control.leftPadding - control.rightPadding - othersWidth()))
                function othersWidth() {
                    let w = 0, n = 0
                    for (let i = 0; i < row.children.length; ++i) {
                        const c = row.children[i]
                        if (c === label || !c.visible) continue
                        w += c.implicitWidth
                        ++n
                    }
                    return w + n * row.spacing
                }
                rightPadding: control.overflow ? 0 : 3
                text: control.text
                color: control.ink
                font: control.font
                elide: Text.ElideRight
            }
            Rectangle {
                visible: control.kind === "llmNew"
                anchors.verticalCenter: parent.verticalCenter
                width: badge.implicitWidth + 8
                height: badge.implicitHeight + 2
                radius: 3
                color: Theme.accentSoft
                Text {
                    id: badge
                    anchors.centerIn: parent
                    text: qsTr("ÚJ")
                    color: Theme.accent
                    font.family: Theme.fontSans
                    font.pixelSize: 9
                    font.weight: Theme.weightBold
                    font.letterSpacing: 0.5
                }
            }
            Text {
                visible: control.countText !== ""
                anchors.verticalCenter: parent.verticalCenter
                text: control.countText
                color: Theme.textMuted
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontMicro
            }
            Item {
                visible: control.showRemove
                anchors.verticalCenter: parent.verticalCenter
                width: 12; height: 12
                TIcon { anchors.fill: parent; name: "x"; size: 12; color: control.suggestion ? Theme.accent : Theme.textMuted }
                HoverHandler { id: removeHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { margin: 4; onTapped: control.removeRequested() }
            }
        }
    }

    background: Rectangle {
        id: bg
        radius: Theme.radiusTag
        color: control.overflow ? Theme.sunken
             : control.suggestion ? "transparent" : Theme.raised
        border.width: control.kind === "applied" ? 1 : 0
        border.color: control.stateHovered || control.stateFocused ? Theme.text : Theme.borderStrong

        TDashedRect {
            anchors.fill: parent
            visible: control.suggestion || control.kind === "partial"
            radius: Theme.radiusTag
            color: control.suggestion ? Theme.accent
                 : control.stateHovered || control.stateFocused ? Theme.text : Theme.borderStrong
        }
        Rectangle {
            anchors.fill: parent
            radius: bg.radius
            color: Theme.stateLayer
            visible: control.kind !== "applied"
            opacity: control.down ? Theme.pressedOpacity : control.stateHovered ? Theme.hoverOpacity : 0
        }
        TFocusRing {
            visible: control.stateFocused
            targetRadius: bg.radius
            border.color: Theme.accentSoft
        }
    }

    TToolTip {
        visible: control.hovered && !removeHover.hovered && (control.toolTipText !== "" || label.truncated)
        text: control.toolTipText !== "" ? control.toolTipText : control.text
    }
}
