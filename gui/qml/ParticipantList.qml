import QtQuick
import QtQuick.Layouts

// KIK VOLTAK OTT lista (design/handoff-v3 V1/V3): surface, keret, sugár 8; sor 8/12 px:
// 26 px avatar (beszélő-színű lágy kitöltés + gyűrű, monogram) · név 13.5/600 (150 px, alatta
// opcionális alszöveg) · forrás-jelvények (20 px, sugár 4, sunken + 11 px ikon; ellentmondó =
// szaggatott borderStrong, halvány; címke-javaslat = accentSoft) · beszédarány-sáv (a beszélő
// vonalszínével) vagy „+ Hozzáadás” · ×. A lemondott meghívott 55 %; a javasolt személy
// szaggatott avatarral, halvány névvel, „javasolt” alszöveggel.
// people: OverviewViewModel.participants
Rectangle {
    id: root

    property var people: []

    signal addRequested(string name)
    signal removeRequested(string id)

    implicitHeight: list.implicitHeight
    radius: Theme.radiusPopup
    color: Theme.surface
    border.width: 1
    border.color: Theme.border

    // Forrás-jelvény.
    component SourceBadge: Item {
        id: badge
        property var source: ({})
        readonly property bool negative: source.negative === true
        readonly property bool accent: source.accent === true
        implicitWidth: badgeRow.implicitWidth + 12
        implicitHeight: 20
        Rectangle {
            anchors.fill: parent
            visible: !badge.negative
            radius: Theme.radiusTag
            color: badge.accent ? Theme.accentSoft : Theme.sunken
        }
        TDashedRect {
            anchors.fill: parent
            visible: badge.negative
            radius: Theme.radiusTag
            color: Theme.borderStrong
        }
        Row {
            id: badgeRow
            x: 5
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4
            TIcon {
                anchors.verticalCenter: parent.verticalCenter
                name: badge.source.icon || "info"
                size: 11
                color: badge.accent ? Theme.accent : Theme.textMuted
            }
            TLabel {
                anchors.verticalCenter: parent.verticalCenter
                text: badge.source.text || ""
                color: badge.accent ? Theme.accent : badge.negative ? Theme.textMuted : Theme.text
                font.pixelSize: 12
            }
        }
    }

    Column {
        id: list
        width: parent.width
        Repeater {
            model: root.people
            Item {
                id: row
                required property var modelData
                required property int index
                readonly property bool suggested: modelData.suggested === true
                readonly property bool hasTalk: modelData.talkShare >= 0
                objectName: "participantRow"
                width: list.width
                height: Math.max(42, rowLayout.implicitHeight + 16)
                opacity: modelData.dimmed ? 0.55 : 1

                Rectangle {
                    visible: row.index > 0
                    width: parent.width; height: 1
                    color: Theme.border
                }
                HoverHandler { id: rowHover }

                RowLayout {
                    id: rowLayout
                    anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter
                              leftMargin: 12; rightMargin: 12 }
                    spacing: 10

                    Item {
                        implicitWidth: 26; implicitHeight: 26
                        TAvatar {
                            anchors.fill: parent
                            visible: !row.suggested
                            size: 26
                            speakerIndex: Math.max(0, row.modelData.colorIndex)
                            monogram: row.modelData.monogram
                        }
                        TDashedRect {
                            anchors.fill: parent
                            visible: row.suggested
                            radius: 13
                            color: Theme.borderStrong
                        }
                        TLabel {
                            anchors.centerIn: parent
                            visible: row.suggested
                            text: row.modelData.monogram
                            muted: true
                            font.pixelSize: 9
                            font.weight: Theme.weightBold
                        }
                    }
                    ColumnLayout {
                        Layout.preferredWidth: 150
                        Layout.maximumWidth: 150
                        spacing: 1
                        TLabel {
                            objectName: "participantName"
                            Layout.fillWidth: true
                            text: row.modelData.name
                            color: row.suggested || row.modelData.dimmed ? Theme.textMuted : Theme.text
                            font.pixelSize: 13
                            font.weight: Theme.weightSemiBold
                            elide: Text.ElideRight
                        }
                        TLabel {
                            Layout.fillWidth: true
                            visible: text !== ""
                            text: row.modelData.sub || ""
                            muted: true
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 4
                        Repeater {
                            model: row.modelData.sources
                            SourceBadge { required property var modelData; source: modelData }
                        }
                    }
                    RowLayout {
                        visible: row.hasTalk
                        Layout.preferredWidth: 120
                        Layout.minimumWidth: 120
                        Layout.maximumWidth: 120
                        spacing: 8
                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 5
                            radius: 3
                            color: Theme.sunken
                            Rectangle {
                                width: parent.width * Math.min(1, Math.max(0, row.modelData.talkShare))
                                height: parent.height
                                radius: 3
                                color: Theme.speakerLine(Math.max(0, row.modelData.colorIndex))
                            }
                        }
                        TLabel {
                            Layout.preferredWidth: 30
                            horizontalAlignment: Text.AlignRight
                            text: Math.round(row.modelData.talkShare * 100) + "%"
                            mono: true
                            muted: true
                            font.pixelSize: 12
                        }
                    }
                    TButton {
                        objectName: "participantAdd"
                        visible: row.suggested
                        text: qsTr("Hozzáadás")
                        iconName: "plus"
                        size: "small"
                        onClicked: root.addRequested(row.modelData.name)
                    }
                    TIconButton {
                        objectName: "participantRemove"
                        visible: row.modelData.removable === true && (row.suggested || rowHover.hovered || activeFocus)
                        variant: "flat"
                        size: "small"
                        iconName: "x"
                        iconSize: 13
                        iconColor: Theme.textMuted
                        toolTipText: row.suggested ? qsTr("Nem volt ott (a javaslat eltűnik)")
                                                   : qsTr("Nem volt ott: a jelölése törlődik, a sorai névtelenek lesznek")
                        onClicked: root.removeRequested(row.modelData.id)
                    }
                }
            }
        }
    }
}
