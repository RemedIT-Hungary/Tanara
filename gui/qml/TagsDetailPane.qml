import QtQuick
import QtQuick.Templates as T

// A Címkék ablak jobb oldala (T10, T13): a kijelölt címke fejléce a műveletekkel (Átnevezés
// helyben, Összevonás…, törlés), a „tanult, nem kell szerkeszteni” sor, a profil két oszlopban
// (JELLEMZŐ RÉSZTVEVŐK · JELLEMZŐ KIFEJEZÉSEK + GYAKRAN EGYÜTT) és a megbeszélései. A
// kifejezések kerek, „#” nélküli pirulák — így nem téveszthetők össze a címkékkel.
// Üres készletnél (T13) a „Még nincs címke” állapot.
Item {
    id: root

    property var vm: null
    property bool renaming: false
    property string demoRenameText: ""
    readonly property var detail: vm ? vm.detail : ({})
    readonly property bool emptyState: !vm || !vm.hasSelection || detail.id === undefined
    readonly property int meetingLimit: 6

    signal mergeRequested()
    signal deleteRequested()
    signal tagClicked(string id)
    signal meetingRequested(string meetingId)
    signal openInLibraryRequested(string id)

    function startRename() {
        if (emptyState) return
        renameError.text = ""
        renameField.text = demoRenameText !== "" ? demoRenameText : detail.name
        renaming = true
        renameField.forceActiveFocus()
        if (demoRenameText === "") renameField.selectAll()
    }
    function commitRename() {
        const error = vm.rename(detail.id, renameField.text)
        if (error !== "") { renameError.text = error; return }
        renaming = false
    }
    function cancelRename() { renaming = false; renameError.text = "" }

    Connections {
        target: root.vm
        function onSelectionChanged() {
            root.renaming = false
            renameError.text = ""
            flick.contentY = 0
        }
    }

    // ---- üres állapot (T13) ----
    Column {
        objectName: "tagsEmptyState"
        visible: root.emptyState
        anchors.centerIn: parent
        width: Math.min(400, parent.width - 56)
        spacing: 10
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 40; height: 40; radius: 10
            color: Theme.sunken
            TIcon { anchors.centerIn: parent; name: "tag"; size: 20; color: Theme.textMuted }
        }
        TLabel {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("Még nincs címke")
            font.pixelSize: Theme.fontHeading
            font.weight: Theme.weightSemiBold
            wrapMode: Text.Wrap
        }
        TLabel {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("Címkét a megbeszélés fejlécében, a felvevőben vagy importkor adhatsz. Ha már van néhány, a rendszer a hasonló megbeszélésekre magától is javasol.")
            muted: true
            cssLineHeight: 1.5
            wrapMode: Text.Wrap
        }
    }

    Flickable {
        id: flick
        visible: !root.emptyState
        anchors.fill: parent
        contentWidth: width
        contentHeight: content.height + 46
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        T.ScrollBar.vertical: TScrollBar {}

        Column {
            id: content
            x: 28; y: 22
            width: Math.min(flick.width - 56, 760)
            spacing: 20

            // ---- fejléc ----
            Item {
                width: parent.width
                height: Math.max(44, headText.height)

                Rectangle {
                    id: bigTile
                    width: 44; height: 44; radius: 10
                    color: Theme.sunken
                    TLabel {
                        anchors.centerIn: parent
                        text: "#"
                        mono: true
                        muted: true
                        font.pixelSize: Theme.fontTitle
                        font.weight: Theme.weightMedium
                    }
                }

                Column {
                    id: headText
                    x: 58
                    width: parent.width - 58 - (actions.visible ? actions.width + 12 : 0)
                    spacing: 4

                    TLabel {
                        objectName: "tagDetailName"
                        visible: !root.renaming
                        width: parent.width
                        text: root.detail.name !== undefined ? root.detail.name : ""
                        font.pixelSize: Theme.fontTitle
                        font.weight: Theme.weightSemiBold
                        cssLineHeight: 1.25
                        wrapMode: Text.Wrap
                    }
                    TLabel {
                        objectName: "tagDetailMeta"
                        visible: !root.renaming
                        // A gombok alatt is elfér (azok csak a név magasságáig érnek).
                        width: content.width - 58
                        text: root.detail.meta !== undefined ? root.detail.meta : ""
                        muted: true
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.Wrap
                    }

                    // Átnevezés helyben.
                    Row {
                        visible: root.renaming
                        spacing: 6
                        TTextField {
                            id: renameField
                            objectName: "tagRenameField"
                            width: Math.min(320, headText.width - saveButton.width - cancelButton.width - 12)
                            height: 34
                            font.pixelSize: 18
                            font.weight: Theme.weightSemiBold
                            hasError: renameError.text !== ""
                            Accessible.name: qsTr("A címke új neve")
                            onAccepted: root.commitRename()
                            onTextEdited: renameError.text = ""
                            Keys.onEscapePressed: root.cancelRename()
                        }
                        TButton {
                            id: saveButton
                            objectName: "tagRenameSave"
                            text: qsTr("Mentés")
                            variant: "primary"
                            font.pixelSize: Theme.fontSmall
                            leftPadding: 12; rightPadding: 12
                            onClicked: root.commitRename()
                        }
                        TButton {
                            id: cancelButton
                            text: qsTr("Mégse")
                            variant: "ghost"
                            font.pixelSize: Theme.fontSmall
                            leftPadding: 10; rightPadding: 10
                            onClicked: root.cancelRename()
                        }
                    }
                    TLabel {
                        visible: root.renaming && renameError.text === ""
                        width: parent.width
                        topPadding: 4
                        text: qsTr("Enter: mentés · Esc: mégse. A régi névre a beviteli mező ezt a címkét ajánlja.")
                        muted: true
                        font.pixelSize: Theme.fontCaption
                        wrapMode: Text.Wrap
                    }
                    TLabel {
                        id: renameError
                        objectName: "tagRenameError"
                        visible: root.renaming && text !== ""
                        width: parent.width
                        topPadding: 4
                        color: Theme.dangerInk
                        font.pixelSize: Theme.fontCaption
                        wrapMode: Text.Wrap
                    }
                }

                Row {
                    id: actions
                    visible: !root.renaming
                    anchors.right: parent.right
                    spacing: 6
                    TButton {
                        objectName: "tagRenameButton"
                        height: 30
                        text: qsTr("Átnevezés")
                        iconName: "pencil"
                        iconSize: 14
                        spacing: 6
                        font.pixelSize: Theme.fontSmall
                        leftPadding: 10; rightPadding: 10
                        onClicked: root.startRename()
                    }
                    TButton {
                        objectName: "tagMergeButton"
                        height: 30
                        visible: root.vm && root.vm.totalCount > 1
                        text: qsTr("Összevonás…")
                        iconName: "merge"
                        iconSize: 14
                        spacing: 6
                        font.pixelSize: Theme.fontSmall
                        leftPadding: 10; rightPadding: 10
                        onClicked: root.mergeRequested()
                    }
                    TIconButton {
                        objectName: "tagDeleteButton"
                        width: 30; height: 30
                        iconName: "trash-2"
                        iconSize: 14
                        iconColor: Theme.dangerInk
                        toolTipText: qsTr("Címke törlése")
                        onClicked: root.deleteRequested()
                    }
                }
            }

            // ---- a profil tanult, nem szerkesztendő ----
            Rectangle {
                width: parent.width
                height: infoText.implicitHeight + 16
                radius: Theme.radiusControl
                color: Theme.surface
                border.width: 1
                border.color: Theme.border
                TIcon { id: infoIcon; x: 12; y: 9; name: "info"; size: 14; color: Theme.textMuted }
                TLabel {
                    id: infoText
                    x: 34; y: 8
                    width: parent.width - 46
                    text: qsTr("A profilt a rendszer a címke használatából tanulja; a javaslatok indoklása ebből jön. Nem kell szerkeszteni.")
                    muted: true
                    font.pixelSize: Theme.fontSmall
                    cssLineHeight: 1.35
                    wrapMode: Text.Wrap
                }
            }

            // ---- két oszlop ----
            Item {
                width: parent.width
                height: Math.max(peopleColumn.height, termsColumn.height)

                Column {
                    id: peopleColumn
                    width: (parent.width - 20) / 2
                    spacing: 8
                    TSectionLabel { text: qsTr("Jellemző résztvevők") }
                    Repeater {
                        model: root.detail.participants !== undefined ? root.detail.participants : []
                        Item {
                            required property var modelData
                            width: peopleColumn.width
                            height: 26
                            Rectangle {
                                id: av
                                anchors.verticalCenter: parent.verticalCenter
                                width: 24; height: 24; radius: 12
                                color: Theme.sunken
                                border.width: 1
                                border.color: Theme.border
                                TLabel {
                                    anchors.centerIn: parent
                                    text: parent.parent.modelData.monogram
                                    font.pixelSize: 9
                                    font.weight: Theme.weightBold
                                }
                            }
                            TLabel {
                                anchors.left: av.right
                                anchors.leftMargin: 10
                                anchors.right: personCount.left
                                anchors.rightMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                text: parent.modelData.name
                                font.pixelSize: Theme.fontSmall
                                elide: Text.ElideRight
                            }
                            TLabel {
                                id: personCount
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                text: parent.modelData.countText
                                mono: true
                                muted: true
                                font.pixelSize: Theme.fontCaption
                            }
                        }
                    }
                    TLabel {
                        visible: root.detail.participants !== undefined && root.detail.participants.length === 0
                        width: parent.width
                        text: qsTr("Még nincs elég megbeszélés hozzá.")
                        muted: true
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.Wrap
                    }
                }

                Column {
                    id: termsColumn
                    x: peopleColumn.width + 20
                    width: (parent.width - 20) / 2
                    spacing: 8
                    TSectionLabel { text: qsTr("Jellemző kifejezések") }
                    Flow {
                        width: parent.width
                        spacing: 6
                        Repeater {
                            model: root.detail.terms !== undefined ? root.detail.terms : []
                            Rectangle {
                                required property string modelData
                                width: termText.implicitWidth + 16
                                height: 24
                                radius: 12
                                color: Theme.sunken
                                TLabel {
                                    id: termText
                                    anchors.centerIn: parent
                                    text: parent.modelData
                                    font.pixelSize: Theme.fontSmall
                                }
                            }
                        }
                    }
                    Item { width: 1; height: 2 }
                    TSectionLabel {
                        visible: coRepeater.count > 0
                        text: qsTr("Gyakran együtt")
                    }
                    Flow {
                        visible: coRepeater.count > 0
                        width: parent.width
                        spacing: 6
                        Repeater {
                            id: coRepeater
                            model: root.detail.cooccurring !== undefined ? root.detail.cooccurring : []
                            TagChip {
                                required property var modelData
                                text: modelData.name
                                countText: modelData.countText
                                toolTipText: qsTr("Megnyitás: #%1").arg(modelData.name)
                                onClicked: root.tagClicked(modelData.id)
                            }
                        }
                    }
                }
            }

            // ---- megbeszélések ----
            Column {
                width: parent.width
                spacing: 8
                Row {
                    spacing: 8
                    TSectionLabel { text: qsTr("Megbeszélések") }
                    TLabel {
                        anchors.baseline: parent.children[0].baseline
                        text: root.detail.meetingsMeta !== undefined ? root.detail.meetingsMeta : ""
                        muted: true
                        font.pixelSize: Theme.fontCaption
                    }
                }
                Rectangle {
                    width: parent.width
                    height: meetingColumn.height
                    radius: Theme.radiusPopup
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.border

                    Column {
                        id: meetingColumn
                        width: parent.width
                        Repeater {
                            model: root.detail.meetings !== undefined ? root.detail.meetings.slice(0, root.meetingLimit) : []
                            T.AbstractButton {
                                id: meetingRow
                                required property var modelData
                                required property int index
                                width: meetingColumn.width
                                height: Math.max(36, meetingTitle.implicitHeight + 16)
                                hoverEnabled: true
                                Accessible.role: Accessible.Link
                                Accessible.name: modelData.title
                                onClicked: root.meetingRequested(modelData.meetingId)
                                background: Item {
                                    Rectangle {
                                        visible: meetingRow.index > 0
                                        width: parent.width; height: 1
                                        color: Theme.border
                                    }
                                    Rectangle {
                                        anchors.fill: parent
                                        anchors.margins: 1
                                        radius: meetingRow.index === 0 ? Theme.radiusPopup - 1 : 0
                                        color: Theme.stateLayer
                                        opacity: meetingRow.hovered ? Theme.hoverOpacity : 0
                                    }
                                }
                                contentItem: Item {
                                    TLabel {
                                        id: meetingTitle
                                        x: 12
                                        width: parent.width - 12 - 12 - 90 - 12 - 64 - 12
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: meetingRow.modelData.title
                                        font.pixelSize: Theme.fontSmall
                                        cssLineHeight: 1.4
                                        wrapMode: Text.Wrap
                                    }
                                    TLabel {
                                        x: parent.width - 12 - 64 - 12 - 90
                                        width: 90
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: meetingRow.modelData.dateText
                                        mono: true
                                        muted: true
                                        font.pixelSize: Theme.fontCaption
                                    }
                                    TLabel {
                                        x: parent.width - 12 - 64
                                        width: 64
                                        anchors.verticalCenter: parent.verticalCenter
                                        horizontalAlignment: Text.AlignRight
                                        text: meetingRow.modelData.durationText
                                        mono: true
                                        muted: true
                                        font.pixelSize: Theme.fontCaption
                                    }
                                }
                            }
                        }
                        T.AbstractButton {
                            id: libraryLink
                            objectName: "openInLibrary"
                            width: meetingColumn.width
                            height: 36
                            hoverEnabled: true
                            activeFocusOnTab: true
                            Accessible.role: Accessible.Link
                            Accessible.name: libraryText.text
                            onClicked: root.openInLibraryRequested(root.detail.id)
                            Keys.onReturnPressed: click()
                            background: Rectangle { width: parent.width; height: 1; color: Theme.border }
                            contentItem: TLabel {
                                id: libraryText
                                leftPadding: 12
                                verticalAlignment: Text.AlignVCenter
                                text: qsTr("Megnyitás a könyvtárban szűrőként")
                                color: Theme.accent
                                font.pixelSize: Theme.fontSmall
                                font.weight: Theme.weightMedium
                                font.underline: libraryLink.hovered || libraryLink.visualFocus
                            }
                        }
                    }
                }
            }
        }
    }
}
