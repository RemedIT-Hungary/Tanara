import QtQuick

// Beszélő → sáv kötés chipjei a KIK VOLTAK OTT sorban (G6): sávonként egy chip (mikrofon / hívás
// hangja; több sáv is bekapcsolható), kattintva váltható. A következtetett oldal sávjai halványan
// (sunken, muted), a kézi kötés accent (accentSoft + accent ikon/felirat); a ki nem kapcsolt sáv
// szaggatott keretes. Ugyanaz az adat, mint a szerkesztő „Miért ő?" MELYIK SÁVON BESZÉL? chipjei
// (ParticipantTrackBinder → AppController::setParticipantTracks).
//   ParticipantTrackChips { binder: trackBinder; participantId: modelData.id }
Flow {
    id: root

    property var binder: null                 // ParticipantTrackBinder
    property string participantId: ""
    readonly property var chips: binder ? (binder.revision, binder.chips(participantId)) : []

    objectName: "participantTrackChips"
    visible: chips.length > 0
    spacing: 4

    Repeater {
        model: root.chips
        Item {
            id: chip
            required property var modelData
            readonly property bool on: modelData.on === true
            readonly property bool manual: modelData.manual === true
            objectName: "participantTrackChip_" + modelData.trackId
            implicitWidth: chipRow.implicitWidth + 12
            implicitHeight: 20
            opacity: chip.on && !chip.manual ? 0.7 : 1

            Rectangle {
                anchors.fill: parent
                visible: chip.on
                radius: Theme.radiusTag
                color: chip.manual ? Theme.accentSoft : Theme.sunken
            }
            TDashedRect {
                anchors.fill: parent
                visible: !chip.on
                radius: Theme.radiusTag
                color: Theme.border
            }
            Rectangle {
                anchors.fill: parent
                radius: Theme.radiusTag
                color: Theme.stateLayer
                opacity: tap.pressed ? Theme.pressedOpacity : hh.hovered ? Theme.hoverOpacity : 0
            }
            Row {
                id: chipRow
                x: 6
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4
                TIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: chip.modelData.icon || "audio-lines"
                    size: 11
                    color: chip.on && chip.manual ? Theme.accent : Theme.textMuted
                }
                TLabel {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, 96)
                    text: chip.modelData.name || ""
                    elide: Text.ElideRight
                    color: chip.on && chip.manual ? Theme.accent : chip.on ? Theme.text : Theme.textMuted
                    font.pixelSize: 12
                }
            }
            HoverHandler { id: hh; cursorShape: Qt.PointingHandCursor }
            TapHandler {
                id: tap
                onTapped: if (root.binder) root.binder.toggle(root.participantId, chip.modelData.trackId)
            }
            TToolTip {
                visible: hh.hovered
                text: chip.manual
                      ? (chip.on ? qsTr("Kézzel ehhez a sávhoz kötve · kattintva leveszed")
                                 : qsTr("Kattintva ezt a sávot is hozzáköti"))
                      : (chip.on ? qsTr("A hang alapján ezen a sávon beszél · kattintva rögzíted")
                                 : qsTr("Kattintva kézzel ehhez a sávhoz köti"))
            }
            Accessible.role: Accessible.CheckBox
            Accessible.checked: chip.on
            Accessible.name: chip.modelData.name || ""
        }
    }
}
