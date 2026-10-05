import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// A könyvtár „+ Szűrő” popovere (C05, T04), 300 px: ÁLLAPOT jelölőnégyzetek (nincs
// összefoglaló / nincs átirat), RÉSZTVEVŐ (a leggyakoribb személyek), CÍMKE „Bármelyik /
// Mindegyik” választóval (VAGY / ÉS), címke-keresővel, a címkék darabszámmal, „Címke nélkül”,
// alul „Címkék kezelése…” (a kezelő ablak). A szűrés azonnal érvényes, a popover nyitva marad.
//   LibraryFilterPopover { library: libraryModel; onManageTagsRequested: shell.openTags() }
TPopover {
    id: root

    property var library: null               // LibraryListModel
    signal manageTagsRequested()

    width: 300
    padding: 6
    onOpened: tagSearch.text = ""

    // Egy jelölőnégyzetes sor (30 px): négyzet, opcionális „#”, név, jobbra szám.
    component OptionRow: T.AbstractButton {
        id: option
        property bool on: false
        property bool hash: false
        property string countText: ""
        property bool dim: false
        implicitHeight: 30
        hoverEnabled: true
        activeFocusOnTab: true
        Accessible.role: Accessible.CheckBox
        Accessible.checked: on
        Accessible.name: text
        Keys.onSpacePressed: click()
        Keys.onReturnPressed: click()

        background: Rectangle {
            radius: 5
            color: option.down ? Theme.alpha(Theme.stateLayer, Theme.pressedOpacity)
                 : option.hovered ? Theme.alpha(Theme.stateLayer, Theme.hoverOpacity) : "transparent"
            TFocusRing { visible: option.visualFocus; targetRadius: 5 }
        }
        contentItem: RowLayout {
            spacing: 10
            Rectangle {
                implicitWidth: 15; implicitHeight: 15
                radius: 4
                color: option.on ? Theme.accent : "transparent"
                border.width: option.on ? 0 : 1.5
                border.color: Theme.borderStrong
                TIcon {
                    anchors.centerIn: parent
                    visible: option.on
                    name: "check"
                    size: 11
                    strokeWidth: 3
                    color: Theme.textOnAccent
                }
            }
            Text {
                visible: option.hash
                text: "#"
                color: Theme.textMuted
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontCaption
            }
            TLabel {
                Layout.fillWidth: true
                text: option.text
                muted: option.dim
                font.pixelSize: Theme.fontSmall
                elide: Text.ElideRight
                wrapMode: Text.NoWrap
            }
            TLabel {
                visible: option.countText !== ""
                text: option.countText
                mono: true
                muted: true
                font.pixelSize: Theme.fontCaption
            }
        }
        leftPadding: 8; rightPadding: 8
    }

    component SectionTitle: TSectionLabel {
        font.pixelSize: Theme.fontMicro
    }

    contentItem: ColumnLayout {
        spacing: 0

        // ---- ÁLLAPOT ----
        SectionTitle {
            Layout.leftMargin: 8
            Layout.topMargin: 6
            Layout.bottomMargin: 4
            text: qsTr("Állapot")
        }
        OptionRow {
            Layout.fillWidth: true
            text: qsTr("Nincs összefoglaló")
            on: root.library ? root.library.noSummary : false
            onClicked: root.library.noSummary = !root.library.noSummary
        }
        OptionRow {
            Layout.fillWidth: true
            text: qsTr("Nincs átirat")
            on: root.library ? root.library.noTranscript : false
            onClicked: root.library.noTranscript = !root.library.noTranscript
        }

        // ---- RÉSZTVEVŐ (a leggyakoribbak; a többi névre a keresés is rátalál) ----
        TDivider {
            visible: peopleRepeater.count > 0
            Layout.fillWidth: true
            Layout.margins: 4
            Layout.topMargin: 6; Layout.bottomMargin: 6
        }
        SectionTitle {
            visible: peopleRepeater.count > 0
            Layout.leftMargin: 8
            Layout.bottomMargin: 4
            text: qsTr("Résztvevő")
        }
        Repeater {
            id: peopleRepeater
            model: root.visible && root.library ? root.library.peopleOptions.slice(0, 4) : []
            OptionRow {
                required property var modelData
                Layout.fillWidth: true
                text: modelData.name
                countText: String(modelData.count)
                on: root.library.people.indexOf(modelData.name) >= 0
                onClicked: on ? root.library.removePerson(modelData.name)
                              : root.library.addPerson(modelData.name)
            }
        }

        // ---- CÍMKE ----
        TDivider {
            Layout.fillWidth: true
            Layout.margins: 4
            Layout.topMargin: 6; Layout.bottomMargin: 6
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.topMargin: 2
            Layout.bottomMargin: 4
            spacing: 8
            SectionTitle {
                Layout.fillWidth: true
                text: qsTr("Címke")
            }
            // Bármelyik (VAGY) / Mindegyik (ÉS).
            Rectangle {
                objectName: "tagModeSwitch"
                implicitWidth: modeRow.implicitWidth + 4
                implicitHeight: 26
                radius: 6
                color: Theme.sunken
                Row {
                    id: modeRow
                    x: 2; y: 2
                    spacing: 2
                    Repeater {
                        model: [{ all: false, label: qsTr("Bármelyik") }, { all: true, label: qsTr("Mindegyik") }]
                        T.AbstractButton {
                            id: seg
                            required property var modelData
                            readonly property bool current: root.library ? root.library.tagsAll === modelData.all : !modelData.all
                            implicitWidth: segLabel.implicitWidth + 16
                            implicitHeight: 22
                            hoverEnabled: true
                            activeFocusOnTab: true
                            Accessible.role: Accessible.RadioButton
                            Accessible.checked: current
                            Accessible.name: modelData.label
                            onClicked: if (root.library) root.library.tagsAll = modelData.all
                            Keys.onSpacePressed: click()
                            background: Rectangle {
                                radius: Theme.radiusTag
                                color: seg.current ? Theme.raised
                                     : seg.hovered ? Theme.alpha(Theme.stateLayer, Theme.hoverOpacity) : "transparent"
                                TFocusRing { visible: seg.visualFocus; targetRadius: Theme.radiusTag }
                            }
                            contentItem: TLabel {
                                id: segLabel
                                text: seg.modelData.label
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                font.pixelSize: Theme.fontCaption
                                font.weight: seg.current ? Theme.weightSemiBold : Theme.weightRegular
                                muted: !seg.current
                            }
                        }
                    }
                }
            }
        }
        // Címke keresése (28 px, süllyesztett).
        Rectangle {
            Layout.fillWidth: true
            Layout.margins: 4
            Layout.topMargin: 4; Layout.bottomMargin: 2
            implicitHeight: 28
            radius: 5
            color: Theme.sunken
            border.width: tagSearch.activeFocus ? 1 : 0
            border.color: Theme.accent
            TIcon {
                id: searchIcon
                x: 8
                anchors.verticalCenter: parent.verticalCenter
                name: "search"
                size: 13
                color: Theme.textMuted
            }
            TextInput {
                id: tagSearch
                objectName: "tagFilterSearch"
                anchors { left: searchIcon.right; right: parent.right; leftMargin: 6; rightMargin: 8
                          verticalCenter: parent.verticalCenter }
                color: Theme.text
                selectionColor: Theme.accent
                selectedTextColor: Theme.textOnAccent
                selectByMouse: true
                clip: true
                font.family: Theme.fontSans
                font.pixelSize: Theme.fontSmall
                Keys.onDownPressed: tagList.forceActiveFocus()
                Text {
                    anchors.fill: parent
                    visible: !tagSearch.length && !tagSearch.preeditText
                    verticalAlignment: Text.AlignVCenter
                    text: qsTr("Címke keresése")
                    color: Theme.textMuted
                    font: tagSearch.font
                }
            }
        }
        ListView {
            id: tagList
            objectName: "tagFilterList"
            Layout.fillWidth: true
            // Legfeljebb ~6 sor látszik, a többi görgethető.
            Layout.preferredHeight: Math.min(contentHeight, 6 * 30)
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: root.visible && root.library ? root.library.tagOptionsMatching(tagSearch.text) : []
            T.ScrollBar.vertical: TScrollBar { opacity: active ? 1 : 0 }
            delegate: OptionRow {
                required property var modelData
                width: ListView.view.width
                hash: true
                text: modelData.name
                countText: String(modelData.count)
                on: root.library.tags.indexOf(modelData.id) >= 0
                onClicked: root.library.toggleTag(modelData.id)
            }
        }
        TLabel {
            visible: tagList.count === 0 && tagSearch.text !== ""
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.topMargin: 4; Layout.bottomMargin: 4
            text: qsTr("Nincs ilyen címke")
            muted: true
            font.pixelSize: Theme.fontSmall
        }
        OptionRow {
            Layout.fillWidth: true
            text: qsTr("Címke nélkül")
            dim: true
            countText: root.library ? String(root.library.untaggedCount) : ""
            on: root.library ? root.library.untagged : false
            onClicked: root.library.untagged = !root.library.untagged
        }

        TDivider {
            Layout.fillWidth: true
            Layout.margins: 4
            Layout.topMargin: 6; Layout.bottomMargin: 2
        }
        // Másodlagos út a kezelőhöz.
        T.AbstractButton {
            id: manage
            objectName: "manageTags"
            Layout.fillWidth: true
            implicitHeight: 32
            hoverEnabled: true
            activeFocusOnTab: true
            Accessible.role: Accessible.Link
            Accessible.name: qsTr("Címkék kezelése…")
            leftPadding: 8; rightPadding: 8
            onClicked: { root.close(); root.manageTagsRequested() }
            Keys.onReturnPressed: click()
            background: Rectangle {
                radius: 5
                color: manage.hovered ? Theme.alpha(Theme.stateLayer, Theme.hoverOpacity) : "transparent"
                TFocusRing { visible: manage.visualFocus; targetRadius: 5 }
            }
            contentItem: RowLayout {
                spacing: 6
                TIcon { name: "tag"; size: 14; color: Theme.accent }
                TLabel {
                    Layout.fillWidth: true
                    text: qsTr("Címkék kezelése…")
                    color: Theme.accent
                    font.pixelSize: Theme.fontSmall
                    font.weight: Theme.weightMedium
                }
            }
        }
    }
}
