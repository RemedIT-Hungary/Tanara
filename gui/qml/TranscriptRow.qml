import QtQuick

// Az átirat-lista egy sora: megszólalás (névsor csak beszélőváltáskor, alatta a szöveg),
// vagy a „Bizonytalan" szűrő „··· N biztos sor elrejtve" elválasztója. Bal oldalt (ha a sín
// látszik) a beszélő-oszlopok ezen sorra eső szelete; a sín egérkezelése a TranscriptTab-ban
// van (egy közös réteg, hogy a húzás több soron át is működjön). A NÉVRE kattintva ennek a
// sornak a beszélője javítható (a teljes beszélő a sáv-fejlécről / az áttekintőről).
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
    required property bool noisy
    required property bool noisyOverlap
    required property string likelySpeakerKey
    required property string likelySpeakerName
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
    height: gap ? 24 : body.height

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

    // A sín jobb széle.
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
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onClicked: mouse => {
                if (mouse.button === Qt.RightButton) row.tab.openRowMenu(row.index, this, mouse.x, mouse.y)
                else row.tab.rowClicked(row.index, mouse.modifiers)
            }
            onDoubleClicked: mouse => { if (mouse.button === Qt.LeftButton) row.tab.playFrom(row.startMs) }
        }

        Column {
            id: textColumn
            x: row.textX + 24
            y: row.head && !row.first ? 14 : 6
            width: body.width - x - 20
            spacing: 2

            // Névsor: név (kattintható: ez a sor kié) · időbélyeg · pirula · (szűrőben)
            // „Meghallgatom" / „Jó így" / „Jó így, de nem minta" / („<Név> mondta") / „Más mondta…".
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
                            onClicked: row.tab.openLinePopover(row.index, nameBox)
                        }
                        TToolTip {
                            visible: nameHover.hovered && !row.nameOpen
                            text: row.selected && row.vm.selectedCount > 1
                                  ? qsTr("Más mondta? A kijelölt sorok beszélőjének javítása")
                                  : qsTr("Más mondta? Ennek a sornak a beszélője javítható")
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
                        id: statePill
                        visible: row.uncertain || row.corrected
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.uncertain ? qsTr("bizonytalan") : qsTr("javítva")
                        tone: row.uncertain ? "warn" : "success"
                        HoverHandler { id: statePillHover }
                        TToolTip {
                            visible: statePillHover.hovered && row.uncertain && row.likelySpeakerName !== ""
                            text: qsTr("Hangra inkább %1 sorának tűnik").arg(row.likelySpeakerName)
                        }
                    }

                    // „Egymásra beszéltek": visszafogott jelzés; a sor nem hangminta.
                    TPill {
                        id: noisyPill
                        objectName: "noisyPill"
                        visible: row.noisy
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("egymásra beszéltek")
                        tone: "neutral"
                        HoverHandler { id: noisyHover }
                        TToolTip {
                            visible: noisyHover.hovered
                            text: row.noisyOverlap
                                  ? qsTr("Ebben a sorban más is beszél egyszerre, ezért a hangját nem használom mintának (bizonytalanság-jelzés, hanglenyomat).")
                                  : qsTr("Megjelölted, hogy ezt a sort ne használjam hangmintának (bizonytalanság-jelzés, hanglenyomat).")
                        }
                    }
                    // A jelzés visszavonása: csak rámutatáskor, hogy ne zajosítsa a listát.
                    TButton {
                        objectName: "noisyClear"
                        visible: row.noisy && (rowHover.hovered || hovered)
                        anchors.verticalCenter: parent.verticalCenter
                        size: "small"
                        variant: "ghost"
                        height: 20
                        leftPadding: 6; rightPadding: 6
                        radius: 5
                        font.pixelSize: Theme.fontMicro
                        text: qsTr("Mintának használható")
                        toolTipText: qsTr("A sor hangja mégis mehet mintának")
                        onClicked: row.vm.setRowNoisy(row.index, false)
                    }
                }

                Loader {
                    id: actions
                    active: row.head && row.uncertain && row.vm.uncertainOnly
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    sourceComponent: Row {
                        spacing: 6
                        // A természetes sorrend: meghallgatom → jó így / más mondta. A MEGHALLGATÁS a
                        // kiemelt (ez az első lépés); a döntés két gombja visszafogott és távolabb áll,
                        // hogy ne lehessen véletlenül a „Jó így"-re kattintani hallgatás helyett.
                        TButton {
                            objectName: "lineListen"
                            size: "small"
                            height: 24
                            leftPadding: 9; rightPadding: 10
                            radius: 5
                            iconName: "play"
                            iconSize: 12
                            spacing: 5
                            font.pixelSize: Theme.fontCaption
                            text: qsTr("Meghallgatom")
                            enabled: row.tab.canPlay
                            onClicked: row.tab.playLine(row.startMs, row.endMs)
                        }
                        Item {
                            width: 22; height: 24
                            Rectangle {
                                anchors.centerIn: parent
                                width: 1; height: 14
                                color: Theme.border
                            }
                        }
                        TButton {
                            objectName: "lineConfirm"
                            size: "small"
                            variant: "ghost"
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
                            objectName: "lineConfirmNoisy"
                            size: "small"
                            variant: "ghost"
                            height: 24
                            leftPadding: 9; rightPadding: 9
                            radius: 5
                            font.pixelSize: Theme.fontCaption
                            text: qsTr("Jó így, de nem minta")
                            toolTipText: qsTr("Jó így, de ne használd mintának: a beszélő rendben van, de egymásra beszéltek, ezért a sor hangja nem lesz minta")
                            onClicked: row.vm.confirmRowNoisy(row.index)
                        }
                        TButton {
                            objectName: "lineLikely"
                            visible: row.likelySpeakerKey !== ""
                            size: "small"
                            variant: "ghost"
                            height: 24
                            leftPadding: 9; rightPadding: 9
                            radius: 5
                            iconName: "arrow-right"
                            iconSize: 12
                            spacing: 5
                            font.pixelSize: Theme.fontCaption
                            text: qsTr("%1 mondta").arg(row.likelySpeakerName)
                            toolTipText: qsTr("Hangra %1 sorának tűnik: a sor átkerül hozzá").arg(row.likelySpeakerName)
                            onClicked: row.vm.moveUtteranceToSpeaker(row.vm.rowInfo(row.index).utteranceId, row.likelySpeakerKey)
                        }
                        TButton {
                            id: fixButton
                            objectName: "lineFix"
                            size: "small"
                            variant: "ghost"
                            height: 24
                            leftPadding: 9; rightPadding: 9
                            radius: 5
                            iconName: "user"
                            iconSize: 12
                            spacing: 5
                            font.pixelSize: Theme.fontCaption
                            text: qsTr("Más mondta…")
                            toolTipText: qsTr("Csak ez a sor kerül át ahhoz, akit választasz")
                            onClicked: row.tab.openLinePopover(row.index, fixButton)
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
}
