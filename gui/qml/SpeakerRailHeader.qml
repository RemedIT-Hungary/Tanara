import QtQuick

// A beszélő-sín rögzített fejléce: látható beszélőnként egy 24 px-es oszlop (20 px-es teli
// avatar a beszélő színében, alatta 4 px-es pötty, ha a személynek van hanglenyomata), az
// összecsukott „+N" csoport, és a „+" (új résztvevő) oszlop. A sorok oszlopai ehhez igazodnak.
Item {
    id: root

    required property var vm                // TranscriptEditorViewModel
    property bool addOpen: false            // a „+" személyválasztója nyitva van
    readonly property alias addItem: addColumn

    signal speakerClicked(string speakerKey, Item anchor)
    signal addClicked()

    readonly property bool groupShown: vm.collapsedCount > 0 && !vm.lanesExpanded
    readonly property bool collapseShown: vm.collapsedCount > 0 && vm.lanesExpanded

    implicitWidth: lanes.width + 8
    implicitHeight: 42

    Row {
        id: lanes
        x: 4

        Repeater {
            model: root.vm.lanes
            Item {
                id: laneHead
                required property var modelData
                required property int index
                width: Theme.laneWidth
                height: root.implicitHeight
                TAvatar {
                    id: avatar
                    y: 8
                    anchors.horizontalCenter: parent.horizontalCenter
                    size: 20
                    variant: "solid"
                    name: laneHead.modelData.name
                    speakerIndex: laneHead.modelData.colorIndex
                }
                Rectangle {
                    y: 32
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 4; height: 4; radius: 2
                    color: Theme.text
                    visible: laneHead.modelData.hasVoiceprint
                }
                HoverHandler { id: laneHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: root.speakerClicked(laneHead.modelData.key, avatar) }
                TToolTip {
                    visible: laneHover.hovered
                    text: {
                        const d = laneHead.modelData
                        let t = d.name + " · " + d.pct + "% · " + qsTr("%n megszólalás", "", d.utteranceCount)
                        if (laneHead.index < 9) t += " · " + qsTr("%1-es billentyű").arg(laneHead.index + 1)
                        t += "\n" + (d.hasVoiceprint ? qsTr("Van hanglenyomata.") : d.anonymous
                                     ? qsTr("Névtelen beszélő.") : qsTr("Nincs hanglenyomata."))
                        return t
                    }
                }
            }
        }

        // „+N": a keveset beszélők összecsukott csoportja — kattintásra kinyílik.
        Item {
            visible: root.groupShown || root.collapseShown
            width: visible ? Theme.laneGroupWidth : 0
            height: root.implicitHeight
            Rectangle {
                y: 8
                anchors.horizontalCenter: parent.horizontalCenter
                width: 18; height: 20
                radius: 4
                color: groupHover.hovered ? Theme.alpha(Theme.stateLayer, Theme.hoverOpacity) : "transparent"
                border.width: 1
                border.color: Theme.borderStrong
                TLabel {
                    visible: root.groupShown
                    anchors.centerIn: parent
                    text: "+" + root.vm.collapsedCount
                    mono: true
                    font.pixelSize: 9
                    font.weight: Theme.weightSemiBold
                }
                TIcon {
                    visible: root.collapseShown
                    anchors.centerIn: parent
                    name: "chevrons-left-right"
                    size: 11
                    color: Theme.textMuted
                }
            }
            HoverHandler { id: groupHover; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: root.vm.lanesExpanded = !root.vm.lanesExpanded }
            TToolTip {
                visible: groupHover.hovered
                text: root.groupShown ? qsTr("%n keveset beszélő résztvevő — kattintásra külön oszlopot kapnak", "", root.vm.collapsedCount)
                                      : qsTr("A keveset beszélők összecsukása")
            }
        }

        // „+": új résztvevő (személyválasztó).
        Item {
            id: addColumn
            objectName: "addParticipant"
            width: Theme.laneWidth
            height: root.implicitHeight
            Rectangle {
                id: addCircle
                y: 8
                anchors.horizontalCenter: parent.horizontalCenter
                width: 20; height: 20; radius: 10
                color: root.addOpen ? Theme.accentSoft : addHover.hovered ? Theme.alpha(Theme.stateLayer, Theme.hoverOpacity) : "transparent"
                TDashedRect {
                    anchors.fill: parent
                    radius: 10
                    color: root.addOpen ? Theme.accent : Theme.borderStrong
                    dashPattern: [2, 2]
                }
                TIcon {
                    anchors.centerIn: parent
                    name: "plus"
                    size: 11
                    color: root.addOpen ? Theme.accent : Theme.textMuted
                }
            }
            HoverHandler { id: addHover; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: root.addClicked() }
            TToolTip { visible: addHover.hovered && !root.addOpen; text: qsTr("Résztvevő hozzáadása") }
        }
    }
}
