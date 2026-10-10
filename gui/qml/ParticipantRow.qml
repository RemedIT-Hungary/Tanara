import QtQuick

// A „Ki volt ott?" párbeszéd egy sora (V2): jelölőnégyzet, avatar, név (130 px), bizonyíték-
// chipek, jobbra a leképezés-chip („→ Beszélő 1 · hívás"; bejelölve accentSoft, kikapcsolva
// sunken; átirat előtt „→ átirat után"). A ki nem jelölt sor 70%-os. Névtelen (ismeretlen hangú)
// sornál „Elnevezés…" gomb; a párbeszédben felvett sornál ×.
//   ParticipantRow { row: modelData; onToggled: (on) => …; onNameRequested: (anchor) => …; onRemoveRequested: … }
Item {
    id: root

    property var row: ({})                 // ParticipantsViewModel.groups[i].rows[j]
    property bool showTopBorder: false

    readonly property bool checked: !!row.checked
    readonly property bool anonymous: !!row.anonymous

    signal toggled(bool on)
    signal nameRequested(Item anchor)
    signal removeRequested()

    implicitHeight: Math.max(42, content.implicitHeight + 16)
    objectName: "participantRow_" + (row.id || "")

    TDivider { width: parent.width; visible: root.showTopBorder }

    TCheckBox {
        id: box
        objectName: "participantCheck_" + (root.row.id || "")
        x: 12
        anchors.verticalCenter: parent.verticalCenter
        checked: root.checked
        enabled: !!root.row.checkable
        Accessible.name: root.row.displayName || ""
        onToggled: root.toggled(checked)
    }

    Row {
        id: content
        x: box.x + 16 + 10
        width: parent.width - x - 12
        anchors.verticalCenter: parent.verticalCenter
        spacing: 10
        opacity: root.checked ? 1 : 0.7

        Item {
            width: 26; height: 26
            anchors.verticalCenter: parent.verticalCenter
            TAvatar {
                visible: !root.anonymous
                size: 26
                name: root.row.name || ""
                speakerIndex: root.row.colorIndex || 0
            }
            TDashedRect {
                visible: root.anonymous
                anchors.fill: parent
                radius: 13
                lineWidth: 1.5
                color: Theme.borderStrong
            }
        }

        Item {
            width: 130
            height: nameLabel.implicitHeight
            anchors.verticalCenter: parent.verticalCenter
            TLabel {
                id: nameLabel
                width: parent.width
                elide: Text.ElideRight
                text: root.row.displayName || ""
                muted: root.anonymous
                font.weight: Theme.weightSemiBold
            }
        }

        Flow {
            id: chips
            width: content.width - 26 - 130 - trailing.width - 3 * content.spacing
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4
            Repeater {
                model: root.row.evidence || []
                EvidenceChip {
                    required property var modelData
                    kind: modelData.kind
                    polarity: modelData.polarity
                    side: modelData.side
                    text: modelData.text
                    quiet: true
                }
            }
            TButton {
                visible: root.anonymous
                objectName: "participantName_" + (root.row.id || "")
                variant: "ghost"
                size: "small"
                height: 20
                iconName: "user-plus"
                text: qsTr("Elnevezés…")
                onClicked: root.nameRequested(this)
            }
        }

        Row {
            id: trailing
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            Rectangle {
                visible: (root.row.mapping || "") !== ""
                anchors.verticalCenter: parent.verticalCenter
                height: 22
                width: mapRow.implicitWidth + 16
                radius: 5
                color: root.checked ? Theme.accentSoft : Theme.sunken
                Row {
                    id: mapRow
                    x: 8
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 5
                    TIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "arrow-right"
                        size: 11
                        color: mapLabel.color
                    }
                    TLabel {
                        id: mapLabel
                        anchors.verticalCenter: parent.verticalCenter
                        mono: true
                        text: root.row.mapping || ""
                        color: root.checked ? Theme.accent : Theme.textMuted
                        font.pixelSize: Theme.fontMicro + 1
                        font.weight: Theme.weightMedium
                    }
                }
            }
            TIconButton {
                visible: !!root.row.added
                objectName: "participantRemove_" + (root.row.id || "")
                anchors.verticalCenter: parent.verticalCenter
                variant: "flat"
                size: "small"
                iconName: "x"
                iconSize: 14
                toolTipText: qsTr("Elvetés")
                onClicked: root.removeRequested()
            }
        }
    }
}
