import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// Summary tab: a finished summary. On top the stale bar (if a speaker was fixed since it was made),
// two columns: content on the left, on the right the participants with talk share, the
// generation details and the actions.
//
// The quick summary has two forms, switched at the top of the left column:
//   "Executive summary" (default): overview, decisions, open questions, to-dos;
//   "Memo": chronological notes per section (title, time range -> jump in the player and the
//   transcript, points), with a table of contents for many sections.
// For an old (pre-memo) summary a short explanation + "Regenerate" replaces the memo.
// A per-topic summary has no switch: the topic sections follow the executive part.
//
// The right column holds the meeting note's collapsible block (collapsed by default: the start
// shows); editable when open, with template suggestions. If it changed since the summary, a gentle
// hint sits under "Regenerate" (it does not make the meeting stale in the library).
Flickable {
    id: root

    required property var vm           // SummaryViewModel
    property string meetingId: ""
    property var shell: null

    readonly property bool narrow: width < 720
    readonly property bool hasSwitch: vm.memoState !== "none"
    readonly property bool showMemo: hasSwitch && vm.section === "memo"
    // Table of contents: only if the memo can no longer be taken in at a glance.
    readonly property bool tocVisible: vm.memo.length >= 6
    readonly property bool anyStamp: {
        for (let i = 0; i < vm.decisions.length; ++i)
            if (vm.decisions[i].ms >= 0) return true
        for (let j = 0; j < vm.openQuestions.length; ++j)
            if (vm.openQuestions[j].ms >= 0) return true
        return false
    }

    contentWidth: width
    contentHeight: col.implicitHeight + 18 + 24
    boundsBehavior: Flickable.StopAtBounds
    clip: true
    T.ScrollBar.vertical: TScrollBar {}

    function regenerate() {
        noteEditor.commit()   // a pending note draft must go into the new run
        // For a per-topic summary the topics must be re-analyzed -> the workspace opens.
        if (vm.mode === "topics" && vm.hasTopics) vm.topicsOpen = true
        else if (root.shell) root.shell.startQuickSummary(root.meetingId)
    }
    function openTopics() {
        if (vm.hasTopics) vm.topicsOpen = true
        else if (root.shell) root.shell.startTopicExtraction(root.meetingId)
    }
    function seek(ms) {
        if (root.shell && ms >= 0) root.shell.seekTo(root.meetingId, ms)
    }
    function copy(part) {
        if (!root.vm.copyToClipboard(part) || !root.shell) return
        root.shell.toast(part === "exec" ? qsTr("A vezetői összefoglaló a vágólapra került.")
                       : part === "memo" ? qsTr("A memó a vágólapra került.")
                       : qsTr("Az összefoglaló a vágólapra került."))
    }
    // Scroll to a memo section (from the table of contents).
    function scrollToSection(index) {
        const it = memoRepeater.itemAt(index)
        if (!it) return
        const y = it.mapToItem(col, 0, 0).y + col.y - 12
        root.contentY = Math.max(0, Math.min(y, root.contentHeight - root.height))
    }

    // Clickable timestamp (accent, monospace): jumps the player and the transcript there.
    component StampLink: Item {
        id: link
        property string text: ""
        property real ms: -1
        implicitWidth: linkLabel.implicitWidth
        implicitHeight: 21
        TLabel {
            id: linkLabel
            anchors.verticalCenter: parent.verticalCenter
            text: link.text
            mono: true
            font.pixelSize: Theme.fontCaption
            font.weight: Theme.weightMedium
            font.underline: linkHover.hovered || linkFocus.activeFocus
            color: Theme.accent
        }
        FocusScope {
            id: linkFocus
            anchors.fill: linkLabel
            activeFocusOnTab: true
            Accessible.role: Accessible.Link
            Accessible.name: qsTr("Ugrás ide: %1").arg(link.text)
            Keys.onReturnPressed: root.seek(link.ms)
            Keys.onSpacePressed: root.seek(link.ms)
            HoverHandler { id: linkHover; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: root.seek(link.ms) }
        }
    }

    // Small dot at the start of a list item.
    component Bullet: Rectangle {
        implicitWidth: 4; implicitHeight: 4; radius: 2
        color: Theme.borderStrong
        Layout.alignment: Qt.AlignTop
        Layout.topMargin: 9
        Layout.leftMargin: 2
    }

    // List with timestamps (decisions, open questions): the timestamp column exists only
    // if the data really has a timestamp.
    component StampedList: ColumnLayout {
        id: stamped
        property string heading: ""
        property var items: []             // [{ text, ms, stamp }]
        visible: items.length > 0
        spacing: 8
        TSectionLabel { text: stamped.heading }
        Repeater {
            model: stamped.items
            RowLayout {
                id: stampedRow
                required property var modelData
                Layout.fillWidth: true
                spacing: 12
                Item {
                    visible: root.anyStamp
                    implicitWidth: 44
                    implicitHeight: 21
                    Layout.alignment: Qt.AlignTop
                    StampLink {
                        visible: stampedRow.modelData.ms >= 0
                        anchors.verticalCenter: parent.verticalCenter
                        text: stampedRow.modelData.stamp
                        ms: stampedRow.modelData.ms
                    }
                }
                Bullet { visible: !root.anyStamp }
                TLabel {
                    Layout.fillWidth: true
                    text: stampedRow.modelData.text
                    cssLineHeight: 1.5
                    wrapMode: Text.Wrap
                }
            }
        }
    }

    // Bulleted list (decisions in topic sections, to-dos with an owner).
    component PointList: ColumnLayout {
        property string heading: ""
        property var items: []             // string or { text, owner, due }
        visible: items.length > 0
        spacing: 4
        TLabel {
            text: parent.heading
            muted: true
            font.pixelSize: Theme.fontSmall
            font.weight: Theme.weightSemiBold
        }
        Repeater {
            model: parent.items
            RowLayout {
                required property var modelData
                readonly property bool plain: typeof modelData === "string"
                Layout.fillWidth: true
                spacing: 8
                Bullet {}
                TLabel {
                    Layout.fillWidth: true
                    text: parent.plain ? parent.modelData
                        : parent.modelData.text
                          + (parent.modelData.owner ? " — " + parent.modelData.owner : "")
                          + (parent.modelData.due ? " (" + parent.modelData.due + ")" : "")
                    cssLineHeight: 1.5
                    wrapMode: Text.Wrap
                }
            }
        }
    }

    ColumnLayout {
        id: col
        x: 24; y: 18
        width: root.width - 48
        spacing: 16

        SummaryStatusBanners {
            Layout.fillWidth: true
            vm: root.vm
            meetingId: root.meetingId
            shell: root.shell
            showBlocker: false
            onRetryRequested: root.regenerate()
        }

        // ---- stale bar ----
        TBanner {
            visible: root.vm.stale
            Layout.fillWidth: true
            tone: "warn"
            text: root.vm.staleCount > 0
                ? qsTr("Az összefoglaló óta %n beszélőt javítottál, ezért a felelősök és a résztvevők elavultak lehetnek.", "", root.vm.staleCount)
                : qsTr("Az összefoglaló óta változtak a beszélők, ezért a felelősök és a résztvevők elavultak lehetnek.")
            TButton {
                text: root.vm.mode === "topics" && root.vm.hasTopics ? qsTr("Témák újraelemzése") : qsTr("Frissítés")
                size: "small"
                implicitHeight: 30
                leftPadding: 12; rightPadding: 12
                font.weight: Theme.weightSemiBold
                enabled: !root.vm.jobRunning
                onClicked: root.regenerate()
            }
            TButton {
                text: qsTr("Rendben így")
                variant: "ghost"; size: "small"
                implicitHeight: 30
                onClicked: root.vm.dismissStale()
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: root.narrow ? 1 : 2
            columnSpacing: 32
            rowSpacing: 24

            // ================= left column: content =================
            ColumnLayout {
                id: leftCol
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: 18

                // ---- switch between the two forms (the short form is the default) ----
                SettingsSegmented {
                    visible: root.hasSwitch
                    Layout.bottomMargin: -2
                    value: root.vm.section
                    options: [
                        { value: "exec", label: qsTr("Vezetői összefoglaló") },
                        { value: "memo", label: root.vm.memoState === "ready"
                                                ? qsTr("Memó · %n szakasz", "", root.vm.memo.length)
                                                : qsTr("Memó") },
                    ]
                    onPicked: (v) => { root.vm.section = v; root.contentY = 0 }
                }

                // ================= executive summary =================
                ColumnLayout {
                    visible: !root.showMemo && root.vm.execSummary !== ""
                    Layout.fillWidth: true
                    spacing: 6
                    // The switch already names the part; a label only without it (per-topic mode).
                    TSectionLabel { visible: !root.hasSwitch; text: qsTr("Vezetői összefoglaló") }
                    TLabel {
                        Layout.fillWidth: true
                        // Qt puts the extra of a fixed line height ABOVE the line (CSS splits it) ->
                        // we shift up 3 px so the gap between the label and the next section is right.
                        Layout.topMargin: -3
                        Layout.bottomMargin: 3
                        text: root.vm.execSummary
                        textFormat: Text.MarkdownText
                        font.pixelSize: 15
                        cssLineHeight: 1.6
                        wrapMode: Text.Wrap
                    }
                }

                StampedList {
                    visible: !root.showMemo && items.length > 0
                    Layout.fillWidth: true
                    heading: qsTr("Döntések")
                    items: root.vm.decisions
                }

                StampedList {
                    visible: !root.showMemo && items.length > 0
                    Layout.fillWidth: true
                    heading: qsTr("Nyitott kérdések")
                    items: root.vm.openQuestions
                }

                ColumnLayout {
                    visible: !root.showMemo && root.vm.actions.length > 0
                    Layout.fillWidth: true
                    spacing: 6
                    TSectionLabel { text: qsTr("Teendők") }
                    Repeater {
                        model: root.vm.actions
                        ColumnLayout {
                            id: actionRow
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 0
                            TDivider { Layout.fillWidth: true }
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.topMargin: 7
                                Layout.bottomMargin: 7
                                spacing: 12
                                // To-dos have no "done" state in the data -> a bullet, not a checkbox.
                                Item {
                                    implicitWidth: 16; implicitHeight: 20
                                    Layout.alignment: Qt.AlignVCenter
                                    Rectangle {
                                        anchors.centerIn: parent
                                        width: 6; height: 6; radius: 3
                                        color: Theme.borderStrong
                                    }
                                }
                                TLabel {
                                    Layout.fillWidth: true
                                    text: actionRow.modelData.text
                                    cssLineHeight: 1.4
                                    wrapMode: Text.Wrap
                                }
                                // Several owners -> several chips (they wrap onto more lines when
                                // narrow).
                                Flow {
                                    visible: actionRow.modelData.owners.length > 0
                                    Layout.alignment: Qt.AlignVCenter
                                    Layout.maximumWidth: Math.max(160, col.width * 0.3)
                                    spacing: 4
                                    Repeater {
                                        model: actionRow.modelData.owners
                                        SummaryOwnerChip {
                                            required property var modelData
                                            name: modelData.name
                                            speakerIndex: modelData.index
                                            maximumWidth: Math.max(160, col.width * 0.3)
                                        }
                                    }
                                }
                                TLabel {
                                    visible: actionRow.modelData.due !== ""
                                    Layout.minimumWidth: 64
                                    Layout.maximumWidth: 110
                                    Layout.alignment: Qt.AlignVCenter
                                    text: actionRow.modelData.due
                                    mono: true; muted: true
                                    font.pixelSize: Theme.fontCaption
                                    horizontalAlignment: Text.AlignRight
                                    wrapMode: Text.Wrap
                                }
                            }
                        }
                    }
                }

                // ---- per-topic summary: topic sections ----
                ColumnLayout {
                    visible: !root.showMemo && root.vm.topicSections.length > 0
                    Layout.fillWidth: true
                    spacing: 14
                    TSectionLabel { text: qsTr("Témák") }
                    Repeater {
                        model: root.vm.topicSections
                        ColumnLayout {
                            id: section
                            required property var modelData
                            required property int index
                            Layout.fillWidth: true
                            spacing: 6
                            TDivider { visible: section.index > 0; Layout.fillWidth: true; Layout.bottomMargin: 8 }
                            TLabel {
                                Layout.fillWidth: true
                                text: section.modelData.title
                                font.pixelSize: 15
                                font.weight: Theme.weightSemiBold
                                wrapMode: Text.Wrap
                            }
                            TLabel {
                                visible: text !== ""
                                Layout.fillWidth: true
                                text: section.modelData.detail
                                textFormat: Text.MarkdownText
                                cssLineHeight: 1.5
                                wrapMode: Text.Wrap
                            }
                            PointList {
                                Layout.fillWidth: true
                                Layout.topMargin: 2
                                heading: qsTr("Döntések")
                                items: section.modelData.decisions
                            }
                            PointList {
                                Layout.fillWidth: true
                                Layout.topMargin: 2
                                heading: qsTr("Nyitott kérdések")
                                items: section.modelData.openQuestions || []
                            }
                            PointList {
                                Layout.fillWidth: true
                                Layout.topMargin: 2
                                heading: qsTr("Teendők")
                                items: section.modelData.actions
                            }
                        }
                    }
                }

                // ================= memo =================
                // Old summary: an explanation + regenerate in place of the memo.
                Rectangle {
                    visible: root.showMemo && root.vm.memoState === "missing"
                    Layout.fillWidth: true
                    Layout.maximumWidth: 640
                    implicitHeight: missingCol.implicitHeight + 36
                    radius: Theme.radiusPopup
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.border
                    ColumnLayout {
                        id: missingCol
                        anchors { fill: parent; leftMargin: 20; rightMargin: 20; topMargin: 18; bottomMargin: 18 }
                        spacing: 10
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            TIcon { name: "file-text"; size: 16; color: Theme.textMuted }
                            TLabel {
                                Layout.fillWidth: true
                                text: qsTr("Ehhez az összefoglalóhoz nincs memó")
                                font.weight: Theme.weightSemiBold
                                wrapMode: Text.Wrap
                            }
                        }
                        TLabel {
                            Layout.fillWidth: true
                            text: qsTr("Ez az összefoglaló még a memó bevezetése előtt készült. Újragenerálva a vezetői összefoglaló mellé időrendi, szakaszonkénti memó is készül.")
                            muted: true
                            cssLineHeight: 1.5
                            wrapMode: Text.Wrap
                        }
                        TButton {
                            Layout.topMargin: 4
                            text: qsTr("Újragenerálás")
                            size: "small"
                            implicitHeight: 30
                            leftPadding: 12; rightPadding: 12
                            iconName: "rotate-ccw"
                            iconSize: 13
                            enabled: root.vm.canRun && !root.vm.jobRunning
                            toolTipText: root.vm.canRun ? "" : (root.vm.blocker.reason || "")
                            onClicked: root.regenerate()
                        }
                    }
                }

                // Table of contents: two columns, top to bottom within each column.
                ColumnLayout {
                    visible: root.showMemo && root.tocVisible
                    Layout.fillWidth: true
                    spacing: 4
                    TSectionLabel { text: qsTr("Tartalom") }
                    GridLayout {
                        id: toc
                        readonly property int cols: leftCol.width >= 560 ? 2 : 1
                        Layout.fillWidth: true
                        flow: GridLayout.TopToBottom
                        rows: Math.ceil(root.vm.memo.length / cols)
                        columnSpacing: 24
                        rowSpacing: 0
                        Repeater {
                            model: root.showMemo && root.tocVisible ? root.vm.memo : []
                            T.AbstractButton {
                                id: tocEntry
                                required property var modelData
                                required property int index
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1      // equal columns
                                implicitHeight: 26
                                hoverEnabled: true
                                activeFocusOnTab: true
                                Accessible.name: modelData.title
                                onClicked: root.scrollToSection(index)
                                Keys.onReturnPressed: click()
                                background: Item { TFocusRing { visible: tocEntry.visualFocus; targetRadius: 4 } }
                                contentItem: RowLayout {
                                    spacing: 10
                                    TLabel {
                                        Layout.preferredWidth: 52
                                        text: tocEntry.modelData.stamp
                                        mono: true; muted: true
                                        font.pixelSize: Theme.fontCaption
                                    }
                                    TLabel {
                                        Layout.fillWidth: true
                                        text: tocEntry.modelData.title
                                        font.pixelSize: Theme.fontSmall
                                        font.underline: tocEntry.hovered
                                        color: tocEntry.hovered ? Theme.accent : Theme.text
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }
                    }
                    TDivider { Layout.fillWidth: true; Layout.topMargin: 10 }
                }

                // The sections: title, time range (jump), points.
                ColumnLayout {
                    visible: root.showMemo && root.vm.memoState === "ready"
                    Layout.fillWidth: true
                    spacing: 16
                    Repeater {
                        id: memoRepeater
                        model: root.showMemo ? root.vm.memo : []
                        ColumnLayout {
                            id: memoSection
                            required property var modelData
                            required property int index
                            Layout.fillWidth: true
                            spacing: 6
                            TDivider { visible: memoSection.index > 0; Layout.fillWidth: true; Layout.bottomMargin: 10 }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 12
                                TLabel {
                                    Layout.fillWidth: true
                                    text: memoSection.modelData.title !== "" ? memoSection.modelData.title
                                                                            : qsTr("Egyéb")
                                    font.pixelSize: 15
                                    font.weight: Theme.weightSemiBold
                                    wrapMode: Text.Wrap
                                }
                                StampLink {
                                    visible: memoSection.modelData.startMs >= 0
                                    Layout.alignment: Qt.AlignTop
                                    text: memoSection.modelData.range
                                    ms: memoSection.modelData.startMs
                                }
                            }
                            Repeater {
                                model: memoSection.modelData.points
                                RowLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Bullet {}
                                    TLabel {
                                        Layout.fillWidth: true
                                        text: parent.modelData
                                        cssLineHeight: 1.5
                                        wrapMode: Text.Wrap
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ================= right column: participants, metadata, actions =================
            ColumnLayout {
                Layout.preferredWidth: 250
                Layout.maximumWidth: root.narrow ? col.width : 250
                Layout.fillWidth: root.narrow
                Layout.alignment: Qt.AlignTop
                spacing: 16

                ColumnLayout {
                    visible: root.vm.participants.length > 0
                    Layout.fillWidth: true
                    spacing: 8
                    TSectionLabel { text: qsTr("Résztvevők") }
                    Repeater {
                        model: root.vm.participants
                        RowLayout {
                            id: person
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 10
                            TAvatar {
                                visible: person.modelData.colorIndex >= 0
                                name: person.modelData.name
                                speakerIndex: Math.max(0, person.modelData.colorIndex)
                            }
                            // The summary mentions them but they are not the meeting's speaker:
                            // neutral mark.
                            Rectangle {
                                visible: person.modelData.colorIndex < 0
                                implicitWidth: 24; implicitHeight: 24; radius: 12
                                color: Theme.sunken
                                border.width: 1
                                border.color: Theme.borderStrong
                                TIcon { anchors.centerIn: parent; name: "user"; size: 12; color: Theme.textMuted }
                            }
                            TLabel {
                                Layout.fillWidth: true
                                text: person.modelData.name
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
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    TDivider { Layout.fillWidth: true; Layout.bottomMargin: 8 }
                    TLabel {
                        Layout.fillWidth: true
                        text: root.vm.modeLabel
                        muted: true
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.Wrap
                    }
                    TLabel {
                        visible: text !== ""
                        Layout.fillWidth: true
                        text: root.vm.metaLine
                        muted: true
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.Wrap
                    }
                    TLabel {
                        visible: text !== ""
                        Layout.fillWidth: true
                        text: root.vm.modelLine
                        mono: true; muted: true
                        font.pixelSize: Theme.fontMicro
                        elide: Text.ElideMiddle
                    }
                }

                // ---- meeting note (collapsible) ----
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    TDivider { Layout.fillWidth: true; Layout.bottomMargin: 2 }
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
                            TIcon {
                                name: root.vm.noteOpen ? "chevron-up" : "chevron-down"
                                size: 14
                                color: Theme.textMuted
                            }
                        }
                    }
                    // Collapsed: the start (or what it is for), and the detected call.
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
                    RowLayout {
                        visible: !root.vm.noteOpen && root.vm.note.detectedCallApp !== ""
                        Layout.fillWidth: true
                        spacing: 6
                        TIcon { name: "radar"; size: 13; color: Theme.textMuted }
                        TLabel {
                            Layout.fillWidth: true
                            text: qsTr("Észlelt hívás: %1").arg(root.vm.note.detectedCallApp)
                            muted: true
                            font.pixelSize: Theme.fontCaption
                            elide: Text.ElideRight
                        }
                    }
                    MeetingNoteEditor {
                        id: noteEditor
                        visible: root.vm.noteOpen
                        Layout.fillWidth: true
                        compact: true
                        model: root.vm.note
                        fieldHeight: 110
                        helperText: qsTr("Nevek, szakszavak, ismert félrehallások (A „…” helyesen: …). Az újragenerált összefoglaló és egy újra-átírás is ezt kapja.")
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    TButton {
                        Layout.fillWidth: true
                        text: qsTr("Témánkénti elemzés")
                        size: "small"
                        implicitHeight: 32
                        leftPadding: 12; rightPadding: 12
                        iconName: "list-tree"
                        horizontalAlignment: Qt.AlignLeft
                        enabled: root.vm.hasTopics || root.vm.canRun
                        toolTipText: enabled ? "" : (root.vm.blocker.reason || "")
                        onClicked: root.openTopics()
                    }
                    TButton {
                        Layout.fillWidth: true
                        text: qsTr("Újragenerálás")
                        variant: "ghost"; size: "small"
                        implicitHeight: 32
                        leftPadding: 12; rightPadding: 12
                        iconName: "rotate-ccw"
                        horizontalAlignment: Qt.AlignLeft
                        enabled: root.vm.canRun && !root.vm.jobRunning
                        toolTipText: root.vm.canRun ? "" : (root.vm.blocker.reason || "")
                        onClicked: root.regenerate()
                    }
                    // The note changed since the summary: a gentle hint, not a stale mark.
                    RowLayout {
                        visible: root.vm.noteChangedSinceSummary
                        Layout.fillWidth: true
                        Layout.leftMargin: 12
                        Layout.bottomMargin: 2
                        spacing: 6
                        TIcon {
                            Layout.alignment: Qt.AlignTop
                            Layout.topMargin: 2
                            name: "info"; size: 13; color: Theme.textMuted
                        }
                        TLabel {
                            Layout.fillWidth: true
                            text: qsTr("A megjegyzés azóta változott; újrageneráláskor már az új számít.")
                            muted: true
                            font.pixelSize: Theme.fontCaption
                            wrapMode: Text.Wrap
                        }
                    }
                    // For a memo summary the menu picks: executive part, memo, or both.
                    TButton {
                        id: copyButton
                        Layout.fillWidth: true
                        text: qsTr("Másolás")
                        variant: "ghost"; size: "small"
                        implicitHeight: 32
                        leftPadding: 12; rightPadding: 12
                        iconName: "copy"
                        trailingIconName: root.vm.memoState === "ready" ? "chevron-down" : ""
                        iconSize: 14
                        horizontalAlignment: Qt.AlignLeft
                        onClicked: {
                            if (root.vm.memoState === "ready")
                                copyMenu.popup(copyButton, 0, copyButton.height + 4)
                            else
                                root.copy("all")
                        }
                    }
                    TMenu {
                        id: copyMenu
                        TMenuItem {
                            text: qsTr("Vezetői összefoglaló")
                            onTriggered: root.copy("exec")
                        }
                        TMenuItem {
                            text: qsTr("Memó")
                            onTriggered: root.copy("memo")
                        }
                        TMenuItem {
                            text: qsTr("Mindkettő")
                            onTriggered: root.copy("all")
                        }
                    }
                    TButton {
                        Layout.fillWidth: true
                        text: qsTr("Megnyitás mappában")
                        variant: "ghost"; size: "small"
                        implicitHeight: 32
                        leftPadding: 12; rightPadding: 12
                        iconName: "folder-open"
                        horizontalAlignment: Qt.AlignLeft
                        onClicked: if (root.shell) root.shell.revealInFolder(root.meetingId)
                    }
                }
            }
        }
    }
}
