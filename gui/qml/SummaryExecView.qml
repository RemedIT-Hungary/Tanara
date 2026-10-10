import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// Vezetői összefoglaló forrás-hivatkozásokkal (S1, célzott elavulással S3). Csak forrásos
// gyors összefoglalónál (vm.hasStatements); régi összefoglalónál a SummaryDocView marad.
//
// Bal oldalt a szöveg: a mondatok egy folyó bekezdésben, mindegyik után a forrás-idő-chip(ek);
// rámutatva a mondat accentSoft hátteret kap és a térkép „Forrás” sorában kigyullad. A chip
// kattintásra (Enter / Szóköz) a „Honnan jön ez?” felugrót nyitja. Döntések és teendők: a
// sor végén a chipek; teendőnél a felelős-chip a beszélő színében. Elavultnál (S3) a banner
// megszámolja az érintett állításokat / teendőket, az érintett mondat 2 px warnLine
// aláhúzást, az érintett forrás warn chipet, az érintett felelős szaggatott „X → Y?” chipet
// kap. Jelölő nélküli állítás: nincs chip, rámutatva „Nincs forrás”.
// Jobb oldalt (250 px) a résztvevők beszédidő-aránnyal, egy rövid magyarázat, a megbeszélés-
// megjegyzés és a megmaradt műveletek (Témánkénti elemzés, Megnyitás mappában).
Item {
    id: root

    required property var vm           // SummaryViewModel
    property string meetingId: ""
    property var shell: null
    property var player: null
    property alias popover: sourcePopover

    readonly property bool showChips: vm.sourcesVisible
    readonly property var paragraphTokens: buildTokens(vm.sentences.rows, showChips)
    // A „Nincs forrás” súgó horgonya (egyetlen közös súgó az összes szóhoz).
    property Item tipAnchor: null

    // A bekezdés elemei: szavak (a mondat azonosítójával) és a mondat utáni idő-chipek.
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
    function hover(sid, on) {
        if (sourcePopover.opened) return   // nyitott felugrónál az ő állítása marad kijelölve
        if (on) root.vm.activeStatementId = sid
        else if (root.vm.activeStatementId === sid && !(sourcePopover.opened && sourcePopover.statementId === sid))
            root.vm.activeStatementId = ""
    }
    function openSources(sid, anchor) {
        root.vm.activeStatementId = sid
        sourcePopover.openFor(sid, anchor)
    }
    function regenerate() {
        root.vm.note.commitDraft()
        if (root.shell) root.shell.startQuickSummary(root.meetingId)
    }
    function staleDetail() {
        const s = root.vm.affectedStatements, t = root.vm.affectedTodos
        if (!root.vm.staleTargeted)
            return qsTr("A felelősök és a résztvevők elavultak lehetnek.")
        if (s > 0 && t > 0)
            return qsTr("%1 és %2 forrásában más lett a beszélő; ezeket jelöltük.")
                .arg(qsTr("%n állítás", "", s)).arg(qsTr("%n teendő", "", t))
        if (s > 0) return qsTr("%n állítás forrásában más lett a beszélő; ezeket jelöltük.", "", s)
        if (t > 0) return qsTr("%n teendő forrásában más lett a beszélő; ezeket jelöltük.", "", t)
        return qsTr("Egyik állítás forrását sem érinti.")
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

    // Egy állítás a döntések / teendők közt: pont, szöveg, (felelős, határidő), idő-chipek.
    component StatementItem: Rectangle {
        id: item
        required property var modelData
        readonly property bool active: modelData.statementId === root.vm.activeStatementId
        objectName: "statementItem"
        Layout.fillWidth: true
        Layout.maximumWidth: 780
        Layout.leftMargin: -8
        implicitHeight: itemRow.implicitHeight + 12
        radius: Theme.radiusControl
        color: active && root.showChips ? Theme.accentSoft : "transparent"
        HoverHandler {
            id: itemHover
            onHoveredChanged: {
                root.hover(item.modelData.statementId, hovered)
                if (hovered && !item.modelData.hasSources) root.tipAnchor = itemText
                else if (!hovered && root.tipAnchor === itemText) root.tipAnchor = null
            }
        }
        RowLayout {
            id: itemRow
            anchors { left: parent.left; right: parent.right; top: parent.top
                      leftMargin: 8; rightMargin: 8; topMargin: 6 }
            spacing: 10
            Rectangle {
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 9
                implicitWidth: 5; implicitHeight: 5; radius: 2.5
                color: Theme.textMuted
            }
            TLabel {
                id: itemText
                Layout.fillWidth: true
                text: item.modelData.text
                font.pixelSize: 15
                cssLineHeight: 1.55
                wrapMode: Text.Wrap
            }
            Flow {
                visible: item.modelData.kind === "todo" && (item.modelData.owners.length > 0 || item.modelData.ownerStale !== "")
                Layout.alignment: Qt.AlignTop
                Layout.maximumWidth: 240
                spacing: 4
                SummaryOwnerChip {
                    objectName: "ownerStaleChip"
                    visible: item.modelData.ownerStale !== ""
                    compact: true
                    warn: true
                    name: item.modelData.ownerStale
                    dotIndex: item.modelData.ownerStaleIndex
                    maximumWidth: 240
                }
                Repeater {
                    model: item.modelData.ownerStale === "" ? item.modelData.owners : []
                    SummaryOwnerChip {
                        required property var modelData
                        compact: true
                        name: modelData.name
                        speakerIndex: modelData.index
                        maximumWidth: 200
                    }
                }
            }
            TLabel {
                visible: item.modelData.kind === "todo" && item.modelData.due !== ""
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 2
                text: item.modelData.due
                mono: true; muted: true
                font.pixelSize: Theme.fontCaption
            }
            Row {
                visible: root.showChips && item.modelData.hasSources
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 2
                spacing: 3
                Repeater {
                    model: item.modelData.spans
                    TimeChip {
                        id: spanChip
                        required property var modelData
                        objectName: "timeChip"
                        text: modelData.stamp
                        active: item.active && !modelData.affected
                        warn: modelData.affected
                        toolTipText: qsTr("Honnan jön ez? (%1)").arg(modelData.stamp)
                        onClicked: root.openSources(item.modelData.statementId, spanChip)
                    }
                }
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Flickable {
            id: flick
            objectName: "summaryExecFlick"
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: col.implicitHeight + 18 + 28
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            T.ScrollBar.vertical: TScrollBar {}

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

                // ---- célzott elavulás (S3) ----
                Rectangle {
                    objectName: "staleBanner"
                    visible: root.vm.stale
                    Layout.fillWidth: true
                    Layout.maximumWidth: 780
                    implicitHeight: staleRow.implicitHeight + 20
                    radius: Theme.radiusPopup
                    color: Theme.warnSoft
                    border.width: 1
                    border.color: Theme.warnLine
                    RowLayout {
                        id: staleRow
                        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter
                                  leftMargin: 14; rightMargin: 14 }
                        spacing: 12
                        TIcon { name: "triangle-alert"; size: 16; color: Theme.warnInk }
                        TLabel {
                            objectName: "staleText"
                            Layout.fillWidth: true
                            textFormat: Text.StyledText
                            text: "<b>" + (root.vm.staleCount > 0
                                  ? qsTr("Az összefoglaló óta %n beszélőt javítottál.", "", root.vm.staleCount)
                                  : qsTr("Az összefoglaló óta változtak a beszélők.")) + "</b> " + root.staleDetail()
                            font.pixelSize: Theme.fontBody
                            cssLineHeight: 1.4
                            wrapMode: Text.Wrap
                        }
                        TButton {
                            objectName: "staleRefresh"
                            text: qsTr("Frissítés")
                            size: "small"
                            implicitHeight: 30
                            leftPadding: 12; rightPadding: 12
                            font.weight: Theme.weightSemiBold
                            enabled: root.vm.canRun && !root.vm.jobRunning
                            onClicked: root.regenerate()
                        }
                        TButton {
                            objectName: "staleDismiss"
                            text: qsTr("Rendben így")
                            variant: "ghost"; size: "small"
                            muted: true
                            implicitHeight: 30
                            onClicked: root.vm.dismissStale()
                        }
                    }
                }

                // ---- a vezetői összefoglaló mondatai, folyó bekezdésben ----
                Flow {
                    id: paragraph
                    objectName: "execParagraph"
                    Layout.fillWidth: true
                    Layout.maximumWidth: 760
                    Layout.bottomMargin: 4
                    spacing: 0
                    Repeater {
                        model: root.paragraphTokens
                        Item {
                            id: token
                            required property var modelData
                            readonly property bool isWord: modelData.kind === "word"
                            readonly property bool active: modelData.sid === root.vm.activeStatementId
                            readonly property real spaceWidth: spaceMetrics.advanceWidth(" ")
                            width: isWord ? wordLabel.implicitWidth + (modelData.chipNext ? 0 : spaceWidth)
                                          : chip.implicitWidth + 4 + 2 + (modelData.last ? spaceWidth : 0)
                            height: 26

                            // Szó: háttér (kijelölve) + aláhúzás (elavult forrás).
                            Rectangle {
                                visible: token.isWord && (token.active && root.showChips || token.modelData.affected)
                                y: 3
                                width: token.isWord ? wordLabel.implicitWidth + (token.modelData.last ? 0 : token.spaceWidth) : 0
                                height: 21
                                color: token.active && root.showChips ? Theme.accentSoft : "transparent"
                                topLeftRadius: token.isWord && token.modelData.first ? 3 : 0
                                bottomLeftRadius: topLeftRadius
                                topRightRadius: token.isWord && token.modelData.last ? 3 : 0
                                bottomRightRadius: topRightRadius
                                Rectangle {
                                    objectName: "staleUnderline"
                                    visible: token.isWord && token.modelData.affected
                                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                                    height: 2
                                    color: Theme.warnLine
                                }
                            }
                            TLabel {
                                id: wordLabel
                                visible: token.isWord
                                y: Math.round((26 - implicitHeight) / 2)
                                text: token.isWord ? token.modelData.text : ""
                                font.pixelSize: 15
                            }
                            HoverHandler {
                                enabled: token.isWord
                                onHoveredChanged: {
                                    root.hover(token.modelData.sid, hovered)
                                    if (hovered && !token.modelData.hasSources) root.tipAnchor = token
                                    else if (!hovered && root.tipAnchor === token) root.tipAnchor = null
                                }
                            }
                            TimeChip {
                                id: chip
                                objectName: "timeChip"
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

                // ---- döntések ----
                ColumnLayout {
                    visible: root.vm.decisionItems.count > 0
                    Layout.fillWidth: true
                    spacing: 7
                    TSectionLabel { Layout.topMargin: 4; text: qsTr("Döntések") }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Repeater {
                            model: root.vm.decisionItems.rows
                            StatementItem {}
                        }
                    }
                }

                // ---- nyitott kérdések (nem állítás: forrás nélkül) ----
                ColumnLayout {
                    visible: root.vm.openQuestions.length > 0
                    Layout.fillWidth: true
                    Layout.maximumWidth: 780
                    spacing: 7
                    TSectionLabel { Layout.topMargin: 4; text: qsTr("Nyitott kérdések") }
                    Repeater {
                        model: root.vm.openQuestions
                        RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 10
                            Rectangle {
                                Layout.alignment: Qt.AlignTop
                                Layout.topMargin: 9
                                implicitWidth: 5; implicitHeight: 5; radius: 2.5
                                color: Theme.textMuted
                            }
                            TLabel {
                                Layout.fillWidth: true
                                text: parent.modelData.text
                                font.pixelSize: 15
                                cssLineHeight: 1.55
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }

                // ---- teendők ----
                ColumnLayout {
                    visible: root.vm.todoItems.count > 0
                    Layout.fillWidth: true
                    spacing: 7
                    TSectionLabel { Layout.topMargin: 4; text: qsTr("Teendők") }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Repeater {
                            model: root.vm.todoItems.rows
                            StatementItem {}
                        }
                    }
                }
            }
        }

        // ================= jobb oszlop =================
        Rectangle {
            Layout.preferredWidth: 250
            Layout.fillHeight: true
            color: "transparent"
            Rectangle { anchors { top: parent.top; bottom: parent.bottom; left: parent.left } width: 1; color: Theme.border }

            Flickable {
                id: side
                anchors { fill: parent; leftMargin: 1 }
                contentWidth: width
                contentHeight: sideCol.implicitHeight + 36
                boundsBehavior: Flickable.StopAtBounds
                clip: true
                T.ScrollBar.vertical: TScrollBar {}

                ColumnLayout {
                    id: sideCol
                    x: 18; y: 18
                    width: side.width - 36
                    spacing: 10

                    TSectionLabel { text: qsTr("Résztvevők") }
                    Repeater {
                        model: root.vm.participants
                        RowLayout {
                            id: person
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 9
                            TAvatar {
                                visible: person.modelData.colorIndex >= 0
                                size: 22
                                border.width: 1.5
                                name: person.modelData.name
                                speakerIndex: Math.max(0, person.modelData.colorIndex)
                            }
                            Rectangle {
                                visible: person.modelData.colorIndex < 0
                                implicitWidth: 22; implicitHeight: 22; radius: 11
                                color: Theme.sunken
                                border.width: 1
                                border.color: Theme.borderStrong
                                TIcon { anchors.centerIn: parent; name: "user"; size: 11; color: Theme.textMuted }
                            }
                            TLabel {
                                Layout.fillWidth: true
                                text: person.modelData.name
                                font.pixelSize: Theme.fontBody
                                elide: Text.ElideRight
                            }
                            TLabel {
                                visible: person.modelData.percent >= 0
                                text: person.modelData.percent + "%"
                                mono: true; muted: true
                                font.pixelSize: Theme.fontCaption
                            }
                        }
                    }
                    TDivider { Layout.fillWidth: true; Layout.topMargin: 6 }
                    TLabel {
                        Layout.fillWidth: true
                        Layout.topMargin: 2
                        text: qsTr("A források a megerősített beszélőkre mutatnak. Ha az Átiratban javítasz, az érintett állítások jelölést kapnak.")
                        muted: true
                        font.pixelSize: Theme.fontSmall
                        cssLineHeight: 1.55
                        wrapMode: Text.Wrap
                    }

                    // ---- megbeszélés-megjegyzés (összecsukható) ----
                    TDivider { Layout.fillWidth: true; Layout.topMargin: 8 }
                    T.AbstractButton {
                        id: noteToggle
                        Layout.fillWidth: true
                        implicitHeight: 28
                        hoverEnabled: true
                        Accessible.name: qsTr("Megjegyzés a megbeszéléshez")
                        onClicked: root.vm.noteOpen = !root.vm.noteOpen
                        background: Rectangle {
                            radius: Theme.radiusControl
                            color: Theme.stateLayer
                            opacity: noteToggle.down ? Theme.pressedOpacity : noteToggle.hovered ? Theme.hoverOpacity : 0
                            TFocusRing { visible: noteToggle.visualFocus }
                        }
                        contentItem: RowLayout {
                            spacing: 8
                            TIcon { name: "pencil"; size: 14; color: Theme.textMuted }
                            TLabel {
                                Layout.fillWidth: true
                                text: qsTr("Megjegyzés a megbeszéléshez")
                                font.pixelSize: Theme.fontSmall
                                font.weight: Theme.weightMedium
                                elide: Text.ElideRight
                            }
                            TIcon { name: root.vm.noteOpen ? "chevron-up" : "chevron-down"; size: 14; color: Theme.textMuted }
                        }
                    }
                    TLabel {
                        visible: !root.vm.noteOpen
                        Layout.fillWidth: true
                        text: root.vm.note.note.trim() !== ""
                              ? root.vm.note.note.trim()
                              : qsTr("Nincs megjegyzés. Ide írhatod a neveket, szakszavakat és a félrehallott szavak helyes alakját.")
                        muted: true
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.Wrap
                        maximumLineCount: 3
                        elide: Text.ElideRight
                    }
                    MeetingNoteEditor {
                        visible: root.vm.noteOpen
                        Layout.fillWidth: true
                        compact: true
                        model: root.vm.note
                        fieldHeight: 110
                        helperText: qsTr("Nevek, szakszavak, ismert félrehallások (A „…” helyesen: …). Az újragenerált összefoglaló és egy újra-átírás is ezt kapja.")
                    }
                    RowLayout {
                        visible: root.vm.noteChangedSinceSummary
                        Layout.fillWidth: true
                        spacing: 6
                        TIcon { Layout.alignment: Qt.AlignTop; Layout.topMargin: 2; name: "info"; size: 13; color: Theme.textMuted }
                        TLabel {
                            Layout.fillWidth: true
                            text: qsTr("A megjegyzés azóta változott; újrageneráláskor már az új számít.")
                            muted: true
                            font.pixelSize: Theme.fontCaption
                            wrapMode: Text.Wrap
                        }
                    }

                    TDivider { Layout.fillWidth: true; Layout.topMargin: 4 }
                    TButton {
                        Layout.fillWidth: true
                        text: qsTr("Témánkénti elemzés")
                        variant: "ghost"; size: "small"
                        leftPadding: 8; rightPadding: 8
                        iconName: "list-tree"
                        horizontalAlignment: Qt.AlignLeft
                        enabled: root.vm.hasTopics || root.vm.canRun
                        toolTipText: enabled ? "" : (root.vm.blocker.reason || "")
                        onClicked: {
                            if (root.vm.hasTopics) root.vm.topicsOpen = true
                            else if (root.shell) root.shell.startTopicExtraction(root.meetingId)
                        }
                    }
                    TButton {
                        Layout.fillWidth: true
                        text: qsTr("Megnyitás mappában")
                        variant: "ghost"; size: "small"
                        leftPadding: 8; rightPadding: 8
                        iconName: "folder-open"
                        horizontalAlignment: Qt.AlignLeft
                        onClicked: if (root.shell) root.shell.revealInFolder(root.meetingId)
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
