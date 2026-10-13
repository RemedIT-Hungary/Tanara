import QtQuick
import QtQuick.Layouts

// Map dock: above the player, one 13 px row per speaker (name 104 px in the speaker's tint · 8 px
// bar · talk share %), an accent playback line, the list's currently visible part as a faint
// band; below it the 44 px control row (previous line, play, next line, time, hint, speed,
// volume). The map is the seek bar: click = jump; the name = the speaker's menu. Shown at the
// bottom of the Transcript (Read and Fix) and summary tabs.
// At the end of the name column (fixed place, does not shift the bar) is the voiceprint mark:
// solid green = has a voiceprint, faint outline = named but none yet, translucent and not
// clickable for an anonymous speaker; a click opens the voiceprint panel
// (TranscriptTab.openVoiceprintPopover).
//
// lanes: rows of TranscriptEditorViewModel.overview ({ key, name, colorIndex, pct, segments,
// marks, nameDuplicate, rawLabel }; colorIndex < 0 = "Other (N)").
// sourceMarks / sectionBands: the summary tabs' extra row (SummaryViewModel), items
// { startMs, endMs, active } -- "Source" (accent = active claim, accentLine = the rest; the
// speaker bars fade to 45% then) or "Sections" (active section accent).
// similarMarks: lines similar to the new person (TranscriptTab.similarMarks, [x, w, x, w, ...]
// 0..1 on the timeline): accent marks above the bars + a faint accent column through the bars so
// it is visible where they are regardless of speaker (the lines also get an accent mark on
// their own bar).
// sampleStats (sample meter): items of TranscriptEditorViewModel.sampleStats ({ key, sampleLines,
// sessionAdded, durationText, ... }); when not empty, "23 samples +4" at the row end ("+4" = became
// a sample in this session, accent), the duration in the tooltip; a click emits samplesClicked(key)
// -- Fix filters to those lines.
// sweep (sweep mode): TranscriptEditorViewModel.sweepDock ({ uncertain: [x...], decided: [x...],
// frozenStart, frozenEnd, sections: [{ start, end, done, active }], doneCount, total, aheadCount });
// when not empty, below the bars a "Doubtful" row (dots: warn = doubtful ahead, green = decided) and
// a "Sections" row (check = done, accent = current), plus the frozen window's band across the bars.
// Standalone (screenshot) it draws made-up sample data; demoState: "" | "similarMarks" |
// "sampleMeter" | "sweep".
Rectangle {
    id: root

    property var player: null                // PlayerController or null
    property var lanes: demoLanes
    property real viewportStart: 0.31
    property real viewportSize: 0.05
    property int collapsedCount: 0
    property bool lanesExpanded: false
    property var sourceMarks: []
    property var sectionBands: []
    property var similarMarks: demoState === "similarMarks" ? demoSimilarMarks : []
    property var sampleStats: demoState === "sampleMeter" || demoState === "sweep" ? demoSampleStats : []
    property var sweep: demoState === "sweep" ? demoSweep : ({})
    readonly property bool sweepShown: !!sweep && sweep.sections !== undefined
    property string demoState: ""
    property int timelineMs: player ? player.durationMs : 2 * 3600000 + 11 * 60000 + 4000

    signal seekRequested(real fraction)
    signal speakerClicked(string speakerKey, Item anchor)
    signal voiceprintClicked(string speakerKey, Item anchor)
    signal expandRequested()
    signal collapseRequested()
    signal lineStepRequested(int delta)     // "Previous line" / "Next line" (in the control row)
    signal samplesClicked(string speakerKey) // the sample meter's "+N now" part: filter to those lines

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

    // For standalone screenshots (V4 names).
    readonly property var demoLanes: {
        const names = [DemoText.name("Kovács Lilla"), DemoText.name("Fehér Gábor"), DemoText.name("Varga Árpád"), DemoText.name("Molnár Eszter"), DemoText.raw("Távoli 1")]
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
    readonly property var demoSweep: {
        const unc = [], dec = []
        for (let k = 0; k < 60; ++k) {
            const x = (k * 0.0163 + (k * k * 0.0007) % 0.012) % 0.99
            if (x < 0.27) dec.push(x)
            else unc.push(x)
        }
        const sections = []
        for (let i = 0; i < 11; ++i)
            sections.push({ start: i / 11, end: (i + 1) / 11, done: i < 2, active: i === 2 })
        return { uncertain: unc, decided: dec, frozenStart: 0.305, frozenEnd: 0.345, sections: sections,
                 doneCount: 2, total: 11, aheadCount: unc.length }
    }
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
                // Sample meter: "23 samples +4" (+4 became a sample in this session).
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
        // Next to the expanded "Other": back to the collapsed view.
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
        // Sweep: the dots of doubtful lines and the sections.
        Item {
            id: sweepDotsRow
            objectName: "mapSweepDots"
            visible: root.sweepShown
            width: lanesColumn.width
            height: 13
            TLabel {
                width: root.nameWidth
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Kétes")
                color: Theme.warnInk
                font.pixelSize: 12
                font.weight: Theme.weightSemiBold
            }
            Item {
                x: root.trackX - Theme.space5
                width: root.trackWidth
                height: parent.height
                Rectangle { anchors.verticalCenter: parent.verticalCenter; width: parent.width; height: 1; color: Theme.border }
                Repeater {
                    model: root.sweepShown ? root.sweep.decided : []
                    Rectangle {
                        required property var modelData
                        x: Math.round(modelData * parent.width) - 2
                        anchors.verticalCenter: parent.verticalCenter
                        width: 4; height: 4; radius: 2
                        color: Theme.success
                    }
                }
                Repeater {
                    objectName: "mapSweepDotRepeater"
                    model: root.sweepShown ? root.sweep.uncertain : []
                    Rectangle {
                        required property var modelData
                        objectName: "mapSweepDot"
                        x: Math.round(modelData * parent.width) - 3
                        anchors.verticalCenter: parent.verticalCenter
                        width: 6; height: 6; radius: 3
                        color: Theme.warn
                    }
                }
            }
            TLabel {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: root.sweepShown ? qsTr("%1 előtted").arg(root.sweep.aheadCount || 0) : ""
                mono: true
                muted: true
                font.pixelSize: Theme.fontMicro
            }
        }
        Item {
            id: sweepSectionsRow
            objectName: "mapSweepSections"
            visible: root.sweepShown
            width: lanesColumn.width
            height: 13
            TLabel {
                width: root.nameWidth
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Szakaszok")
                color: Theme.accent
                font.pixelSize: 12
                font.weight: Theme.weightSemiBold
            }
            Item {
                x: root.trackX - Theme.space5
                width: root.trackWidth
                height: parent.height
                Repeater {
                    model: root.sweepShown ? root.sweep.sections : []
                    Rectangle {
                        id: sectionBox
                        required property var modelData
                        objectName: "mapSweepSection"
                        x: Math.round(modelData.start * parent.width) + 1
                        width: Math.max(3, Math.round((modelData.end - modelData.start) * parent.width) - 2)
                        anchors.verticalCenter: parent.verticalCenter
                        height: 11
                        radius: 2
                        color: modelData.done ? Theme.successSoft : modelData.active ? Theme.accentSoft : "transparent"
                        border.width: 1
                        border.color: modelData.done ? Theme.successLine : modelData.active ? Theme.accent : Theme.border
                        TIcon {
                            visible: sectionBox.modelData.done && sectionBox.width > 16
                            anchors.centerIn: parent
                            name: "check"
                            size: 9
                            strokeWidth: 2.5
                            color: Theme.successInk
                        }
                    }
                }
            }
            TLabel {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: root.sweepShown ? qsTr("%1 / %2 kész").arg(root.sweep.doneCount || 0).arg(root.sweep.total || 0) : ""
                mono: true
                muted: true
                font.pixelSize: Theme.fontMicro
            }
        }
        // The summary tabs' extra rows: "Source" marks / "Sections" bars.
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

    // Lines similar to the new person: a faint accent column through the bars + an accent mark above.
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

    // The list's currently visible part (7% text color) and the playback line (accent).
    Rectangle {
        visible: root.viewportSize > 0 && root.lanes.length > 0
        x: root.trackX + Math.round(root.viewportStart * root.trackWidth)
        y: 6
        width: Math.max(4, Math.round(root.viewportSize * root.trackWidth))
        height: lanesColumn.height + 8
        radius: 2
        color: Theme.alpha(Theme.text, 0.07)
    }
    // Sweep: the frozen window (visible lines +-2) across the bars.
    Rectangle {
        objectName: "mapFrozenWindow"
        visible: root.sweepShown && (root.sweep.frozenEnd || 0) > (root.sweep.frozenStart || 0)
        x: root.trackX + Math.round((root.sweep.frozenStart || 0) * root.trackWidth)
        y: 5
        width: Math.max(4, Math.round(((root.sweep.frozenEnd || 0) - (root.sweep.frozenStart || 0)) * root.trackWidth))
        height: lanesColumn.height + 9
        radius: 2
        color: Theme.alpha(Theme.accent, 0.10)
        border.width: 1
        border.color: Theme.accentLine
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
        hint: root.sweepShown ? qsTr("Space lejátszás · ←/→ sor eleje / köv. megálló · Shift+←/→ sor · [ ] tempó")
                              : qsTr("a beszélő-térkép a keresősáv: kattintás = ugrás, név = beszélő menü")
    }
}
