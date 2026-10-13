import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// Library sidebar (276 px): "New recording", search (Ctrl+F) with filter chips (status, person,
// tag) and a hit count, the "+ Filter" popover (LibraryFilterPopover), the meeting list with
// date sections, status icons and a tag row, multi-select (Ctrl / Shift+click, Esc; Main.qml shows
// the right-hand panel based on `selection`), and "People - Tags - gear" at the bottom. Data comes
// from LibraryListModel (over the core MeetingLibrary; a fictional sample library in App.demo
// mode).
Item {
    id: root

    property var shell: null                 // ShellActions or null (standalone screenshot)
    property string currentMeetingId: ""
    property alias forceEmpty: libraryModel.forceEmpty
    property alias searchText: libraryModel.searchText
    readonly property alias library: libraryModel
    // Multi-select (LibrarySelectionModel); count > 1 -> Main.qml shows the selection panel.
    readonly property alias selection: selectionModel
    property var importModel: null            // ShellImportModel: for the background import strip
    // The running-import strip was clicked (Main.qml brings the dialog back).
    signal importStripClicked()

    function focusSearch() {
        search.forceActiveFocus()
        search.selectAll()
    }

    // Filter to a single tag (the tag chip / the manager's "Open in library as filter"
    // path): search and all other filters are cleared.
    function filterByTag(tagId) {
        selectionModel.clear()
        libraryModel.clearFilters()
        if (tagId !== "")
            libraryModel.tags = [tagId]
    }

    // The tag manager (provided by the shell; absent in older shells).
    function openTags() {
        if (root.shell && root.shell.openTags)
            root.shell.openTags()
    }

    // Demo / screenshot: "filters" (tag filters + open popover) | "selection" (multi-select).
    function applyDemo(state) {
        if (state === "filters") {
            libraryModel.tags = ["t-nordvik", "t-partnerek"]
            Qt.callLater(() => root.openFilterPopover())
        } else if (state === "selection") {
            selectionModel.selectIds(["demo-tamogatas", "demo-partner", "demo-belepo"])
        }
    }

    function openFilterPopover() {
        filterPopover.open()
    }

    // Click on a row: Ctrl toggles selection, Shift selects a range, otherwise open.
    function rowClicked(meetingId, snippetMs, modifiers) {
        if (modifiers & Qt.ControlModifier) {
            selectionModel.toggle(meetingId)
            settleSelection()
        } else if (modifiers & Qt.ShiftModifier) {
            selectionModel.rangeTo(meetingId)
            settleSelection()
        } else {
            selectionModel.clear()
            activate(meetingId, snippetMs)
        }
    }

    // A single-item selection means that one meeting is open.
    function settleSelection() {
        if (selectionModel.count !== 1)
            return
        const only = selectionModel.ids[0]
        selectionModel.clear()
        activate(only, -1)
    }

    function activate(meetingId, snippetMs) {
        if (!root.shell) {
            root.currentMeetingId = meetingId
            return
        }
        // Clicking a search hit jumps to the matching utterance in the transcript.
        if (snippetMs >= 0)
            root.shell.seekTo(meetingId, snippetMs)
        else
            root.shell.showMeeting(meetingId)
    }

    function moveSelection(delta) {
        if (libraryModel.count === 0)
            return
        selectionModel.clear()
        let row = libraryModel.indexOfMeeting(root.currentMeetingId)
        row = row < 0 ? (delta > 0 ? 0 : libraryModel.count - 1)
                      : Math.max(0, Math.min(libraryModel.count - 1, row + delta))
        activate(libraryModel.meetingIdAt(row), -1)
        list.positionViewAtIndex(row, ListView.Contain)
    }

    // Keep the selected item visible when it was selected from outside (new recording, "Waiting
    // for you", startup restore). Scroll only if it is actually clipped, and only after the
    // list has laid out (the section headers do not exist yet at startup).
    function revealCurrent() {
        const row = libraryModel.indexOfMeeting(currentMeetingId)
        if (row < 0 || list.height <= 0)
            return
        const item = list.itemAtIndex(row)
        if (item) {
            const top = item.y - list.contentY
            if (top >= 0 && top + item.height <= list.height)
                return
        }
        list.positionViewAtIndex(row, ListView.Contain)
    }
    onCurrentMeetingIdChanged: revealTimer.restart()
    Timer { id: revealTimer; interval: 30; onTriggered: root.revealCurrent() }

    LibraryListModel { id: libraryModel }
    LibrarySelectionModel {
        id: selectionModel
        library: libraryModel
        currentId: root.currentMeetingId
    }

    ColumnLayout {
        anchors { fill: parent; leftMargin: 12; rightMargin: 12; topMargin: 14 }
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            TButton {
                Layout.fillWidth: true
                text: qsTr("Új felvétel")
                variant: "record"
                toolTipText: qsTr("A felvevő megnyitása külön ablakban (Ctrl+N)")
                onClicked: if (root.shell) root.shell.openRecorder()
            }
            // Secondary way to create a meeting: from an existing audio file.
            TIconButton {
                objectName: "sidebarImport"
                iconName: "import"
                toolTipText: qsTr("Hangfájl importálása… (Ctrl+I)")
                onClicked: if (root.shell) root.shell.openImport()
            }
        }

        ShellImportStrip {
            objectName: "importStrip"
            visible: root.importModel ? root.importModel.running : false
            Layout.fillWidth: true
            model: root.importModel
            onClicked: root.importStripClicked()
        }

        TSearchField {
            id: search
            objectName: "librarySearch"
            Layout.fillWidth: true
            placeholderText: qsTr("Keresés")
            hint: "Ctrl+F"
            text: libraryModel.searchText
            onTextChanged: libraryModel.searchText = text
            // On first focus, preload the transcript text so search is instant.
            onActiveFocusChanged: if (activeFocus) libraryModel.warmUp()
            Keys.onDownPressed: { list.forceActiveFocus(); root.moveSelection(1) }
            Keys.onReturnPressed: { libraryModel.refreshNow(); if (libraryModel.count > 0) root.moveSelection(1) }
        }

        // Filter chips (visible only while searching / filtering, so the idle list stays clean).
        Flow {
            id: chips
            visible: libraryModel.filtered || search.activeFocus || filterPopover.visible
            Layout.fillWidth: true
            spacing: 6

            TChip {
                visible: libraryModel.noTranscript
                text: qsTr("Nincs átirat")
                removable: true
                onRemoved: libraryModel.noTranscript = false
                onClicked: libraryModel.noTranscript = false
            }
            TChip {
                visible: libraryModel.noSummary
                text: qsTr("Nincs összefoglaló")
                removable: true
                onRemoved: libraryModel.noSummary = false
                onClicked: libraryModel.noSummary = false
            }
            Repeater {
                model: libraryModel.people
                TChip {
                    required property string modelData
                    text: modelData
                    speakerIndex: libraryModel.personColorIndex(modelData)
                    removable: true
                    onRemoved: libraryModel.removePerson(modelData)
                    onClicked: libraryModel.removePerson(modelData)
                }
            }
            // Tag filters: the tag chip (with x), so it is not confused with a person chip.
            Repeater {
                model: libraryModel.tagFilterItems
                TagChip {
                    required property var modelData
                    text: modelData.name
                    removable: true
                    removeAlwaysVisible: true
                    onRemoveRequested: libraryModel.removeTag(modelData.id)
                    onClicked: libraryModel.removeTag(modelData.id)
                }
            }
            TChip {
                visible: libraryModel.untagged
                text: qsTr("Címke nélkül")
                tone: "neutral"
                removable: true
                onRemoved: libraryModel.untagged = false
                onClicked: libraryModel.untagged = false
            }
            TChip {
                id: addFilter
                objectName: "addFilter"
                text: qsTr("Szűrő")
                dashed: true
                down: pressed || filterPopover.visible
                onClicked: filterPopover.visible ? filterPopover.close() : root.openFilterPopover()
            }
        }

        TLabel {
            visible: libraryModel.filtered
            Layout.fillWidth: true
            Layout.leftMargin: 2
            Layout.topMargin: -2
            text: libraryModel.resultText
            muted: true
            font.pixelSize: Theme.fontCaption
            elide: Text.ElideRight
        }

        // ---- the list (or its empty states) ----
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: list
                objectName: "libraryList"
                anchors.fill: parent
                visible: libraryModel.count > 0
                clip: true
                model: libraryModel
                spacing: 1
                boundsBehavior: Flickable.StopAtBounds
                reuseItems: true
                cacheBuffer: 400
                activeFocusOnTab: true
                keyNavigationEnabled: false
                // The scrollbar shows only while scrolling / hovering (a clean list at rest).
                T.ScrollBar.vertical: TScrollBar {
                    opacity: active ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: Theme.durationNormal } }
                }

                section.property: "section"
                section.delegate: Item {
                    required property string section
                    width: ListView.view ? ListView.view.width : 0
                    height: 30
                    TSectionLabel {
                        x: 10
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.verticalCenterOffset: 2
                        text: parent.section
                        font.pixelSize: Theme.fontMicro
                    }
                }

                delegate: LibraryItem {
                    width: ListView.view.width
                    selectionMode: selectionModel.count > 0
                    checked: selectionModel.count > 0 && selectionModel.ids.indexOf(meetingId) >= 0
                    selected: selectionModel.count > 1 ? checked : meetingId === root.currentMeetingId
                    keyboardFocused: list.activeFocus && list.keyNav
                    onActivated: (modifiers) => {
                        list.keyNav = false
                        list.tapFocus = !list.activeFocus
                        list.forceActiveFocus()
                        root.rowClicked(meetingId, hasSnippet ? snippetMs : -1, modifiers)
                    }
                    onCheckToggled: {
                        list.forceActiveFocus()
                        selectionModel.toggle(meetingId)
                        root.settleSelection()
                    }
                    onContextMenuRequested: (x, y) => {
                        itemMenu.meetingId = meetingId
                        itemMenu.popup(this, x, y)
                    }
                }

                // Whether focus arrived via keyboard (then the selection shows its focus ring).
                property bool keyNav: false
                property bool tapFocus: false
                onActiveFocusChanged: {
                    if (activeFocus) keyNav = !tapFocus
                    tapFocus = false
                }

                Keys.onDownPressed: { keyNav = true; root.moveSelection(1) }
                Keys.onUpPressed: { keyNav = true; root.moveSelection(-1) }
                Keys.onEscapePressed: (event) => {
                    if (selectionModel.count > 0) selectionModel.clear()
                    else event.accepted = false
                }
                Keys.onPressed: (event) => {
                    if (!root.shell || root.currentMeetingId === "")
                        return
                    if (event.key === Qt.Key_F2) {
                        root.shell.requestRename(root.currentMeetingId)
                        event.accepted = true
                    } else if (event.key === Qt.Key_Delete) {
                        root.shell.requestDelete(root.currentMeetingId)
                        event.accepted = true
                    }
                }
            }

            // Empty library.
            ColumnLayout {
                visible: libraryModel.totalCount === 0 && !libraryModel.filtered
                anchors.centerIn: parent
                anchors.verticalCenterOffset: -8
                width: parent.width - 24
                spacing: 8
                TIcon { Layout.alignment: Qt.AlignHCenter; name: "inbox"; size: 22; color: Theme.textMuted }
                TLabel {
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("Még nincs megbeszélés")
                    font.weight: Theme.weightSemiBold
                    wrapMode: Text.Wrap
                }
                TLabel {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("A felvételek itt jelennek meg, legújabb felül.")
                    muted: true
                    font.pixelSize: Theme.fontSmall
                    cssLineHeight: 1.5
                    wrapMode: Text.Wrap
                }
            }

            // No results for the filter / search.
            ColumnLayout {
                visible: libraryModel.count === 0 && libraryModel.filtered
                anchors.horizontalCenter: parent.horizontalCenter
                y: 24
                width: parent.width - 24
                spacing: 8
                TLabel {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("Nincs találat")
                    font.weight: Theme.weightSemiBold
                }
                TLabel {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("Próbálj más kifejezést, vagy vedd le a szűrőket.")
                    muted: true
                    font.pixelSize: Theme.fontSmall
                    cssLineHeight: 1.5
                    wrapMode: Text.Wrap
                }
                TButton {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Szűrők törlése")
                    size: "small"
                    onClicked: libraryModel.clearFilters()
                }
            }
        }

        // ---- footer ----
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0
            TDivider { Layout.fillWidth: true }
            // "People - Tags - gear": the Settings label shrinks to an icon so it fits.
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 8
                Layout.bottomMargin: 10
                spacing: 2
                TButton {
                    text: qsTr("Személyek")
                    variant: "ghost"; muted: true
                    iconName: "users"
                    horizontalAlignment: Qt.AlignLeft
                    leftPadding: 8; rightPadding: 8
                    font.weight: Theme.weightRegular
                    onClicked: if (root.shell) root.shell.openPeople()
                }
                TButton {
                    objectName: "footerTags"
                    text: qsTr("Címkék")
                    variant: "ghost"; muted: true
                    iconName: "tag"
                    horizontalAlignment: Qt.AlignLeft
                    leftPadding: 8; rightPadding: 8
                    font.weight: Theme.weightRegular
                    onClicked: root.openTags()
                }
                Item { Layout.fillWidth: true }
                TIconButton {
                    objectName: "footerSettings"
                    implicitWidth: 32; implicitHeight: 32
                    variant: "flat"
                    iconName: "settings"
                    toolTipText: qsTr("Beállítások · Ctrl+,")
                    onClicked: if (root.shell) root.shell.openSettings("")
                }
            }
        }
    }

    LibraryFilterPopover {
        id: filterPopover
        parent: addFilter
        // Per the design it hangs slightly to the left (at the window edge the margin catches it).
        x: -40
        y: addFilter.height + 6
        library: libraryModel
        onManageTagsRequested: root.openTags()
    }

    // Meeting selected from outside (shell, "Waiting for you", manager): the selection is cleared.
    Connections {
        target: root
        function onCurrentMeetingIdChanged() {
            if (selectionModel.count > 0 && selectionModel.ids.indexOf(root.currentMeetingId) < 0)
                selectionModel.clear()
        }
    }

    // Context menu on list items.
    TMenu {
        id: itemMenu
        property string meetingId: ""
        TMenuItem {
            text: qsTr("Átnevezés")
            iconName: "pencil"
            shortcutText: "F2"
            onTriggered: if (root.shell) root.shell.requestRename(itemMenu.meetingId)
        }
        TMenuItem {
            text: qsTr("Megnyitás mappában")
            iconName: "folder-open"
            onTriggered: if (root.shell) root.shell.revealInFolder(itemMenu.meetingId)
        }
        TMenuSeparator {}
        TMenuItem {
            text: qsTr("Törlés…")
            iconName: "trash-2"
            danger: true
            onTriggered: if (root.shell) root.shell.requestDelete(itemMenu.meetingId)
        }
    }
}
