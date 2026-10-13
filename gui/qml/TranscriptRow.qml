import QtQuick

// One row of the transcript list: an utterance (the name line only at a speaker change and on a
// flagged line, the text below it), or the filter's "... N sure lines hidden" divider. On the
// left (when the rail is visible) the slice of the speaker columns that falls on this row; the
// rail's mouse handling lives in TranscriptTab (one shared layer so a drag can span rows).
// Clicking the NAME corrects this line's speaker (LinePopover; the whole speaker from the rail
// header / the map: SpeakerWhyPopover).
// In Correct mode (rail visible) the name line carries markers: check (confirmed), "short",
// "uncertain - track | voice | tag", "corrected", "new person", "talked over each other", on
// decided lines the sample mark (solid wave = sample, struck through = not a sample) and the
// "? unidentifiable" marker; an unidentifiable line is faded (not hatched).
// Sweep mode: the stopped line (playback stopped here) gets an accent left stripe and the
// decision bar (SweepDecisionBar) below it; in continuous mode the retroactive-window line gets
// a dashed accent border (it is the keys' target line, not the playing one); "stopped" /
// "retroactive window" markers on the right.
Item {
    id: row

    // ---- from the model ----
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
    required property bool sweepHandled
    required property bool selected
    required property bool suggested
    required property bool suggestionAnchor
    required property int hiddenCount
    required property int startMs
    required property int endMs

    // ---- from TranscriptTab ----
    required property var tab               // TranscriptTab (state + actions)
    required property var vm                // TranscriptEditorViewModel

    readonly property bool gap: kind === "gap"
    readonly property bool playing: !gap && vm.playingRow === index
    readonly property bool nameOpen: tab.speakerPopoverRow === index
    readonly property real textX: tab.textX
    readonly property bool inDrag: tab.dragActive && index >= Math.min(tab.dragFromRow, tab.dragToRow)
                                   && index <= Math.max(tab.dragFromRow, tab.dragToRow)
    // Preview (drag or hover): the target column's cell on this row.
    readonly property bool inPreview: inDrag || (!tab.dragActive && tab.hoverRow === index)
    readonly property alias nameItem: nameBox
    // ---- sweep mode ----
    readonly property bool sweepOn: tab.sweepOn
    readonly property bool sweepStopped: sweepOn && !gap && vm.sweepStopRow === index
    readonly property bool sweepAfter: sweepOn && !gap && vm.sweepAfterRow === index
    readonly property bool sweepUpcoming: sweepOn && uncertain && !ignored && !sweepHandled && !sweepStopped && !sweepAfter

    width: ListView.view ? ListView.view.width : 0
    height: gap ? 24 : body.height

    // ---- divider (filter) ----
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

    // The rail's right edge.
    Rectangle {
        visible: row.tab.railShown && !row.gap
        x: row.textX - 1
        width: 1
        height: row.height
        color: Theme.border
    }

    // ---- utterance ----
    Item {
        id: body
        visible: !row.gap
        width: row.width
        height: row.gap ? 0 : textColumn.y + textColumn.height + 6

        // An "unidentifiable" line is faded (its rail cell and its text).
        opacity: row.ignored && !row.selected ? 0.5 : 1

        // Row background: played = accentSoft; selected = raised + 1.5 px accent border.
        Rectangle {
            anchors.fill: parent
            visible: row.playing || row.selected || row.sweepStopped
            color: row.selected ? Theme.raised : Theme.accentSoft
            border.width: row.selected ? 1.5 : 0
            border.color: Theme.accent
        }
        // Sweep: the stopped line's left accent stripe; the retroactive-window line has a dashed border.
        Rectangle {
            objectName: "sweepStopBar"
            visible: row.sweepStopped
            x: row.textX
            width: 3
            height: parent.height
            color: Theme.accent
        }
        TDashedRect {
            objectName: "sweepAfterFrame"
            visible: row.sweepAfter
            x: row.textX + 4
            y: 2
            width: parent.width - x - 6
            height: parent.height - 4
            radius: Theme.radiusControl
            color: Theme.accent
            lineWidth: 1.5
        }

        // The rail slice that falls on this row.
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

        // Click on the text: select (Ctrl / Shift: several lines); double click: play from here.
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

            // Name line: name (clickable: whose line this is) · timestamp · markers.
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

                    // ---- markers (in Correct mode; when reading only the uncertain / corrected ones) ----
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
                        // On new-person lines "corrected" is implied (the "new person" marker says it).
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

                    // Sample mark on decided lines: solid wave = became a voice sample, struck through = not a sample.
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
                    // "Talked over each other": a subdued flag; the line is not a voice sample (on a decided
                    // line the struck-through wave says so).
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
                    // Undoing the flag: only on hover, so it does not clutter the list.
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
                // Sweep: the stop markers on the right.
                TPill {
                    objectName: "sweepStopPill"
                    visible: row.sweepStopped || row.sweepAfter || row.sweepUpcoming
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    tone: row.sweepStopped || row.sweepAfter ? "accent" : "warn"
                    text: row.sweepStopped ? qsTr("megálló · itt állt meg")
                        : row.sweepAfter ? qsTr("utólagos ablak · a billentyűk ide")
                        : qsTr("megálló")
                }
            }

            TLabel {
                width: parent.width
                text: row.richText !== "" ? row.richText : row.lineText
                textFormat: row.richText !== "" ? Text.RichText : Text.PlainText
                wrapMode: Text.Wrap
                cssLineHeight: Theme.transcriptLineHeight
            }
            // Sweep, stopped: the decision bar below the row.
            Loader {
                objectName: "sweepDecisionLoader"
                active: row.sweepStopped
                visible: active
                width: parent.width
                sourceComponent: SweepDecisionBar {
                    objectName: "sweepDecisionBar"
                    width: textColumn.width
                    tab: row.tab
                    vm: row.vm
                    rowIndex: row.index
                    speakerKey: row.speakerKey
                    ownLane: row.lane
                    likelyKey: row.likelySpeakerKey
                    likelyName: row.likelySpeakerName
                }
            }
        }

        // For a continuation paragraph (no name line) the timestamp appears on hover.
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
