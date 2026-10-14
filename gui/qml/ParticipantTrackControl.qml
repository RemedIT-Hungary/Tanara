import QtQuick
import QtQuick.Layouts

// The "Track:" control of a "Who was there?" row: which track the speaker talks on, as a CONTROL
// (button look), on its own, apart from the evidence badges. Every selectable track is a round,
// outlined button in one of three states:
//   off              outline, muted label                      ("ki")
//   heard            stronger outline + ear icon: the inferred  ("itt hallottuk": a conclusion,
//                    side, NOT a binding                         not a binding)
//   bound by hand    accent fill + check                        ("kézzel kötve")
// A click toggles (ParticipantTrackBinder.toggle: clicking the single heard track confirms it as a
// manual binding). With 3+ tracks a drop-down button lists them instead (same states, same click).
// The kind of a track never means a person: only a click binds. stateText: the state in words
// after the buttons (wide rows; otherwise in the tooltip).
//   ParticipantTrackControl { binder: trackBinder; participantId: row.id }
RowLayout {
    id: root

    property var binder: null                 // ParticipantTrackBinder
    property string participantId: ""
    property bool showState: false

    readonly property var chips: binder ? (binder.revision, binder.chips(participantId)) : []
    readonly property bool manual: chips.length > 0 && chips[0].manual === true
    readonly property bool dropDown: chips.length >= 3
    readonly property string heardText: chips.filter(c => c.on).map(c => c.shortName).join(", ")
    readonly property string stateText: manual ? qsTr("kézzel kötve: %1").arg(heardText)
                                      : heardText !== "" ? qsTr("hallottuk: %1 · kattints a kötéshez").arg(heardText)
                                      : qsTr("nincs kötve")

    objectName: "participantTrackControl"
    visible: chips.length > 0
    spacing: 4

    function toggle(trackId) { if (root.binder) root.binder.toggle(root.participantId, trackId) }

    TLabel {
        text: qsTr("Sáv:")
        muted: true
        font.pixelSize: 12
        font.weight: Theme.weightSemiBold
    }

    // One round button per track (two tracks: the common case).
    Repeater {
        model: root.dropDown ? [] : root.chips
        Rectangle {
            id: seg
            required property var modelData
            readonly property bool bound: root.manual && modelData.on === true
            readonly property bool heard: !root.manual && modelData.on === true
            objectName: "participantTrackButton_" + modelData.trackId
            implicitWidth: segRow.implicitWidth + 16
            implicitHeight: 24
            radius: 12
            color: seg.bound ? Theme.accent : segHover.hovered ? Theme.sunken : Theme.surface
            border.width: 1
            border.color: seg.bound ? Theme.accent : seg.heard ? Theme.borderStrong : Theme.border
            Row {
                id: segRow
                x: 8
                anchors.verticalCenter: parent.verticalCenter
                spacing: 5
                TIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: seg.bound ? "check" : (seg.modelData.icon || "audio-lines")
                    size: 12
                    color: seg.bound ? Theme.textOnAccent : seg.heard ? Theme.text : Theme.textMuted
                }
                TLabel {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, 110)
                    elide: Text.ElideRight
                    text: seg.modelData.shortName || seg.modelData.name || ""
                    font.pixelSize: 12
                    font.weight: seg.bound ? Theme.weightSemiBold : Theme.weightRegular
                    color: seg.bound ? Theme.textOnAccent : seg.heard ? Theme.text : Theme.textMuted
                }
                TIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: seg.heard
                    name: "ear"
                    size: 11
                    color: Theme.textMuted
                }
            }
            HoverHandler { id: segHover; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: root.toggle(seg.modelData.trackId) }
            TToolTip {
                visible: segHover.hovered
                delay: 300
                text: (seg.bound ? qsTr("Kézzel ehhez a sávhoz kötve · kattintva leveszed")
                       : seg.heard ? qsTr("Itt hallottuk (következtetés, nem kötés) · kattintva ehhez a sávhoz kötöd")
                       : root.manual ? qsTr("Kattintva ezt a sávot is hozzáköti")
                       : qsTr("Kattintva kézzel ehhez a sávhoz köti"))
                      + (root.showState ? "" : "\n" + root.stateText)
            }
            Accessible.role: Accessible.CheckBox
            Accessible.checked: seg.bound
            Accessible.name: (seg.modelData.name || "") + " · " + root.stateText
        }
    }

    // 3+ tracks: one drop-down button with the state, the tracks in a list.
    TButton {
        id: dropButton
        objectName: "participantTrackDropDown"
        visible: root.dropDown
        size: "small"
        variant: root.manual ? "primary" : "secondary"
        iconName: root.manual ? "check" : "ear"
        iconSize: 12
        trailingIconName: "chevron-down"
        text: root.heardText !== "" ? root.heardText : qsTr("nincs kötve")
        toolTipText: root.stateText
        onClicked: trackMenu.open()
        TPopover {
            id: trackMenu
            objectName: "participantTrackMenu"
            y: dropButton.height + 4
            width: 240
            padding: 4
            contentItem: Column {
                Repeater {
                    model: root.chips
                    TMenuItem {
                        required property var modelData
                        objectName: "participantTrackMenuItem"
                        width: 232
                        iconName: modelData.icon || "audio-lines"
                        text: modelData.name + (root.manual && modelData.on ? "  · " + qsTr("kézzel kötve")
                                                : modelData.on ? "  · " + qsTr("itt hallottuk") : "")
                        checked: root.manual && modelData.on === true
                        onTriggered: root.toggle(modelData.trackId)
                    }
                }
            }
        }
    }

    TLabel {
        visible: root.showState
        Layout.leftMargin: 2
        text: root.stateText
        font.pixelSize: 12
        color: root.manual ? Theme.accent : Theme.textMuted
        elide: Text.ElideRight
        Layout.maximumWidth: 220
    }
}
