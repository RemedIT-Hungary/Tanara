import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// Memó szakaszokkal (S2): balra a tartalomjegyzék (MemoToc, 236 px), jobbra a szakaszok
// időrendben. Szakasz-fejléc: „N. cím” 19/600, alatta az időtartomány-chip (kattintás =
// lejátszás onnan) és a szakasz beszélői; utána a bevezető (folyó bekezdés, mondatonként
// idő-chippel), a törzs pontjai, majd az alszakaszok („ELHANGZOTT ÉRVEK”, „NYITOTT KÉRDÉS” —
// ez utóbbi pont nélkül, bekezdésként). Minden állítás után az idő-chip(ek); a chip a
// „Honnan jön ez?” felugrót nyitja, rámutatva az állítás kiemelődik (és a térkép „Forrás”
// sorában is). A kiemelt szakasz (vm.activeSection) a görgetést követi, és a térkép
// „Szakaszok” sávjában accent.
Item {
    id: root

    required property var vm           // SummaryViewModel
    property string meetingId: ""
    property var shell: null
    property var player: null
    property alias popover: sourcePopover

    property bool tracking: true       // a görgetés követi-e a kiemelt szakaszt
    readonly property bool showChips: vm.sourcesVisible
    // A „Nincs forrás” súgó horgonya (egy közös súgó).
    property Item tipAnchor: null

    function hover(sid, on) {
        if (sid === "" || sourcePopover.opened) return
        if (on) root.vm.activeStatementId = sid
        else if (root.vm.activeStatementId === sid) root.vm.activeStatementId = ""
    }
    function openSources(sid, anchor) {
        root.vm.activeStatementId = sid
        sourcePopover.openFor(sid, anchor)
    }
    // Egy bekezdés elemei: szavak (az állítás azonosítójával) és a mondat utáni idő-chipek.
    function buildTokens(rows, chips) {
        const out = []
        for (let r = 0; r < rows.length; ++r) {
            const row = rows[r]
            const words = row.text.split(/\s+/).filter(w => w.length > 0)
            const withChips = chips && row.spans.length > 0
            for (let i = 0; i < words.length; ++i)
                out.push({ kind: "word", text: words[i], sid: row.statementId, first: i === 0,
                           last: i === words.length - 1, chipNext: withChips && i === words.length - 1,
                           affected: row.affected, hasSources: row.hasSources })
            if (withChips)
                for (let k = 0; k < row.spans.length; ++k)
                    out.push({ kind: "chip", sid: row.statementId, span: row.spans[k], index: k,
                               last: k === row.spans.length - 1 })
        }
        return out
    }

    function scrollToSection(index) {
        const it = sections.itemAt(index)
        if (!it) return
        root.tracking = false
        const y = it.mapToItem(col, 0, 0).y + col.y - 18
        flick.contentY = Math.max(0, Math.min(y, flick.contentHeight - flick.height))
        root.vm.activeSection = index
        Qt.callLater(() => root.tracking = true)
    }
    function playFrom(index, ms) {
        root.vm.activeSection = index
        if (!root.player || ms < 0) return
        root.player.seek(ms)
        root.player.play()
    }
    function trackScroll() {
        if (!root.tracking) return
        let active = -1
        for (let i = 0; i < sections.count; ++i) {
            const it = sections.itemAt(i)
            if (!it) continue
            if (it.mapToItem(col, 0, 0).y + col.y <= flick.contentY + 40) active = i
            else break
        }
        root.vm.activeSection = Math.max(0, active)
    }
    function regenerate() {
        root.vm.note.commitDraft()
        if (root.shell) root.shell.startQuickSummary(root.meetingId)
    }

    FontMetrics {
        id: spaceMetrics
        font.family: Theme.fontSans
        font.pixelSize: 15
    }

    TToolTip {
        parent: root.tipAnchor
        visible: root.tipAnchor !== null && root.showChips
        text: qsTr("Nincs forrás: ehhez a modell nem jelölt időpontot.")
    }

    // Folyó bekezdés állításokból (a bevezető, ill. a „Nyitott kérdés” alszakasz): szavanként
    // egy Flow-ban, a mondat után az idő-chip(ek).
    component StatementParagraph: Flow {
        id: para
        property var rows: []
        readonly property var tokens: root.buildTokens(rows, root.showChips)
        Layout.fillWidth: true
        spacing: 0
        Repeater {
            model: para.tokens
            Item {
                id: token
                required property var modelData
                readonly property bool isWord: modelData.kind === "word"
                readonly property bool active: modelData.sid !== "" && modelData.sid === root.vm.activeStatementId
                readonly property real spaceWidth: spaceMetrics.advanceWidth(" ")
                width: isWord ? wordLabel.implicitWidth + (modelData.chipNext ? 0 : spaceWidth)
                              : chip.implicitWidth + 4 + 2 + (modelData.last ? spaceWidth : 0)
                height: 27

                Rectangle {
                    visible: token.isWord && (token.active && root.showChips || token.modelData.affected)
                    y: 3
                    width: token.isWord ? wordLabel.implicitWidth + (token.modelData.last ? 0 : token.spaceWidth) : 0
                    height: 22
                    color: token.active && root.showChips ? Theme.accentSoft : "transparent"
                    topLeftRadius: token.isWord && token.modelData.first ? 3 : 0
                    bottomLeftRadius: topLeftRadius
                    topRightRadius: token.isWord && token.modelData.last ? 3 : 0
                    bottomRightRadius: topRightRadius
                    Rectangle {
                        visible: token.isWord && token.modelData.affected
                        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                        height: 2
                        color: Theme.warnLine
                    }
                }
                TLabel {
                    id: wordLabel
                    visible: token.isWord
                    y: Math.round((27 - implicitHeight) / 2)
                    text: token.isWord ? token.modelData.text : ""
                    font.pixelSize: 15
                }
                HoverHandler {
                    enabled: token.isWord
                    onHoveredChanged: {
                        root.hover(token.modelData.sid, hovered)
                        if (hovered && !token.modelData.hasSources && token.modelData.sid !== "") root.tipAnchor = token
                        else if (!hovered && root.tipAnchor === token) root.tipAnchor = null
                    }
                }
                TimeChip {
                    id: chip
                    objectName: "memoTimeChip"
                    visible: !token.isWord
                    x: 4
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.verticalCenterOffset: -1
                    text: token.isWord ? "" : token.modelData.span.stamp
                    active: !token.isWord && token.active && !token.modelData.span.affected
                    warn: !token.isWord && token.modelData.span.affected
                    toolTipText: token.isWord ? "" : qsTr("Honnan jön ez? (%1)").arg(text)
                    onClicked: root.openSources(token.modelData.sid, chip)
                    onHoveredChanged: if (!token.isWord) root.hover(token.modelData.sid, hovered)
                }
            }
        }
    }

    // Egy memó-pont: pötty, szöveg, a sor végén (jobbra) az idő-chipek.
    component MemoPoint: Rectangle {
        id: point
        required property var modelData
        readonly property bool active: modelData.statementId !== "" && modelData.statementId === root.vm.activeStatementId
        objectName: "memoPoint"
        Layout.fillWidth: true
        Layout.leftMargin: -8
        implicitHeight: pointRow.implicitHeight + 8
        radius: Theme.radiusControl
        color: active && root.showChips ? Theme.accentSoft : "transparent"
        HoverHandler {
            onHoveredChanged: {
                root.hover(point.modelData.statementId, hovered)
                if (hovered && !point.modelData.hasSources && point.modelData.statementId !== "") root.tipAnchor = pointText
                else if (!hovered && root.tipAnchor === pointText) root.tipAnchor = null
            }
        }
        RowLayout {
            id: pointRow
            anchors { left: parent.left; right: parent.right; top: parent.top
                      leftMargin: 8; rightMargin: 8; topMargin: 4 }
            spacing: 10
            Rectangle {
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 10
                implicitWidth: 5; implicitHeight: 5; radius: 2.5
                color: Theme.textMuted
            }
            TLabel {
                id: pointText
                Layout.fillWidth: true
                text: point.modelData.text
                font.pixelSize: 15
                cssLineHeight: 1.65
                wrapMode: Text.Wrap
            }
            Row {
                visible: root.showChips && point.modelData.hasSources
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 4
                spacing: 3
                Repeater {
                    model: point.modelData.spans
                    TimeChip {
                        id: pointChip
                        required property var modelData
                        objectName: "memoTimeChip"
                        text: modelData.stamp
                        active: point.active && !modelData.affected
                        warn: modelData.affected
                        toolTipText: qsTr("Honnan jön ez? (%1)").arg(modelData.stamp)
                        onClicked: root.openSources(point.modelData.statementId, pointChip)
                    }
                }
            }
        }
    }

    // A képernyőkép / visszatérés: a kiemelt szakaszhoz görget.
    Component.onCompleted: if (vm.activeSection > 0) initialScroll.start()
    Timer {
        id: initialScroll
        interval: 60
        onTriggered: root.scrollToSection(root.vm.activeSection)
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        MemoToc {
            Layout.preferredWidth: 236
            Layout.fillHeight: true
            sections: root.vm.memo
            activeIndex: root.vm.activeSection
            onPicked: (index) => root.scrollToSection(index)
        }

        Flickable {
            id: flick
            objectName: "summaryMemoFlick"
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: col.implicitHeight + 18 + 28
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            T.ScrollBar.vertical: TScrollBar {}
            onContentYChanged: root.trackScroll()

            ColumnLayout {
                id: col
                x: 28; y: 18
                width: flick.width - 56
                spacing: 14

                SummaryStatusBanners {
                    Layout.fillWidth: true
                    vm: root.vm
                    meetingId: root.meetingId
                    shell: root.shell
                    showBlocker: false
                    onRetryRequested: root.regenerate()
                }

                TBanner {
                    visible: root.vm.stale
                    Layout.fillWidth: true
                    Layout.maximumWidth: 780
                    tone: "warn"
                    text: root.vm.staleCount > 0
                        ? qsTr("Az összefoglaló óta %n beszélőt javítottál, ezért a memó beszélői elavultak lehetnek.", "", root.vm.staleCount)
                        : qsTr("Az összefoglaló óta változtak a beszélők, ezért a memó beszélői elavultak lehetnek.")
                    TButton {
                        text: qsTr("Frissítés")
                        size: "small"
                        implicitHeight: 30
                        leftPadding: 12; rightPadding: 12
                        font.weight: Theme.weightSemiBold
                        enabled: root.vm.canRun && !root.vm.jobRunning
                        onClicked: root.regenerate()
                    }
                    TButton {
                        text: qsTr("Rendben így")
                        variant: "ghost"; size: "small"
                        muted: true
                        implicitHeight: 30
                        onClicked: root.vm.dismissStale()
                    }
                }

                Repeater {
                    id: sections
                    model: root.vm.memo
                    ColumnLayout {
                        id: section
                        required property var modelData
                        required property int index
                        objectName: "memoSection"
                        Layout.fillWidth: true
                        Layout.maximumWidth: 780
                        spacing: 5

                        TDivider { visible: section.index > 0; Layout.fillWidth: true; Layout.topMargin: 6; Layout.bottomMargin: 14 }
                        TLabel {
                            Layout.fillWidth: true
                            text: (section.index + 1) + ". " + (section.modelData.title !== "" ? section.modelData.title : qsTr("Egyéb"))
                            font.pixelSize: 19
                            font.weight: Theme.weightSemiBold
                            wrapMode: Text.Wrap
                        }
                        RowLayout {
                            visible: section.modelData.sourceRange !== "" || section.modelData.speakersText !== ""
                            Layout.fillWidth: true
                            spacing: 8
                            TimeChip {
                                objectName: "sectionRangeChip"
                                visible: section.modelData.sourceRange !== ""
                                large: true
                                iconName: "play"
                                text: section.modelData.sourceRange
                                toolTipText: qsTr("Lejátszás innen: %1").arg(section.modelData.sourceRange.split("–")[0])
                                onClicked: root.playFrom(section.index, section.modelData.startMs)
                            }
                            TLabel {
                                Layout.fillWidth: true
                                text: section.modelData.speakersText
                                muted: true
                                font.pixelSize: Theme.fontSmall
                                elide: Text.ElideRight
                            }
                        }
                        // Bevezető: folyó bekezdés, mondatonként chippel.
                        StatementParagraph {
                            objectName: "memoIntro"
                            visible: rows.length > 0
                            Layout.topMargin: 6
                            rows: section.modelData.introRows || []
                        }
                        // A törzs pontjai, utána az alszakaszok (cím + pontok; a nyitott kérdés bekezdés).
                        Repeater {
                            model: section.modelData.blocks || []
                            ColumnLayout {
                                id: block
                                required property var modelData
                                objectName: "memoBlock"
                                Layout.fillWidth: true
                                Layout.topMargin: modelData.title !== "" ? 10 : 2
                                spacing: 2
                                TSectionLabel {
                                    objectName: "memoBlockTitle"
                                    visible: block.modelData.title !== ""
                                    Layout.bottomMargin: 4
                                    text: block.modelData.title
                                }
                                StatementParagraph {
                                    visible: block.modelData.key === "open"
                                    rows: block.modelData.key === "open" ? block.modelData.rows : []
                                }
                                Repeater {
                                    model: block.modelData.key === "open" ? [] : block.modelData.rows
                                    MemoPoint {}
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    SourcePopover {
        id: sourcePopover
        vm: root.vm
        player: root.player
        shell: root.shell
        meetingId: root.meetingId
    }
}
