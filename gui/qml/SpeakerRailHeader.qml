import QtQuick

// Pinned header of the speaker rail: one 24 px column per visible speaker (a 20 px solid avatar
// in the speaker's color, with a voiceprint dot on its corner: solid green if the person has a
// voiceprint, an empty ring if named but without one; below it the side icon: mic / call audio;
// a just-added person's avatar gets an accent ring), the collapsed "+N" group, and the "+" (new
// participant) column. The row columns line up with this.
Item {
    id: root

    required property var vm                // TranscriptEditorViewModel
    property bool addOpen: false            // the "+" person picker is open
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
                objectName: "laneHead"
                required property var modelData
                required property int index
                width: Theme.laneWidth
                height: root.implicitHeight
                // Column of a just-added person: 2 px accent ring around the avatar.
                Rectangle {
                    visible: laneHead.modelData.isNew === true
                    anchors.centerIn: avatar
                    width: avatar.width + 4; height: width; radius: width / 2
                    color: "transparent"
                    border.width: 2
                    border.color: Theme.accent
                }
                TAvatar {
                    id: avatar
                    y: 6
                    anchors.horizontalCenter: parent.horizontalCenter
                    size: 20
                    variant: "solid"
                    name: laneHead.modelData.name
                    speakerIndex: laneHead.modelData.colorIndex
                }
                // Voiceprint mark on the avatar's bottom-right corner: solid green = has one, empty ring = named
                // but none yet; none for an unnamed speaker.
                Rectangle {
                    objectName: "laneVoiceprint"
                    property string voiceprint: laneHead.modelData.voiceprint
                    x: avatar.x + avatar.width - 5
                    y: avatar.y + avatar.height - 6
                    width: 6; height: 6; radius: 3
                    color: laneHead.modelData.hasVoiceprint ? Theme.success : Theme.surface
                    border.width: 1
                    border.color: laneHead.modelData.hasVoiceprint ? Theme.surface : Theme.borderStrong
                    visible: !laneHead.modelData.anonymous
                }
                // Which track the speaker is on (per track analysis): mic / call audio.
                TIcon {
                    objectName: "laneSide"
                    readonly property string side: laneHead.modelData.side || "unknown"
                    visible: side === "local" || side === "remote"
                    y: 29
                    anchors.horizontalCenter: parent.horizontalCenter
                    name: side === "local" ? "mic" : "phone"
                    size: 10
                    color: Theme.textMuted
                }
                HoverHandler { id: laneHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: root.speakerClicked(laneHead.modelData.key, avatar) }
                TToolTip {
                    visible: laneHover.hovered
                    text: {
                        const d = laneHead.modelData
                        let t = d.name + (d.nameDuplicate && d.rawLabel ? " (" + d.rawLabel + ")" : "")
                                + " · " + d.pct + "% · " + qsTr("%n megszólalás", "", d.utteranceCount)
                        if (laneHead.index < 9) t += " · " + qsTr("%1-es billentyű").arg(laneHead.index + 1)
                        t += "\n" + (d.hasVoiceprint ? qsTr("Van hanglenyomata.") : d.anonymous
                                     ? qsTr("Névtelen beszélő.") : qsTr("Nincs hanglenyomata."))
                        if (d.side === "local") t += " " + qsTr("A mikrofonon beszél.")
                        else if (d.side === "remote") t += " " + qsTr("A hívás hangján beszél.")
                        t += "\n" + qsTr("Kattintásra: miért ő, és hol javítható")
                        return t
                    }
                }
            }
        }

        // "+N": collapsed group of low-talking speakers - expands on click.
        Item {
            visible: root.groupShown || root.collapseShown
            width: visible ? Theme.laneGroupWidth : 0
            height: root.implicitHeight
            Rectangle {
                y: 6
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

        // "+": new participant (person picker).
        Item {
            id: addColumn
            objectName: "addParticipant"
            width: Theme.laneWidth
            height: root.implicitHeight
            Rectangle {
                id: addCircle
                y: 6
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
