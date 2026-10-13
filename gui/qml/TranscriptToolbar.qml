import QtQuick
import QtQuick.Layouts

// The Transcript tab's toolbar: 46 px, 1 px line at the bottom. Read · Fix · To review (N)
// segment (Ctrl+E: Read <-> Fix; B: To review) · status ("1298 utterances · 5 speakers", voice
// analysis) · ... · in Read the quiet "N lines to review" (at 0 the recheck) · "Who was there?" ·
// search. No permanent undo button (Ctrl+Z, notice bar) and no rail switch (View > Speaker
// columns, Ctrl+L); the per-speaker timeline moved into the map dock above the player (MapDock).
// The Fix mode content (rail, markers, To review groups) belongs to the editor. The To review
// segment always shows the count (0 too: then the empty state shows, not an empty list); in the
// To review view the status reads "84 / 1298 lines · 3 groups".
Item {
    id: root

    required property var vm                // TranscriptEditorViewModel
    property bool searchOpen: false
    property bool fixMode: false
    property string viewMode: fixMode ? "fix" : "read"     // "read" | "fix" | "review"
    // The old UI is kept (the map dock gets it in Main.qml).
    property real viewportStart: 0
    property real viewportSize: 0

    signal modeRequested(string mode)       // Read / Fix / To review
    signal participantsRequested()          // "Who was there?"
    signal searchStepRequested(int direction)
    signal nextUncertainRequested()         // "Next to review" (B)
    signal recheckRequested()               // 0 to review: recheck based on the confirmed lines
    signal seekRequested(real fraction)
    signal speakerClicked(string speakerKey, Item anchor)
    signal voiceprintClicked(string speakerKey, Item anchor)

    implicitHeight: 46
    height: implicitHeight

    function openSearch() {
        searchOpen = true
        searchField.forceActiveFocus()
        searchField.selectAll()
    }
    function closeSearch() {
        searchOpen = false
        vm.searchQuery = ""
    }
    // The voiceprint mark lives in the map dock; in a standalone screenshot the status label is the anchor.
    function voiceprintMark(speakerKey) { return statusLabel }

    // Quiet tool button (28 px): icon + label; `checked`: accentSoft / accent.
    component QuietButton: Item {
        id: qb
        property string text: ""
        property string iconName: ""
        property bool checked: false
        property string toolTipText: ""
        signal clicked()
        implicitHeight: 28
        implicitWidth: qbRow.implicitWidth + (text === "" ? 0 : 20)
        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: text !== "" ? text : toolTipText
        Keys.onSpacePressed: clicked()
        Keys.onReturnPressed: clicked()
        readonly property color ink: !enabled ? Theme.borderStrong : checked ? Theme.accent : Theme.textMuted
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusControl
            color: qb.checked ? Theme.accentSoft : "transparent"
            border.width: qb.checked ? 1 : 0
            border.color: Theme.accentLine
            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                color: Theme.stateLayer
                opacity: !qb.enabled ? 0 : qbTap.pressed ? Theme.pressedOpacity : qbHover.hovered ? Theme.hoverOpacity : 0
            }
            TFocusRing { visible: qb.activeFocus; targetRadius: parent.radius }
        }
        Row {
            id: qbRow
            anchors.centerIn: parent
            spacing: 6
            TIcon {
                visible: qb.iconName !== ""
                anchors.verticalCenter: parent.verticalCenter
                name: qb.iconName
                size: 14
                color: qb.ink
                width: qb.text === "" ? 30 : 14
            }
            TLabel {
                visible: qb.text !== ""
                anchors.verticalCenter: parent.verticalCenter
                text: qb.text
                color: qb.checked ? Theme.accent : !qb.enabled ? Theme.borderStrong : Theme.textMuted
                font.pixelSize: Theme.fontSmall
                font.weight: qb.checked ? Theme.weightSemiBold : Theme.weightRegular
            }
        }
        HoverHandler { id: qbHover }
        TapHandler { id: qbTap; enabled: qb.enabled; onTapped: qb.clicked() }
        TToolTip { visible: qb.toolTipText !== "" && qbHover.hovered; text: qb.toolTipText }
    }

    RowLayout {
        anchors { fill: parent; leftMargin: Theme.space5; rightMargin: 20 }
        spacing: 10

        // ---- Read · Fix · To review (N) ----
        Rectangle {
            id: segment
            objectName: "modeSegment"
            implicitWidth: segRow.implicitWidth + 6
            implicitHeight: 32
            radius: 7
            color: Theme.sunken
            Row {
                id: segRow
                anchors.centerIn: parent
                spacing: 2
                Repeater {
                    model: [
                        { label: qsTr("Olvasás"), icon: "book-open", mode: "read", name: "modeRead" },
                        { label: qsTr("Javítás"), icon: "pencil", mode: "fix", name: "modeFix" },
                        { label: qsTr("Átnézendő"), icon: "list-checks", mode: "review", name: "modeReview" }
                    ]
                    Item {
                        id: seg
                        required property var modelData
                        objectName: modelData.name
                        readonly property bool active: root.viewMode === modelData.mode
                        readonly property bool review: modelData.mode === "review"
                        implicitWidth: segContent.implicitWidth + 22
                        implicitHeight: 26
                        width: implicitWidth
                        height: implicitHeight
                        activeFocusOnTab: true
                        Accessible.role: Accessible.RadioButton
                        Accessible.name: review ? qsTr("Átnézendő (%1)").arg(root.vm.reviewCount) : modelData.label
                        Accessible.checked: active
                        Keys.onSpacePressed: root.modeRequested(modelData.mode)
                        Rectangle {
                            anchors.fill: parent
                            radius: 5
                            color: seg.active ? Theme.raised : segHover.hovered ? Theme.alpha(Theme.stateLayer, Theme.hoverOpacity) : "transparent"
                            border.width: seg.active ? 1 : 0
                            border.color: Theme.border
                            TFocusRing { visible: seg.activeFocus; targetRadius: parent.radius }
                        }
                        Row {
                            id: segContent
                            anchors.centerIn: parent
                            spacing: 6
                            TIcon {
                                anchors.verticalCenter: parent.verticalCenter
                                name: seg.modelData.icon
                                size: 14
                                color: seg.active ? Theme.text : Theme.textMuted
                            }
                            TLabel {
                                anchors.verticalCenter: parent.verticalCenter
                                text: seg.modelData.label
                                color: seg.active ? Theme.text : Theme.textMuted
                                font.pixelSize: Theme.fontSmall
                                font.weight: seg.active ? Theme.weightSemiBold : Theme.weightRegular
                            }
                            // The To review count: always visible, also at 0.
                            Rectangle {
                                visible: seg.review
                                anchors.verticalCenter: parent.verticalCenter
                                width: countText.implicitWidth + 10
                                height: 18
                                radius: Theme.radiusTag
                                color: seg.active ? Theme.accentSoft : Theme.raised
                                TLabel {
                                    id: countText
                                    objectName: "reviewCount"
                                    anchors.centerIn: parent
                                    text: String(root.vm.reviewCount)
                                    mono: true
                                    color: seg.active ? Theme.accent : root.vm.reviewCount > 0 ? Theme.text : Theme.textMuted
                                    font.pixelSize: Theme.fontCaption
                                    font.weight: Theme.weightMedium
                                }
                            }
                            // A subtle indicator during background analysis.
                            TSpinner {
                                visible: seg.review && root.vm.reviewRunning
                                anchors.verticalCenter: parent.verticalCenter
                                size: 11
                                color: Theme.textMuted
                            }
                        }
                        HoverHandler { id: segHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: root.modeRequested(seg.modelData.mode) }
                        TToolTip {
                            visible: segHover.hovered
                            text: seg.modelData.mode === "fix" ? qsTr("Javítás: beszélő-oszlopok, jelölők, soronkénti javítás (Ctrl+E)")
                                : seg.review ? qsTr("Átnézendő: a kétes sorok okok szerint csoportosítva, csoportonként egy döntéssel (B: a következő)")
                                : qsTr("Olvasás: tiszta lista (Ctrl+E)")
                        }
                    }
                }
            }
        }
        TLabel {
            visible: !root.searchOpen
            text: "Ctrl+E"
            mono: true
            muted: true
            font.pixelSize: Theme.fontMicro
        }

        // ---- status ----
        TLabel {
            id: statusLabel
            objectName: "transcriptStatus"
            visible: !root.searchOpen
            Layout.leftMargin: 6
            Layout.fillWidth: true
            Layout.minimumWidth: 60
            text: root.viewMode === "review"
                  ? qsTr("%1 / %2 sor · %n csoport", "", root.vm.reviewGroupCount).arg(root.vm.reviewCount).arg(root.vm.utteranceCount)
                  : qsTr("%1 megszólalás · %2 beszélő").arg(root.vm.utteranceCount).arg(root.vm.speakerCount)
            muted: true
            font.pixelSize: 12
            elide: Text.ElideRight
        }
        // Voice analysis: a quiet progress indicator while running; a short notice if unavailable.
        Row {
            visible: !root.searchOpen && root.vm.embeddingRunning
            spacing: 8
            TLabel {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Hangelemzés… %1%").arg(Math.round(root.vm.embeddingProgress * 100))
                muted: true
                font.pixelSize: Theme.fontCaption
            }
            TProgressBar {
                anchors.verticalCenter: parent.verticalCenter
                width: 64
                value: root.vm.embeddingProgress
            }
        }
        Row {
            objectName: "voiceNote"
            visible: !root.searchOpen && !root.vm.embeddingRunning && root.vm.voiceNote !== ""
            spacing: 6
            TIcon { anchors.verticalCenter: parent.verticalCenter; name: "info"; size: 13; color: Theme.textMuted }
            TLabel {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Hangelemzés nélkül")
                muted: true
                font.pixelSize: Theme.fontCaption
            }
            HoverHandler { id: voiceNoteHover }
            TToolTip { visible: voiceNoteHover.hovered; text: root.vm.voiceNote; delay: 150 }
        }

        // ---- search (in place of the status) ----
        RowLayout {
            visible: root.searchOpen
            Layout.fillWidth: true
            spacing: 6
            TSearchField {
                id: searchField
                objectName: "transcriptSearch"
                Layout.fillWidth: true
                Layout.maximumWidth: 300
                Layout.preferredHeight: 28
                placeholderText: qsTr("Keresés az átiratban")
                onTextChanged: root.vm.searchQuery = text
                Keys.onReturnPressed: event => root.searchStepRequested(event.modifiers & Qt.ShiftModifier ? -1 : 1)
                Keys.onEnterPressed: event => root.searchStepRequested(event.modifiers & Qt.ShiftModifier ? -1 : 1)
                Keys.onEscapePressed: root.closeSearch()
                Connections {
                    target: root.vm
                    function onSearchChanged() {
                        if (root.vm.searchQuery !== "" && !root.searchOpen) root.searchOpen = true
                        if (searchField.text !== root.vm.searchQuery) searchField.text = root.vm.searchQuery
                    }
                }
            }
            TLabel {
                visible: root.vm.searchQuery.trim() !== ""
                text: root.vm.searchMatchCount === 0 ? qsTr("Nincs találat")
                    : qsTr("%1 / %2").arg(root.vm.searchCurrent + 1).arg(root.vm.searchMatchCount)
                mono: root.vm.searchMatchCount > 0
                color: root.vm.searchMatchCount === 0 ? Theme.dangerInk : Theme.textMuted
                font.pixelSize: Theme.fontCaption
            }
            QuietButton {
                iconName: "chevron-up"
                enabled: root.vm.searchMatchCount > 0
                toolTipText: qsTr("Előző találat (Shift+Enter)")
                onClicked: root.searchStepRequested(-1)
            }
            QuietButton {
                iconName: "chevron-down"
                enabled: root.vm.searchMatchCount > 0
                toolTipText: qsTr("Következő találat (Enter)")
                onClicked: root.searchStepRequested(1)
            }
            Item { Layout.fillWidth: true }
        }

        // In Read, the quiet "N lines to review" (a click opens the To review view). When no (more)
        // lines need review it offers the recheck -- measures the rest against the sound of the
        // confirmed and fixed lines. In Fix / To review the segment's count is enough.
        QuietButton {
            id: reviewButton
            objectName: "uncertainChip"
            readonly property int count: root.vm.reviewCount
            readonly property bool offersRecheck: count === 0 && root.vm.uncertainCount === 0
                                                  && root.vm.voiceAvailable && !root.vm.embeddingRunning
            visible: root.viewMode === "read"
            enabled: root.vm.voiceAvailable || count > 0
            iconName: offersRecheck && root.vm.canRecheck ? "refresh-cw" : "list-checks"
            text: offersRecheck ? qsTr("Nincs átnézendő sor") : qsTr("%n átnézendő sor", "", count)
            toolTipText: !root.vm.voiceAvailable ? root.vm.voiceNote
                       : offersRecheck && root.vm.canRecheck
                         ? qsTr("Nincs átnézendő sor. Kattints, és a megerősített és javított sorok hangja alapján újraellenőrzöm a többit.")
                       : offersRecheck ? qsTr("Nincs átnézendő sor. Újraellenőrzéshez: %1").arg(root.vm.recheckBlocker)
                       : qsTr("A kétes sorok okok szerint csoportosítva, csoportonként egy döntéssel (B: a következő)")
            onClicked: offersRecheck ? root.recheckRequested() : root.modeRequested("review")
        }
        QuietButton {
            visible: root.viewMode === "review" && root.vm.reviewCount > 0
            iconName: "chevrons-down"
            toolTipText: qsTr("Következő átnézendő sor (B)")
            onClicked: root.nextUncertainRequested()
        }
        QuietButton {
            objectName: "whoWasThereButton"
            visible: !root.searchOpen
            text: qsTr("Ki volt ott?")
            iconName: "users"
            toolTipText: qsTr("A résztvevők átnézése: kik voltak ott, és melyik beszélő kicsoda")
            onClicked: root.participantsRequested()
        }
        QuietButton {
            objectName: "searchButton"
            iconName: root.searchOpen ? "x" : "search"
            toolTipText: root.searchOpen ? qsTr("Keresés bezárása") : qsTr("Keresés az átiratban (Ctrl+Shift+F)")
            onClicked: root.searchOpen ? root.closeSearch() : root.openSearch()
        }
    }

    TDivider { anchors.bottom: parent.bottom; width: parent.width }
}
