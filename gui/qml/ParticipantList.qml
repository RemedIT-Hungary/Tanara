import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// KIK VOLTAK OTT lista (design/handoff-v3 V1/V3): surface, keret, sugár 8; sor 8/12 px:
// 26 px avatar (beszélő-színű lágy kitöltés + gyűrű, monogram) · név 13.5/600 (150 px, alatta
// opcionális alszöveg) · forrás-jelvények (20 px, sugár 4, sunken + 11 px ikon; ellentmondó =
// szaggatott borderStrong, halvány; címke-javaslat = accentSoft) · beszédarány-sáv (a beszélő
// vonalszínével) vagy „+ Hozzáadás” · ×. A lemondott meghívott és az átirat után nem hallott
// (unheard: „nem hallottuk” jelvény) résztvevő 55 %; a javasolt személy szaggatott avatarral,
// halvány névvel, „javasolt” alszöveggel. A × tartós (törlés / a javaslat elutasítása erre a
// megbeszélésre); a közös visszavonás (Ctrl+Z, változás-sáv) hozza vissza.
// A jelvény (13. döntés, „bizonyíték mindenhol, javítás helye mindenhol”): tooltipje a bizonyíték
// magyarázata (source.detail); átirat után (a sornak van beszélő-kulcsa) kattintható, és a
// szerkesztő „Miért ő?” paneljét nyitja arra a beszélőre (whyRequested). A névtelen sor
// („elnevezhető”) kattintásra és a jobb szélső „Elnevezés…” gombbal elnevezhető (nameRequested).
// Sáv-chipek (G6): a forrás-jelvények után a beszélő sávjai (ParticipantTrackChips; kattintva a
// kötés vált, ugyanaz az adat, mint a „Miért ő?” MELYIK SÁVON BESZÉL? chipjei).
// people: OverviewViewModel.participants · trackBinder: ParticipantTrackBinder
Rectangle {
    id: root

    property var people: []
    // Kattintható-e a jelvény („Miért ő?”): átirat után igen.
    property bool canExplain: true
    // Beszélő → sáv kötés (G6): ParticipantTrackBinder; null → nincsenek sáv-chipek.
    property var trackBinder: null

    signal addRequested(string name)
    signal removeRequested(string id)
    signal whyRequested(string speakerKey, Item anchor)
    signal nameRequested(var row, Item anchor)

    implicitHeight: list.implicitHeight
    radius: Theme.radiusPopup
    color: Theme.surface
    border.width: 1
    border.color: Theme.border

    // Forrás-jelvény.
    component SourceBadge: T.AbstractButton {
        id: badge
        property var source: ({})
        property string speakerKey: ""
        readonly property bool negative: source.negative === true
        readonly property bool accent: source.accent === true
        readonly property bool clickable: root.canExplain && speakerKey !== ""
        readonly property string tip: (source.detail || "") !== "" ? source.detail : (source.text || "")
        objectName: "participantBadge"
        hoverEnabled: true
        focusPolicy: clickable ? Qt.TabFocus : Qt.NoFocus
        enabled: true
        implicitWidth: badgeRow.implicitWidth + 12
        implicitHeight: 20
        Accessible.name: badge.tip
        Accessible.role: clickable ? Accessible.Button : Accessible.StaticText
        onClicked: if (clickable) root.whyRequested(speakerKey, badge)
        HoverHandler { cursorShape: badge.clickable ? Qt.PointingHandCursor : Qt.ArrowCursor }
        TToolTip {
            visible: badge.hovered && badge.tip !== ""
            delay: 300
            text: badge.clickable ? badge.tip + "\n" + qsTr("Kattints: Miért ő? (javítás a szerkesztőben)") : badge.tip
        }
        background: Item {}
        contentItem: Item {}
        Rectangle {
            anchors.fill: parent
            visible: !badge.negative
            radius: Theme.radiusTag
            color: badge.accent ? Theme.accentSoft : Theme.sunken
        }
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusTag
            color: Theme.stateLayer
            opacity: badge.clickable ? (badge.down ? Theme.pressedOpacity : badge.hovered ? Theme.hoverOpacity : 0) : 0
        }
        TFocusRing { visible: badge.visualFocus; targetRadius: Theme.radiusTag }
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
                readonly property bool nameable: modelData.anonymous === true && !suggested
                objectName: "participantRow"
                width: list.width
                height: Math.max(42, rowLayout.implicitHeight + 16)
                opacity: modelData.dimmed ? 0.55 : 1

                Rectangle {
                    visible: row.index > 0
                    width: parent.width; height: 1
                    color: Theme.border
                }
                HoverHandler { id: rowHover; cursorShape: row.nameable ? Qt.PointingHandCursor : Qt.ArrowCursor }
                // A névtelen sor kattintásra elnevezhető (a gombok és a jelvények a sajátjukat kapják).
                TapHandler {
                    enabled: row.nameable
                    onTapped: root.nameRequested(row.modelData, nameButton)
                }
                Rectangle {
                    anchors.fill: parent
                    visible: row.nameable
                    color: Theme.stateLayer
                    opacity: rowHover.hovered ? Theme.hoverOpacity : 0
                }

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
                            // A sáv-chipek mutatják az oldalt: a (nem ellentmondó) mikrofon / hívás
                            // jelvény ilyenkor kimarad.
                            model: trackChips.visible
                                   ? (row.modelData.sources || []).filter(s => s.negative === true
                                                                         || (s.icon !== "mic" && s.icon !== "phone"))
                                   : row.modelData.sources
                            SourceBadge {
                                required property var modelData
                                source: modelData
                                speakerKey: row.modelData.speakerKey || ""
                            }
                        }
                        ParticipantTrackChips {
                            id: trackChips
                            binder: root.trackBinder
                            // A nem hallott résztvevőnek nincs beszéde: nincs mit sávhoz kötni.
                            participantId: row.suggested || row.modelData.unheard === true ? "" : (row.modelData.id || "")
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
                        id: nameButton
                        objectName: "participantNameButton"
                        visible: row.nameable
                        text: qsTr("Elnevezés…")
                        variant: "ghost"
                        size: "small"
                        toolTipText: qsTr("Kié ez a hang? A sorai a választott névre kerülnek")
                        onClicked: root.nameRequested(row.modelData, nameButton)
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
                        toolTipText: row.suggested ? qsTr("Nem volt ott: a javaslat ennél a megbeszélésnél nem jön elő (Ctrl+Z visszahozza)")
                                   : row.modelData.unheard === true ? qsTr("Törlés a résztvevők közül (Ctrl+Z visszavonja)")
                                   : qsTr("Nem volt ott: törlés a résztvevők közül, a sorai névtelenek lesznek (Ctrl+Z visszavonja)")
                        onClicked: root.removeRequested(row.modelData.id)
                    }
                }
            }
        }
    }
}
