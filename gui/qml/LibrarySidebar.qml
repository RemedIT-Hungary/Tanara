import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// Könyvtár-oldalsáv (276 px): „Új felvétel”, keresés (Ctrl+F) szűrő-chipekkel (állapot,
// személy, címke) és találatszámmal, a „+ Szűrő” popover (LibraryFilterPopover), a
// megbeszélések listája dátum-szekciókkal, állapot-ikonokkal és címke-sorral, többes kijelölés
// (Ctrl / Shift+kattintás, Esc; a jobb oldali panelt a Main.qml mutatja a `selection` alapján),
// alul „Személyek · Címkék · ⚙”. Az adat a LibraryListModelből jön (a core MeetingLibrary
// fölött; App.demo módban kitalált mintakönyvtár).
Item {
    id: root

    property var shell: null                 // ShellActions vagy null (önálló képernyőkép)
    property string currentMeetingId: ""
    property alias forceEmpty: libraryModel.forceEmpty
    property alias searchText: libraryModel.searchText
    readonly property alias library: libraryModel
    // Többes kijelölés (LibrarySelectionModel); count > 1 → a Main.qml a kijelölés-panelt mutatja.
    readonly property alias selection: selectionModel
    property var importModel: null            // ShellImportModel: a háttérben futó importálás sávjához
    // A futó importálás sávjára kattintottak (a Main.qml a párbeszédablakot hozza vissza).
    signal importStripClicked()

    function focusSearch() {
        search.forceActiveFocus()
        search.selectAll()
    }

    // Szűrés egyetlen címkére (a címke-chip / a kezelő „Megnyitás a könyvtárban szűrőként”
    // útja): a keresés és a többi szűrő törlődik.
    function filterByTag(tagId) {
        selectionModel.clear()
        libraryModel.clearFilters()
        if (tagId !== "")
            libraryModel.tags = [tagId]
    }

    // A címkék kezelője (a héj adja; régebbi héjban még nincs).
    function openTags() {
        if (root.shell && root.shell.openTags)
            root.shell.openTags()
    }

    // Demó / képernyőkép: "filters" (T04: címke-szűrők + nyitott popover) | "selection" (T05).
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

    // Kattintás egy soron: Ctrl → kijelölés váltása, Shift → tartomány, különben megnyitás.
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

    // Egyelemű kijelölés = az az egy megbeszélés megnyitva.
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
        // Keresési találatra kattintva az átiratban a találat megszólalásához ugrunk.
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

    // A kijelölt elem legyen látható, ha kívülről választották ki (új felvétel, „Ezek várnak
    // rád”, indításkori visszaállítás). Csak akkor görgetünk, ha tényleg kilóg — és csak
    // miután a lista elrendeződött (induláskor a szekció-fejlécek még nincsenek meg).
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
            // Másodlagos út egy megbeszéléshez: meglévő hangfájlból.
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
            // Az első fókuszkor előtöltjük az átiratok szövegét, hogy a keresés azonnali legyen.
            onActiveFocusChanged: if (activeFocus) libraryModel.warmUp()
            Keys.onDownPressed: { list.forceActiveFocus(); root.moveSelection(1) }
            Keys.onReturnPressed: { libraryModel.refreshNow(); if (libraryModel.count > 0) root.moveSelection(1) }
        }

        // Szűrő-chipek (csak keresés / szűrés közben látszanak, hogy a nyugalmi lista tiszta maradjon).
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
            // Címke-szűrők: a címke-chip (×-szel), hogy ne tévesszük össze a személy-chippel.
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

        // ---- a lista (vagy az üres állapotai) ----
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
                // A görgetősáv csak görgetés / rámutatás közben látszik (nyugalomban tiszta lista).
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

                // Billentyűzettel érkezett-e a fókusz (ekkor látszik a kijelölés fókusz-kerete).
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

            // M01 — üres könyvtár.
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

            // Szűrés / keresés találat nélkül.
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

        // ---- lábléc ----
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0
            TDivider { Layout.fillWidth: true }
            // „Személyek · Címkék · ⚙” — hogy kiférjen, a Beállítások ikonra rövidül.
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
        // A design szerint kicsit balra lóg (az ablak szélénél a margó megfogja).
        x: -40
        y: addFilter.height + 6
        library: libraryModel
        onManageTagsRequested: root.openTags()
    }

    // Kívülről választott megbeszélés (héj, „Ezek várnak rád”, kezelő): a kijelölés megszűnik.
    Connections {
        target: root
        function onCurrentMeetingIdChanged() {
            if (selectionModel.count > 0 && selectionModel.ids.indexOf(root.currentMeetingId) < 0)
                selectionModel.clear()
        }
    }

    // Helyi menü a lista elemein.
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
