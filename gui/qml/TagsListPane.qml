import QtQuick
import QtQuick.Templates as T

// A Címkék ablak bal oldali sávja (T10, T13): kereső („Címke keresése”), darabszám + rendezés
// (Legutóbb használt / ABC / Leggyakoribb), a lista; üres készletnél szaggatott tipp.
Rectangle {
    id: root

    property var vm: null
    property bool demoSortOpen: false
    signal tagClicked(string id)

    function focusSearch() { search.forceActiveFocus(); search.selectAll() }

    color: Theme.surface

    Item {
        id: top
        x: 12; y: 12
        width: parent.width - 24
        height: 32 + 8 + countRow.height

        TSearchField {
            id: search
            objectName: "tagsSearch"
            width: parent.width
            placeholderText: qsTr("Címke keresése")
            font.pixelSize: Theme.fontBody
            stateFocused: activeFocus || length > 0
            onTextChanged: if (root.vm && root.vm.filter !== text) root.vm.filter = text
            Component.onCompleted: if (root.vm) text = root.vm.filter
            Connections {
                target: root.vm
                function onFilterChanged() { if (search.text !== root.vm.filter) search.text = root.vm.filter }
            }
            Keys.onDownPressed: list.forceActiveFocus()
            Keys.onReturnPressed: if (list.count > 0) root.tagClicked(root.vm.tags.idAt(0))
            Accessible.name: qsTr("Keresés a címkék között")
        }

        Item {
            id: countRow
            y: 40
            width: parent.width
            height: 20
            TLabel {
                objectName: "tagsCount"
                anchors.verticalCenter: parent.verticalCenter
                x: 1
                text: root.vm ? root.vm.countText : ""
                muted: true
                font.pixelSize: Theme.fontCaption
            }
            T.AbstractButton {
                id: sortButton
                objectName: "tagsSortButton"
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
                    TMenuItem { text: qsTr("Legutóbb használt"); checked: root.vm && root.vm.sort === "recent"; onTriggered: root.vm.sort = "recent" }
                    TMenuItem { text: qsTr("ABC"); checked: root.vm && root.vm.sort === "alpha"; onTriggered: root.vm.sort = "alpha" }
                    TMenuItem { text: qsTr("Leggyakoribb"); checked: root.vm && root.vm.sort === "count"; onTriggered: root.vm.sort = "count" }
                }
            }
        }
    }
    onDemoSortOpenChanged: if (demoSortOpen) Qt.callLater(sortMenu.open)

    ListView {
        id: list
        objectName: "tagsList"
        anchors.top: top.bottom
        anchors.topMargin: 4
        anchors.bottom: parent.bottom
        x: 8
        width: parent.width - 16
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: root.vm ? root.vm.tags : null
        currentIndex: root.vm ? root.vm.selectedRow : -1
        highlightFollowsCurrentItem: false
        activeFocusOnTab: true
        T.ScrollBar.vertical: TScrollBar {}

        delegate: TagsListRow {
            required property var model
            width: list.width
            name: model.name
            meta: model.meta
            before: model.before
            match: model.match
            after: model.after
            selected: root.vm && root.vm.selectedId === model.tagId
            onClicked: { root.tagClicked(model.tagId); list.forceActiveFocus() }
        }

        Keys.onUpPressed: move(-1)
        Keys.onDownPressed: move(1)
        function move(step) {
            if (!root.vm || count === 0) return
            const next = Math.max(0, Math.min(count - 1, root.vm.selectedRow + step))
            root.tagClicked(root.vm.tags.idAt(next))
            positionViewAtIndex(next, ListView.Contain)
        }
        onCurrentIndexChanged: if (currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain)

        footer: Item {
            width: list.width
            height: emptyHint.visible ? emptyHint.height + 12 : noResult.visible ? noResult.implicitHeight + 20 : 10

            // T13: még nincs címke.
            Item {
                id: emptyHint
                objectName: "tagsEmptyHint"
                visible: root.vm !== null && root.vm.totalCount === 0
                x: 6; y: 6
                width: parent.width - 12
                height: hintText.implicitHeight + 28
                TDashedRect { anchors.fill: parent; radius: Theme.radiusPopup }
                TLabel {
                    id: hintText
                    x: 14; y: 14
                    width: parent.width - 28
                    text: qsTr("Itt jelennek meg a címkék, amint az első megbeszélésre felkerül egy.")
                    muted: true
                    font.pixelSize: Theme.fontSmall
                    cssLineHeight: 1.45
                    wrapMode: Text.Wrap
                }
            }
            TLabel {
                id: noResult
                visible: root.vm !== null && root.vm.searching && list.count === 0 && root.vm.totalCount > 0
                x: 6; y: 10
                width: parent.width - 12
                text: qsTr("Nincs ilyen címke.")
                muted: true
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.Wrap
            }
        }
    }
}
