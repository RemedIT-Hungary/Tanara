import QtQuick

// Speaker -> track binding chips in the WHO WAS THERE row: one chip per track (mic / call audio;
// several tracks can be switched on), toggled by a click. Tracks of the inferred side are faint
// (sunken + border), a manual binding is accent (accentSoft + accent icon / label); a track that
// is switched off has a dashed border, faint. In one row, with a short name ("mic" / "call" when
// there is only one of them). The same data as the editor's "Why them?" WHICH TRACK IS THE SPEAKER
// ON? chips (ParticipantTrackBinder -> AppController::setParticipantTracks).
//   ParticipantTrackChips { binder: trackBinder; participantId: modelData.id }
//   ParticipantTrackChips { binder: trackBinder; participantId: modelData.id }
Row {
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
            opacity: chip.on ? 1 : 0.8

            Rectangle {
                anchors.fill: parent
                visible: chip.on
                radius: Theme.radiusTag
                color: chip.manual ? Theme.accentSoft : Theme.sunken
                border.width: chip.manual ? 0 : 1
                border.color: Theme.border
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
                    text: chip.modelData.shortName || chip.modelData.name || ""
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
