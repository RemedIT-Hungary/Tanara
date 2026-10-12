import QtQuick
import QtQuick.Layouts

// Térkép-dokk (design/handoff-v3 V4, 12. döntés): a lejátszó fölött beszélőnként egy 13 px-es
// sor (név 104 px a beszélő tintájával · 8 px-es sáv · beszédarány %), accent lejátszás-vonal,
// a lista épp látható szakasza halvány sávként; alatta a 44 px-es vezérlősor (előző sor,
// lejátszás, következő sor, idő, tipp, sebesség, hangerő). A térkép a keresősáv: kattintás = ugrás; a név = a beszélő menüje.
// Az Átirat (Olvasás és Javítás) és az összefoglaló fülek alján látszik.
// A név-oszlop végén (rögzített helyen, a sávot nem tolja el) a hanglenyomat-jel: teli zöld =
// van hanglenyomata, halvány körvonal = elnevezett, de még nincs, névtelennél áttetsző és nem
// kattintható; kattintásra a hanglenyomat panelje (TranscriptTab.openVoiceprintPopover).
//
// lanes: a TranscriptEditorViewModel.overview sorai ({ key, name, colorIndex, pct, segments,
// marks, nameDuplicate, rawLabel }; colorIndex < 0 = „Egyéb (N)”).
// sourceMarks / sectionBands: az összefoglaló fülek extra sora (U4: SummaryViewModel), elemei
// { startMs, endMs, active } — „Forrás” (accent = aktív állítás, accentLine = a többi; a
// beszélő-sávok ilyenkor 45 %-ra halványulnak) ill. „Szakaszok” (aktív szakasz accent).
// similarMarks: az új személyhez hasonló sorok (E4; TranscriptTab.similarMarks, [x, w, x, w, …]
// 0..1 az idővonalon): accent jelek a sávok fölött + halvány accent oszlop a sávokon át, hogy a
// beszélőtől függetlenül látsszon, hol vannak (a sorok a saját sávjukon is accent jelet kapnak).
// sampleStats (S2, minta-mérő): a TranscriptEditorViewModel.sampleStats elemei ({ key, sampleLines,
// sessionAdded, durationText, … }); ha nem üres, a sorok végén „23 minta +4” (a „+4” = ebben a
// munkamenetben lett minta, accent), tooltipben az idő; kattintásra samplesClicked(key) — a
// Javítás ezekre a sorokra szűr.
// Önállóan (képernyőkép) kitalált mintával rajzol; demoState: "" | "similarMarks" (E4 jelek) |
// "sampleMeter" (minta-mérő).
Rectangle {
    id: root

    property var player: null                // PlayerController vagy null
    property var lanes: demoLanes
    property real viewportStart: 0.31
    property real viewportSize: 0.05
    property int collapsedCount: 0
    property bool lanesExpanded: false
    property var sourceMarks: []
    property var sectionBands: []
    property var similarMarks: demoState === "similarMarks" ? demoSimilarMarks : []
    property var sampleStats: demoState === "sampleMeter" ? demoSampleStats : []
    property string demoState: ""
    property int timelineMs: player ? player.durationMs : 2 * 3600000 + 11 * 60000 + 4000

    signal seekRequested(real fraction)
    signal speakerClicked(string speakerKey, Item anchor)
    signal voiceprintClicked(string speakerKey, Item anchor)
    signal expandRequested()
    signal collapseRequested()
    signal lineStepRequested(int delta)     // „Előző sor” / „Következő sor” (a vezérlősorban)
    signal samplesClicked(string speakerKey) // a minta-mérő „+N most” része: szűrés ezekre a sorokra

    readonly property real nameWidth: 104
    readonly property real trackX: Theme.space5 + nameWidth + 10
    readonly property bool meterShown: sampleStats.length > 0
    readonly property real meterWidth: meterShown ? 92 : 0
    readonly property real trackWidth: Math.max(10, width - trackX - 10 - 30 - meterWidth - Theme.space5)
    function sampleOf(key) {
        for (let i = 0; i < sampleStats.length; ++i) if (sampleStats[i].key === key) return sampleStats[i]
        return null
    }
    readonly property real playFraction: !player ? 0.33 : timelineMs > 0 ? Math.min(1, player.positionMs / timelineMs) : 0
    readonly property bool dimLanes: sourceMarks.length > 0
    readonly property var extraRows: {
        const rows = []
        if (sourceMarks.length > 0) rows.push({ label: qsTr("Forrás"), items: sourceMarks, band: false })
        if (sectionBands.length > 0) rows.push({ label: qsTr("Szakaszok"), items: sectionBands, band: true })
        return rows
    }

    // Önálló képernyőképhez (V4 nevei).
    readonly property var demoLanes: {
        const names = ["Kovács Lilla", "Fehér Gábor", "Varga Árpád", "Molnár Eszter", "Távoli 1"]
        const pct = [22, 26, 19, 15, 7]
        const out = []
        for (let i = 0; i < names.length; ++i) {
            const seg = []
            for (let k = 0; k < 40; ++k) {
                const start = ((k * 0.0249 + i * 0.0071 + (k * k * 0.0013 * (i + 1)) % 0.02) % 0.995)
                seg.push(start, 0.0025 + ((k * 7 + i * 3) % 4) * 0.002)
            }
            out.push({ key: "d" + i, name: names[i], colorIndex: i, pct: pct[i], segments: seg, marks: [] })
        }
        return out
    }

    readonly property var demoSampleStats: [
        { key: "d0", sampleLines: 32, sessionAdded: 1, durationText: qsTr("2 p 41 mp"), sessionAddedIds: [] },
        { key: "d1", sampleLines: 24, sessionAdded: 4, durationText: qsTr("1 p 12 mp"), sessionAddedIds: [] },
        { key: "d2", sampleLines: 18, sessionAdded: 0, durationText: qsTr("58 mp"), sessionAddedIds: [] },
        { key: "d3", sampleLines: 14, sessionAdded: 0, durationText: qsTr("41 mp"), sessionAddedIds: [] }
    ]
    readonly property var demoSimilarMarks: [0.06, 0.004, 0.17, 0.003, 0.29, 0.005, 0.41, 0.003, 0.48, 0.004,
                                             0.55, 0.003, 0.63, 0.004, 0.71, 0.003, 0.78, 0.005, 0.86, 0.003,
                                             0.91, 0.004, 0.97, 0.003]

    function fractionOf(ms) { return timelineMs > 0 ? Math.max(0, Math.min(1, ms / timelineMs)) : 0 }

    implicitHeight: lanesColumn.implicitHeight + 16 + controls.height
    color: Theme.surface

    TDivider { anchors { left: parent.left; right: parent.right; top: parent.top } }

    Column {
        id: lanesColumn
        x: Theme.space5
        y: 10
        width: root.width - 2 * Theme.space5
        spacing: 4

        Repeater {
            model: root.lanes
            Item {
                id: laneRow
                required property var modelData
                readonly property bool other: modelData.colorIndex < 0
                width: lanesColumn.width
                height: 13
                opacity: root.dimLanes ? 0.45 : 1
                readonly property string voiceprint: modelData.voiceprint || ""
                readonly property alias mark: voiceprintMark
                TLabel {
                    id: laneName
                    objectName: "overviewName"
                    width: laneRow.other ? root.nameWidth : root.nameWidth - 18
                    anchors.verticalCenter: parent.verticalCenter
                    text: laneRow.modelData.name
                          + (laneRow.modelData.nameDuplicate && laneRow.modelData.rawLabel
                             ? " · " + laneRow.modelData.rawLabel : "")
                    color: laneRow.other ? Theme.textMuted : Theme.speakerInk(laneRow.modelData.colorIndex)
                    font.pixelSize: 12
                    font.weight: Theme.weightSemiBold
                    font.underline: nameHover.hovered
                    elide: Text.ElideRight
                    HoverHandler { id: nameHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        onTapped: laneRow.other ? root.expandRequested()
                                                : root.speakerClicked(laneRow.modelData.key, laneName)
                    }
                    TToolTip {
                        visible: nameHover.hovered
                        text: laneRow.other
                              ? qsTr("%n keveset beszélő résztvevő — kattintásra külön sort kapnak", "", root.collapsedCount)
                              : qsTr("%1 — a beszélő menüje (átnevezés, összevonás, hanglenyomat)").arg(laneRow.modelData.name)
                    }
                }
                Item {
                    id: voiceprintMark
                    objectName: "overviewVoiceprint"
                    readonly property bool has: laneRow.voiceprint === "has"
                    readonly property bool anonymous: laneRow.voiceprint === "anonymous"
                    property string speakerKey: laneRow.modelData.key
                    property string voiceprint: laneRow.voiceprint
                    visible: laneRow.voiceprint !== ""
                    x: root.nameWidth - 16
                    width: 16; height: 14
                    anchors.verticalCenter: parent.verticalCenter
                    opacity: anonymous ? 0.4 : 1
                    activeFocusOnTab: !anonymous
                    Accessible.role: Accessible.Button
                    Accessible.name: voiceprintTip.text
                    Keys.onSpacePressed: if (!anonymous) root.voiceprintClicked(speakerKey, voiceprintMark)
                    Keys.onReturnPressed: if (!anonymous) root.voiceprintClicked(speakerKey, voiceprintMark)
                    Rectangle {
                        anchors.centerIn: parent
                        width: 13; height: 13; radius: 6.5
                        color: voiceprintMark.has ? Theme.success
                             : voiceprintHover.hovered && !voiceprintMark.anonymous
                               ? Theme.alpha(Theme.stateLayer, Theme.hoverOpacity) : "transparent"
                        TFocusRing { visible: voiceprintMark.activeFocus; targetRadius: parent.radius }
                    }
                    TIcon {
                        anchors.centerIn: parent
                        name: "fingerprint"
                        size: voiceprintMark.has ? 9 : 11
                        strokeWidth: voiceprintMark.has ? 2.5 : 2
                        color: voiceprintMark.has ? Theme.textOnSpeaker : Theme.textMuted
                    }
                    HoverHandler {
                        id: voiceprintHover
                        cursorShape: voiceprintMark.anonymous ? Qt.ArrowCursor : Qt.PointingHandCursor
                    }
                    TapHandler {
                        enabled: !voiceprintMark.anonymous
                        onTapped: root.voiceprintClicked(voiceprintMark.speakerKey, voiceprintMark)
                    }
                    TToolTip {
                        id: voiceprintTip
                        visible: voiceprintHover.hovered
                        text: voiceprintMark.has ? qsTr("Van hanglenyomata — kattintásra a részletek")
                            : voiceprintMark.anonymous
                              ? qsTr("Névtelen beszélő: hanglenyomat csak elnevezett beszélőhöz készíthető")
                              : qsTr("Még nincs hanglenyomata — kattintásra itt készíthető")
                    }
                }
                TranscriptLaneStrip {
                    x: root.trackX - Theme.space5
                    width: root.trackWidth
                    height: 8
                    anchors.verticalCenter: parent.verticalCenter
                    segments: laneRow.modelData.segments
                    marks: laneRow.modelData.marks || []
                    trackColor: Theme.sunken
                    color: laneRow.other ? Theme.borderStrong : Theme.speakerLine(laneRow.modelData.colorIndex)
                    markColor: Theme.accent
                }
                // Minta-mérő: „23 minta +4” (a +4 ebben a munkamenetben lett minta).
                Item {
                    id: meter
                    objectName: "sampleMeter"
                    readonly property var stat: root.meterShown && !laneRow.other ? root.sampleOf(laneRow.modelData.key) : null
                    readonly property string speakerKey: laneRow.modelData.key
                    readonly property int samples: stat ? stat.sampleLines : 0
                    readonly property int added: stat ? stat.sessionAdded : 0
                    visible: root.meterShown
                    x: parent.width - 30 - root.meterWidth
                    width: root.meterWidth - 8
                    height: 13
                    anchors.verticalCenter: parent.verticalCenter
                    Row {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 4
                        TLabel {
                            objectName: "sampleMeterCount"
                            anchors.verticalCenter: parent.verticalCenter
                            text: meter.stat ? qsTr("%n minta", "", meter.samples) : ""
                            mono: true
                            muted: true
                            font.pixelSize: Theme.fontMicro
                        }
                        TLabel {
                            objectName: "sampleMeterAdded"
                            visible: meter.added > 0
                            anchors.verticalCenter: parent.verticalCenter
                            text: "+" + meter.added
                            mono: true
                            color: Theme.accent
                            font.pixelSize: Theme.fontMicro
                            font.weight: Theme.weightSemiBold
                            font.underline: meterHover.hovered
                        }
                    }
                    HoverHandler { id: meterHover; cursorShape: meter.added > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor }
                    TapHandler {
                        enabled: meter.added > 0
                        onTapped: root.samplesClicked(meter.speakerKey)
                    }
                    TToolTip {
                        visible: meterHover.hovered && !!meter.stat
                        text: !meter.stat ? ""
                              : qsTr("%1 · %n minta-sor", "", meter.samples).arg(laneRow.modelData.name)
                                + " · " + meter.stat.durationText
                                + (meter.added > 0 ? " · " + qsTr("+%1 most — kattintásra ezek a sorok").arg(meter.added) : "")
                    }
                }
                TLabel {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: 30
                    horizontalAlignment: Text.AlignRight
                    text: laneRow.modelData.pct + "%"
                    mono: true
                    muted: true
                    font.pixelSize: Theme.fontMicro
                }
            }
        }
        // Kibontott „Egyéb” mellett: vissza az összecsukott nézethez.
        Item {
            visible: root.collapsedCount > 0 && root.lanesExpanded
            width: lanesColumn.width
            height: 13
            TLabel {
                objectName: "overviewCollapse"
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Keveset beszélők összecsukása")
                muted: true
                font.pixelSize: Theme.fontMicro
                font.weight: Theme.weightMedium
                font.underline: collapseHover.hovered
                HoverHandler { id: collapseHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: root.collapseRequested() }
            }
        }
        // Az összefoglaló fülek extra sorai (U4): „Forrás” jelölők / „Szakaszok” sávok.
        Repeater {
            model: root.extraRows
            Item {
                id: extraRow
                required property var modelData
                objectName: "mapExtraRow"
                width: lanesColumn.width
                height: 13
                TLabel {
                    width: root.nameWidth
                    anchors.verticalCenter: parent.verticalCenter
                    text: extraRow.modelData.label
                    color: Theme.accent
                    font.pixelSize: 12
                    font.weight: Theme.weightSemiBold
                }
                Rectangle {
                    x: root.trackX - Theme.space5
                    width: root.trackWidth
                    height: 8
                    anchors.verticalCenter: parent.verticalCenter
                    radius: 2
                    color: Theme.sunken
                    Repeater {
                        model: extraRow.modelData.items
                        Rectangle {
                            required property var modelData
                            x: root.fractionOf(modelData.startMs) * parent.width
                            width: Math.max(2, (root.fractionOf(modelData.endMs) - root.fractionOf(modelData.startMs)) * parent.width)
                            height: parent.height
                            radius: extraRow.modelData.band ? 2 : 1
                            color: modelData.active ? Theme.accent
                                 : extraRow.modelData.band ? Theme.alpha(Theme.accent, 0.25) : Theme.accentLine
                            border.width: extraRow.modelData.band ? 1 : 0
                            border.color: Theme.surface
                        }
                    }
                }
            }
        }
    }

    // Az új személyhez hasonló sorok (E4): halvány accent oszlop a sávokon át + accent jel fölöttük.
    Repeater {
        objectName: "mapSimilarMarks"
        model: Math.floor(root.similarMarks.length / 2)
        Item {
            id: similarMark
            required property int index
            objectName: "mapSimilarMark"
            readonly property real fx: root.similarMarks[2 * index]
            readonly property real fw: root.similarMarks[2 * index + 1]
            x: root.trackX + Math.round(fx * root.trackWidth)
            y: 3
            width: Math.max(2, Math.round(fw * root.trackWidth))
            height: lanesColumn.height + 9
            Rectangle {
                width: parent.width
                height: 5
                radius: 1
                color: Theme.accent
            }
            Rectangle {
                y: 7
                width: parent.width
                height: parent.height - 7
                color: Theme.alpha(Theme.accent, 0.16)
            }
        }
    }

    // A lista épp látható szakasza (7 % szövegszín) és a lejátszás-vonal (accent).
    Rectangle {
        visible: root.viewportSize > 0 && root.lanes.length > 0
        x: root.trackX + Math.round(root.viewportStart * root.trackWidth)
        y: 6
        width: Math.max(4, Math.round(root.viewportSize * root.trackWidth))
        height: lanesColumn.height + 8
        radius: 2
        color: Theme.alpha(Theme.text, 0.07)
    }
    Rectangle {
        objectName: "mapPlayhead"
        visible: root.lanes.length > 0
        x: root.trackX + Math.round(root.playFraction * root.trackWidth) - 1
        y: 4
        width: 2
        height: lanesColumn.height + 10
        color: Theme.accent
    }
    MouseArea {
        objectName: "mapSeekArea"
        x: root.trackX
        y: lanesColumn.y
        width: root.trackWidth
        height: lanesColumn.height
        cursorShape: Qt.PointingHandCursor
        onClicked: mouse => root.seekRequested(Math.max(0, Math.min(1, mouse.x / width)))
    }

    PlayerBar {
        id: controls
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 44
        player: root.player
        showSlider: false
        lineSteps: true
        onLineStepRequested: delta => root.lineStepRequested(delta)
        hint: qsTr("a beszélő-térkép a keresősáv: kattintás = ugrás, név = beszélő menü")
    }
}
