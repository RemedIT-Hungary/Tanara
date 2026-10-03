import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// Könyvtár-oldalsáv (276 px): „Új felvétel”, keresés (Ctrl+F) szűrő-chipekkel és
// találatszámmal, a megbeszélések listája dátum-szekciókkal és állapot-ikonokkal, alul
// „Személyek” és „Beállítások”. Az adat a LibraryListModelből jön (a core MeetingLibrary
// fölött; App.demo módban kitalált mintakönyvtár).
Item {
    id: root

    property var shell: null                 // ShellActions vagy null (önálló képernyőkép)
    property string currentMeetingId: ""
    property alias forceEmpty: libraryModel.forceEmpty
    property alias searchText: libraryModel.searchText
    readonly property alias library: libraryModel
    property var importModel: null            // ShellImportModel: a háttérben futó importálás sávjához
    // A futó importálás sávjára kattintottak (a Main.qml a párbeszédablakot hozza vissza).
    signal importStripClicked()

    function focusSearch() {
        search.forceActiveFocus()
        search.selectAll()
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
            visible: libraryModel.filtered || search.activeFocus || filterMenu.visible
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
            TChip {
                id: addFilter
                text: qsTr("Szűrő")
                dashed: true
                iconName: "list-filter"
                onClicked: filterMenu.popup(addFilter, 0, addFilter.height + 4)

                TMenu {
                    id: filterMenu
                    TMenuItem {
                        text: qsTr("Nincs átirat")
                        iconName: "file-text"
                        checked: libraryModel.noTranscript
                        onTriggered: libraryModel.noTranscript = !libraryModel.noTranscript
                    }
                    TMenuItem {
                        text: qsTr("Nincs összefoglaló")
                        iconName: "sparkles"
                        checked: libraryModel.noSummary
                        onTriggered: libraryModel.noSummary = !libraryModel.noSummary
                    }
                    TMenuSeparator { visible: peopleItems.count > 0 }
                    Instantiator {
                        id: peopleItems
                        // A leggyakoribb résztvevők (a többi névre a keresés is rátalál).
                        model: libraryModel.peopleOptions.slice(0, 8)
                        delegate: TMenuItem {
                            required property var modelData
                            text: modelData.name
                            iconName: "user"
                            checked: libraryModel.people.indexOf(modelData.name) >= 0
                            onTriggered: checked ? libraryModel.removePerson(modelData.name)
                                                 : libraryModel.addPerson(modelData.name)
                        }
                        onObjectAdded: (index, object) => filterMenu.insertItem(index + 3, object)
                        onObjectRemoved: (index, object) => filterMenu.removeItem(object)
                    }
                }
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
                    selected: meetingId === root.currentMeetingId
                    keyboardFocused: list.activeFocus && list.keyNav
                    onActivated: {
                        list.keyNav = false
                        list.tapFocus = !list.activeFocus
                        list.forceActiveFocus()
                        root.activate(meetingId, hasSnippet ? snippetMs : -1)
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
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 8
                Layout.bottomMargin: 10
                spacing: 4
                TButton {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    text: qsTr("Személyek")
                    variant: "ghost"; muted: true
                    iconName: "users"
                    horizontalAlignment: Qt.AlignLeft
                    leftPadding: 8; rightPadding: 8
                    font.weight: Theme.weightRegular
                    onClicked: if (root.shell) root.shell.openPeople()
                }
                TButton {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    text: qsTr("Beállítások")
                    variant: "ghost"; muted: true
                    iconName: "settings"
                    horizontalAlignment: Qt.AlignLeft
                    leftPadding: 8; rightPadding: 8
                    font.weight: Theme.weightRegular
                    onClicked: if (root.shell) root.shell.openSettings("")
                }
            }
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
