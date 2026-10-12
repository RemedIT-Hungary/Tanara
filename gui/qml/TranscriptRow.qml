import QtQuick

// Az átirat-lista egy sora: megszólalás (névsor csak beszélőváltáskor és jelölt sornál, alatta a
// szöveg), vagy a szűrő „··· N biztos sor elrejtve" elválasztója. Bal oldalt (ha a sín látszik) a
// beszélő-oszlopok ezen sorra eső szelete; a sín egérkezelése a TranscriptTab-ban van (egy közös
// réteg, hogy a húzás több soron át is működjön). A NÉVRE kattintva ennek a sornak a beszélője
// javítható (LinePopover; a teljes beszélő a sín-fejlécről / a térképről: SpeakerWhyPopover).
// Javítás módban (sín látszik) a névsor jelölői (handoff-v3 E1): pipa (megerősítve), „rövid",
// „bizonytalan · sáv | hang | címke", „javítva", „új személy", „egymásra beszéltek", és (S2) a
// döntött sorokon a minta-jel (tömör hullám = minta, áthúzott = nem minta), valamint a
// „? nem azonosítható" jelölő; a nem azonosítható sor halvány (nem sraffozott).
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
    required property string uncertainReason
    required property bool confirmed
    required property bool isShort
    required property bool sideConflict
    required property bool newPerson
    required property bool similar
    required property bool ignored
    required property string sampleState
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
    // Előnézet (húzás vagy rámutatás): a cél-oszlop cellája ezen a soron.
    readonly property bool inPreview: inDrag || (!tab.dragActive && tab.hoverRow === index)
    readonly property alias nameItem: nameBox

    width: ListView.view ? ListView.view.width : 0
    height: gap ? 24 : body.height

    // ---- elválasztó (szűrő) ----
    readonly property string gapText: {
        const f = vm.listFilter
        if (f === "samples") return qsTr("··· %n sor elrejtve", "", row.hiddenCount)
        if (f === "hideIgnored") return qsTr("··· %n nem azonosítható sor elrejtve", "", row.hiddenCount)
        return qsTr("··· %n biztos sor elrejtve", "", row.hiddenCount)
    }
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
                text: row.gapText
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

        // A „nem azonosítható" sor halvány (a sín-cellája és a szövege is).
        opacity: row.ignored && !row.selected ? 0.5 : 1

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
                suggestLane: row.similar ? row.tab.newPersonLane : -1
                dimmed: row.inPreview && row.tab.previewLane >= 0 && row.tab.previewLane !== row.lane
                targetLane: row.inPreview && row.tab.previewLane !== row.lane ? row.tab.previewLane : -1
                targetDashed: row.tab.shiftHeld
                targetLineColor: Theme.speakerLine(row.tab.previewColorIndex)
                targetSoftColor: Theme.speakerSoft(row.tab.previewColorIndex)
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

            // Névsor: név (kattintható: ez a sor kié) · időbélyeg · jelölők. (A soronkénti döntés
            // gombjai az Átnézendő nézet „Egyenként" soraiban vannak: ReviewGroupsView.)
            Item {
                visible: row.head
                width: parent.width
                height: row.head ? 18 : 0

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

                    // ---- jelölők (Javítás módban; olvasáskor csak a bizonytalan / javítva) ----
                    TranscriptMarker {
                        objectName: "newPersonMarker"
                        visible: row.newPerson && row.tab.railShown
                        anchors.verticalCenter: parent.verticalCenter
                        kind: "newPerson"
                        toolTipText: qsTr("Most vetted fel: a hozzá hasonló hangú sorok kék kerettel látszanak a sínen")
                    }
                    TranscriptMarker {
                        id: statePill
                        objectName: "statePill"
                        // Az új személy sorainál a „javítva" magától értetődik (az „új személy" mondja).
                        visible: row.uncertain || (row.corrected && !(row.newPerson && row.tab.railShown))
                        anchors.verticalCenter: parent.verticalCenter
                        kind: row.uncertain ? "uncertain" : "corrected"
                        reason: row.uncertainReason
                        toolTipText: !row.uncertain ? qsTr("Kézzel javítottad a beszélőjét")
                                   : row.uncertainReason === "side"
                                     ? qsTr("A sor a beszélője oldalával ellentétes sávon szólt (mikrofon / hívás hangja)")
                                   : row.likelySpeakerName !== "" ? qsTr("Hangra inkább %1 sorának tűnik").arg(row.likelySpeakerName)
                                   : row.uncertainReason === "tag" ? qsTr("Hangra kétes, és a beszélő címkéi nem illenek a megbeszéléshez")
                                   : qsTr("Hangra kétes, kié a sor")
                    }
                    TranscriptMarker {
                        objectName: "ignoredMarker"
                        visible: row.ignored
                        anchors.verticalCenter: parent.verticalCenter
                        kind: "ignored"
                        toolTipText: qsTr("Nem azonosítható: a név marad, de a hangját semmire nem használom (minta, bizonyíték, átnézendő). Visszavonás: 0 újra, vagy Ctrl+Z")
                    }
                    TranscriptMarker {
                        objectName: "confirmedMarker"
                        visible: row.confirmed && !row.uncertain && row.tab.railShown
                        anchors.verticalCenter: parent.verticalCenter
                        kind: "confirmed"
                        toolTipText: qsTr("Megerősítetted: a beszélő rendben van")
                    }
                    TranscriptMarker {
                        objectName: "shortMarker"
                        visible: row.isShort && !row.uncertain && !row.corrected && row.tab.railShown
                        anchors.verticalCenter: parent.verticalCenter
                        kind: "short"
                        toolTipText: qsTr("Túl rövid a hang-ellenőrzéshez: csak fülre dönthető el, kié")
                    }

                    // Minta-jel a döntött sorokon: tömör hullám = hangminta lett, áthúzott = nem minta.
                    TranscriptMarker {
                        objectName: "sampleMarker"
                        visible: row.sampleState !== "" && row.tab.railShown
                        anchors.verticalCenter: parent.verticalCenter
                        kind: row.sampleState === "sample" ? "sample" : "noSample"
                        toolTipText: row.sampleState === "sample"
                                     ? qsTr("Hangminta: a sor hangja tanítja %1 felismerését").arg(row.speakerName)
                                     : row.noisy ? qsTr("Nem minta: egymásra beszéltek / zajos, a hangját nem használom")
                                     : qsTr("Nem minta: túl rövid ahhoz, hogy a hangja minta legyen")
                    }
                    // „Egymásra beszéltek": visszafogott jelzés; a sor nem hangminta (a döntött
                    // soron az áthúzott hullám mondja ki).
                    TranscriptMarker {
                        id: noisyPill
                        objectName: "noisyPill"
                        visible: row.noisy && (row.sampleState === "" || !row.tab.railShown)
                        anchors.verticalCenter: parent.verticalCenter
                        kind: "noisy"
                        toolTipText: row.noisyOverlap
                              ? qsTr("Ebben a sorban más is beszél egyszerre, ezért a hangját nem használom mintának (bizonytalanság-jelzés, hanglenyomat).")
                              : qsTr("Megjelölted, hogy ezt a sort ne használjam hangmintának (bizonytalanság-jelzés, hanglenyomat).")
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
