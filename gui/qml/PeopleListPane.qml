import QtQuick
import QtQuick.Templates as T

// A Személyek ablak bal oldali sávja: kereső (név és becenév), „Új személy”, darabszám +
// rendezés, és a névsor szakaszokkal. Üres állapot (P06) és eredménytelen keresés a lista alatt.
Rectangle {
    id: root

    property var vm: null
    // Képernyőképhez: a rendezés menüje nyitva.
    property bool demoSortOpen: false
    // Hamis: a kijelölt sor nincs kiemelve (P06: a részletező az üres állapotot mutatja).
    property bool showSelection: true
    signal newPersonRequested(string name)
    // A sor kiválasztása előtt a részletező elmenti a félbehagyott megjegyzést.
    signal aboutToSelect()
    signal personClicked(string name)

    function focusSearch() { search.forceActiveFocus(); search.selectAll() }

    color: Theme.surface

    Item {
        id: top
        x: 12; y: 12
        width: parent.width - 24
        height: 32 + 8 + countRow.height

        TSearchField {
            id: search
            objectName: "peopleSearch"
            width: parent.width - 38
            placeholderText: qsTr("Név vagy becenév")
            font.pixelSize: Theme.fontBody
            // Szöveggel fókusz nélkül is kiemelt marad (P02).
            stateFocused: activeFocus || length > 0
            onTextChanged: if (root.vm && root.vm.query !== text) root.vm.query = text
            Component.onCompleted: if (root.vm) text = root.vm.query
            Connections {
                target: root.vm
                function onQueryChanged() { if (search.text !== root.vm.query) search.text = root.vm.query }
            }
            Keys.onDownPressed: list.forceActiveFocus()
            Keys.onReturnPressed: if (list.count > 0) { root.aboutToSelect(); root.personClicked(root.vm.people.nameAt(0)) }
            Accessible.name: qsTr("Keresés a személyek között")
        }
        TIconButton {
            objectName: "newPersonButton"
            anchors.right: parent.right
            width: 32; height: 32
            iconName: "user-plus"
            iconSize: 15
            toolTipText: qsTr("Új személy")
            onClicked: root.newPersonRequested("")
        }

        Item {
            id: countRow
            y: 40
            width: parent.width
            height: 20
            TLabel {
                objectName: "peopleCount"
                anchors.verticalCenter: parent.verticalCenter
                x: 1
                text: root.vm ? root.vm.countText : ""
                muted: true
                font.pixelSize: Theme.fontCaption
            }
            T.AbstractButton {
                id: sortButton
                objectName: "sortButton"
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: sortRow.implicitWidth + 8
                height: 22
                hoverEnabled: true
                activeFocusOnTab: true
                Accessible.name: qsTr("Rendezés: %1").arg(root.vm ? root.vm.sortLabel : "")
                onClicked: sortMenu.open()
                Keys.onReturnPressed: click()
                background: Rectangle {
                    radius: 4
                    color: Theme.stateLayer
                    opacity: sortButton.down ? Theme.pressedOpacity : sortButton.hovered ? Theme.hoverOpacity : 0
                    TFocusRing { visible: sortButton.visualFocus; targetRadius: 4 }
                }
                contentItem: Item {
                    Row {
                        id: sortRow
                        anchors.centerIn: parent
                        spacing: 4
                        TLabel {
                            text: root.vm ? root.vm.sortLabel : ""
                            muted: true
                            font.pixelSize: Theme.fontCaption
                        }
                        TIcon { anchors.verticalCenter: parent.verticalCenter; name: "chevron-down"; size: 12; color: Theme.textMuted }
                    }
                }
                TMenu {
                    id: sortMenu
                    y: sortButton.height + 2
                    x: sortButton.width - width
                    TMenuItem { text: qsTr("ABC"); checked: root.vm && root.vm.sort === "abc"; onTriggered: root.vm.sort = "abc" }
                    TMenuItem { text: qsTr("Legutóbb"); checked: root.vm && root.vm.sort === "recent"; onTriggered: root.vm.sort = "recent" }
                    TMenuItem { text: qsTr("Legtöbb megbeszélés"); checked: root.vm && root.vm.sort === "meetings"; onTriggered: root.vm.sort = "meetings" }
                }
            }
        }
    }
    onDemoSortOpenChanged: if (demoSortOpen) Qt.callLater(sortMenu.open)

    ListView {
        id: list
        objectName: "peopleList"
        anchors.top: top.bottom
        anchors.topMargin: 4
        anchors.bottom: parent.bottom
        x: 8
        width: parent.width - 16
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: root.vm ? root.vm.people : null
        currentIndex: root.vm ? root.vm.selectedRow : -1
        highlightFollowsCurrentItem: false
        activeFocusOnTab: true
        cacheBuffer: 480
        T.ScrollBar.vertical: TScrollBar {}

        delegate: PeopleListRow {
            required property int index
            required property var model
            width: list.width
            name: model.name
            monogram: model.monogram
            isSelf: model.isSelf
            hasVoiceprint: model.hasVoiceprint
            meta: model.meta
            header: model.header
            before: model.before
            match: model.match
            after: model.after
            selected: root.showSelection && root.vm && root.vm.selectedName === model.name
            onClicked: { root.aboutToSelect(); root.personClicked(model.name); list.forceActiveFocus() }
        }

        Keys.onUpPressed: move(-1)
        Keys.onDownPressed: move(1)
        function move(step) {
            if (!root.vm || count === 0) return
            const next = Math.max(0, Math.min(count - 1, root.vm.selectedRow + step))
            root.aboutToSelect()
            root.personClicked(root.vm.people.nameAt(next))
            positionViewAtIndex(next, ListView.Contain)
        }
        // A kijelölt sor látszódjon (mély hivatkozás, visszavonás utáni kijelölés).
        onCurrentIndexChanged: if (currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain)

        footer: Item {
            width: list.width
            height: hint.visible ? hint.height + 24 : noResult.visible ? noResult.height + 20 : 10

            // P06: csak a saját személy van.
            Item {
                id: hint
                visible: root.vm && root.vm.onlySelf && !root.vm.searching
                x: 6; y: 14
                width: parent.width - 12
                height: hintColumn.implicitHeight + 28
                TDashedRect { anchors.fill: parent; radius: Theme.radiusPopup }
                Column {
                    id: hintColumn
                    x: 14; y: 14
                    width: parent.width - 28
                    spacing: 6
                    TLabel {
                        width: parent.width
                        text: qsTr("Itt jelennek meg a résztvevők")
                        font.pixelSize: Theme.fontSmall
                        font.weight: Theme.weightSemiBold
                        wrapMode: Text.Wrap
                    }
                    TLabel {
                        width: parent.width
                        text: qsTr("Amikor az átiratban elnevezel egy beszélőt, személy lesz belőle, és a következő megbeszéléseken hangja alapján felismerjük.")
                        muted: true
                        font.pixelSize: Theme.fontSmall
                        cssLineHeight: 1.45
                        wrapMode: Text.Wrap
                    }
                }
            }

            // Eredménytelen keresés.
            Column {
                id: noResult
                objectName: "noResult"
                visible: root.vm && root.vm.searching && list.count === 0
                x: 6; y: 10
                width: parent.width - 12
                spacing: 8
                TLabel {
                    width: parent.width
                    text: qsTr("Becenevekben is kerestem.")
                    muted: true
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.Wrap
                }
                T.AbstractButton {
                    id: createLink
                    objectName: "createFromSearch"
                    width: parent.width
                    height: createText.implicitHeight + 6
                    hoverEnabled: true
                    activeFocusOnTab: true
                    Accessible.role: Accessible.Link
                    Accessible.name: createText.text
                    onClicked: root.newPersonRequested(search.text.trim())
                    Keys.onReturnPressed: click()
                    contentItem: Item {
                        TIcon { id: createIcon; y: 4; name: "user-plus"; size: 14; color: Theme.accent }
                        TLabel {
                            id: createText
                            x: 20
                            width: parent.width - 20
                            text: qsTr("Új személy: „%1”").arg(search.text.trim())
                            color: Theme.accent
                            font.pixelSize: Theme.fontSmall
                            font.weight: Theme.weightMedium
                            font.underline: createLink.hovered || createLink.visualFocus
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }
        }
    }
}
