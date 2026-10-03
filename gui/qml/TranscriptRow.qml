import QtQuick

// Az átirat-lista egy sora: megszólalás (névsor csak beszélőváltáskor, alatta a szöveg),
// vagy a „Bizonytalan" szűrő „··· N biztos sor elrejtve" elválasztója. Bal oldalt (ha a sín
// látszik) a beszélő-oszlopok ezen sorra eső szelete; a sín egérkezelése a TranscriptTab-ban
// van (egy közös réteg, hogy a húzás több soron át is működjön).
Item {
    id: row

    // ---- a modellből ----
    required property int index
    required property string kind
    required property int utteranceIndex
    required property string timeLabel
    required property string lineText
    required property string richText
    required property string speakerKey
    required property string speakerName
    required property int colorIndex
    required property int lane
    required property bool head
    required property bool first
    required property bool uncertain
    required property bool corrected
    required property bool selected
    required property bool suggested
    required property bool suggestionAnchor
    required property int hiddenCount
    required property int startMs
    required property int endMs

    // ---- a TranscriptTab-tól ----
    required property var tab               // TranscriptTab (állapot + műveletek)
    required property var vm                // TranscriptEditorViewModel

    readonly property bool gap: kind === "gap"
    readonly property bool playing: !gap && vm.playingRow === index
    readonly property bool nameOpen: tab.speakerPopoverRow === index
    readonly property real textX: tab.textX
    readonly property bool inDrag: tab.dragActive && index >= Math.min(tab.dragFromRow, tab.dragToRow)
                                   && index <= Math.max(tab.dragFromRow, tab.dragToRow)
    readonly property alias nameItem: nameBox

    width: ListView.view ? ListView.view.width : 0
    height: gap ? 24 : body.height + (suggestionLoader.active ? suggestionLoader.height : 0)

    // ---- elválasztó (szűrő) ----
    Loader {
        active: row.gap
        x: row.textX + 24
        width: row.width - x - 20
        height: 24
        sourceComponent: Item {
            Rectangle { anchors.left: parent.left; anchors.right: gapLabel.left; anchors.rightMargin: 12; anchors.verticalCenter: parent.verticalCenter; height: 1; color: Theme.border }
            TLabel {
                id: gapLabel
                anchors.centerIn: parent
                text: qsTr("··· %n biztos sor elrejtve", "", row.hiddenCount)
                mono: true
                muted: true
                font.pixelSize: Theme.fontCaption
            }
            Rectangle { anchors.left: gapLabel.right; anchors.leftMargin: 12; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; height: 1; color: Theme.border }
        }
    }

    // A sín jobb széle (a javaslat-doboz mellett is végigfut).
    Rectangle {
        visible: row.tab.railShown && !row.gap
        x: row.textX - 1
        width: 1
        height: row.height
        color: Theme.border
    }

    // ---- megszólalás ----
    Item {
        id: body
        visible: !row.gap
        width: row.width
        height: row.gap ? 0 : textColumn.y + textColumn.height + 6

        // Sor-háttér: lejátszott = accentSoft; kijelölt = raised + 1.5 px accent keret.
        Rectangle {
            anchors.fill: parent
            visible: row.playing || row.selected
            color: row.selected ? Theme.raised : Theme.accentSoft
            border.width: row.selected ? 1.5 : 0
            border.color: Theme.accent
        }

        // A sín ezen sorra eső szelete.
        Loader {
            active: row.tab.railShown
            x: row.tab.railX
            width: row.tab.railWidth
            height: body.height
            sourceComponent: SpeakerRailCells {
                laneCount: row.tab.laneCount
                hasGroup: row.tab.groupShown
                leftInset: 4
                laneWidth: Theme.laneWidth
                groupWidth: Theme.laneGroupWidth
                ownLane: row.lane
                lineColor: Theme.speakerLine(row.colorIndex)
                softColor: Theme.speakerSoft(row.colorIndex)
                uncertain: row.uncertain
                selected: row.selected
                suggested: row.suggested
                dimmed: row.inDrag && row.tab.dragLane >= 0 && row.tab.dragLane !== row.lane
                targetLane: row.inDrag ? row.tab.dragLane : -1
                targetLineColor: Theme.speakerLine(row.tab.dragColorIndex)
                targetSoftColor: Theme.speakerSoft(row.tab.dragColorIndex)
                outlineColor: Theme.text
                dashColor: Theme.borderStrong
                accentColor: Theme.accent
            }
        }

        // Kattintás a szövegen: kijelölés (Ctrl / Shift: több sor); dupla kattintás: lejátszás innen.
        MouseArea {
            x: row.textX
            width: body.width - x
            height: body.height
            preventStealing: true
            onClicked: mouse => row.tab.rowClicked(row.index, mouse.modifiers)
            onDoubleClicked: row.tab.playFrom(row.startMs)
        }

        Column {
            id: textColumn
            x: row.textX + 24
            y: row.head && !row.first ? 14 : 6
            width: body.width - x - 20
            spacing: 2

            // Névsor: név (kattintható) · időbélyeg · pirula · (szűrőben) „Jó így" / „Meghallgatom".
            Item {
                visible: row.head
                width: parent.width
                height: row.head ? Math.max(17, actions.active ? 24 : 0) : 0

                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 10

                    Item {
                        id: nameBox
                        objectName: "speakerName"
                        width: nameRow.width
                        height: nameRow.height
                        anchors.verticalCenter: parent.verticalCenter
                        Rectangle {
                            x: -7; y: -3
                            width: parent.width + 14
                            height: parent.height + 6
                            radius: 5
                            visible: row.nameOpen || nameHover.hovered
                            color: row.nameOpen ? Theme.raised : Theme.alpha(Theme.stateLayer, Theme.hoverOpacity)
                            border.width: row.nameOpen ? 1.5 : 0
                            border.color: Theme.accent
                        }
                        Row {
                            id: nameRow
                            spacing: 4
                            TLabel {
                                text: row.speakerName
                                color: Theme.speakerInk(row.colorIndex)
                                font.pixelSize: Theme.fontSmall
                                font.weight: Theme.weightSemiBold
                            }
                            TIcon {
                                visible: row.nameOpen
                                anchors.verticalCenter: parent.verticalCenter
                                name: "chevron-down"
                                size: 13
                                color: Theme.speakerInk(row.colorIndex)
                            }
                        }
                        HoverHandler { id: nameHover; cursorShape: Qt.PointingHandCursor }
                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -4
                            cursorShape: Qt.PointingHandCursor
                            onClicked: row.tab.openSpeakerPopover(row.speakerKey, nameBox, row.index)
                        }
                        TToolTip {
                            visible: nameHover.hovered && !row.nameOpen
                            text: qsTr("A teljes beszélő átnevezése vagy összevonása")
                        }
                    }

                    TLabel {
                        id: stamp
                        objectName: "stamp"
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.timeLabel
                        mono: true
                        color: row.playing || stampHover.hovered ? Theme.accent : Theme.textMuted
                        font.pixelSize: Theme.fontMicro
                        HoverHandler { id: stampHover; cursorShape: Qt.PointingHandCursor }
                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -5
                            cursorShape: Qt.PointingHandCursor
                            onClicked: row.tab.playFrom(row.startMs)
                        }
                        TToolTip { visible: stampHover.hovered; text: qsTr("Lejátszás innen") }
                    }

                    TPill {
                        visible: row.uncertain || row.corrected
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.uncertain ? qsTr("bizonytalan") : qsTr("javítva")
                        tone: row.uncertain ? "warn" : "success"
                    }
                }

                Loader {
                    id: actions
                    active: row.head && row.uncertain && row.vm.uncertainOnly
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    sourceComponent: Row {
                        spacing: 6
                        TButton {
                            size: "small"
                            height: 24
                            leftPadding: 9; rightPadding: 9
                            radius: 5
                            iconName: "check"
                            iconSize: 12
                            spacing: 5
                            font.pixelSize: Theme.fontCaption
                            text: qsTr("Jó így")
                            toolTipText: qsTr("A beszélő rendben van: a sor többé nem bizonytalan")
                            onClicked: row.vm.confirmRow(row.index)
                        }
                        TButton {
                            size: "small"
                            variant: "ghost"
                            height: 24
                            leftPadding: 9; rightPadding: 9
                            radius: 5
                            iconName: "play"
                            iconSize: 12
                            spacing: 5
                            font.pixelSize: Theme.fontCaption
                            text: qsTr("Meghallgatom")
                            enabled: row.tab.canPlay
                            onClicked: row.tab.playLine(row.startMs, row.endMs)
                        }
                    }
                }
            }

            TLabel {
                width: parent.width
                text: row.richText !== "" ? row.richText : row.lineText
                textFormat: row.richText !== "" ? Text.RichText : Text.PlainText
                wrapMode: Text.Wrap
                cssLineHeight: Theme.transcriptLineHeight
            }
        }

        // Névsor nélküli (folytató) bekezdésnél az időbélyeg rámutatáskor jelenik meg.
        Loader {
            active: !row.head && rowHover.hovered
            anchors.right: parent.right
            anchors.rightMargin: 20
            y: 4
            sourceComponent: Rectangle {
                width: hoverStamp.implicitWidth + 12
                height: 18
                radius: 9
                color: Theme.raised
                border.width: 1
                border.color: Theme.border
                TLabel {
                    id: hoverStamp
                    anchors.centerIn: parent
                    text: row.timeLabel
                    mono: true
                    color: Theme.accent
                    font.pixelSize: Theme.fontMicro
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: row.tab.playFrom(row.startMs)
                }
            }
        }
        HoverHandler { id: rowHover }
    }

    // ---- javaslat a kézzel javított sor alatt ----
    Loader {
        id: suggestionLoader
        active: !row.gap && row.suggestionAnchor && row.vm.suggestionActive
        y: body.height
        x: row.textX + 24
        width: row.width - x - 20
        sourceComponent: Item {
            height: box.height + 14
            Rectangle {
                id: box
                y: 4
                width: parent.width
                height: boxColumn.height + 24
                radius: Theme.radiusControl
                color: Theme.accentSoft
                border.width: 1
                border.color: Theme.accentLine
                Column {
                    id: boxColumn
                    x: 14; y: 12
                    width: parent.width - 28
                    spacing: 10
                    Item {
                        width: parent.width
                        height: suggestionText.height
                        TIcon { y: 2; name: "wand-sparkles"; size: 16; color: Theme.accent }
                        TLabel {
                            id: suggestionText
                            x: 26
                            width: parent.width - 26
                            wrapMode: Text.Wrap
                            cssLineHeight: 1.45
                            text: qsTr("Még %n sor hasonlít erre a hangra. Átrakjam őket ehhez: %1?", "",
                                       row.vm.suggestionCount).arg(row.vm.suggestionTargetName)
                        }
                    }
                    Row {
                        x: 26
                        spacing: 8
                        TButton {
                            size: "small"; variant: "primary"
                            leftPadding: 12; rightPadding: 12
                            text: qsTr("Átrakom")
                            onClicked: row.vm.acceptSuggestion()
                        }
                        TButton {
                            size: "small"
                            text: row.vm.suggestionShown ? qsTr("Elrejtem") : qsTr("Megmutatom")
                            toolTipText: qsTr("A hasonló sorok kiemelése a sávokon és az áttekintőn")
                            onClicked: row.vm.suggestionShown = !row.vm.suggestionShown
                        }
                        TButton {
                            size: "small"; variant: "ghost"
                            text: qsTr("Nem")
                            onClicked: row.vm.dismissSuggestion()
                        }
                    }
                }
            }
        }
    }
}
