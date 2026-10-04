import QtQuick
import QtQuick.Templates as T

// A Személyek ablak jobb oldala (P01–P03, P06): a kijelölt személy fejléce a műveletekkel,
// becenevek, megjegyzés, hangminták (vagy a „még nincs hanglenyomata” doboz) és a
// megbeszélései. Görgethető; a hosszú címek törnek.
Item {
    id: root

    property var vm: null
    // A P06 üres állapot helyett a saját személy adatai (ha a sorára kattintott).
    property bool showSelfDetail: false
    property bool renaming: false
    property bool aliasEditing: false
    property bool showAllSamples: false
    property bool showAllMeetings: false
    property string selectedSampleId: ""
    // Képernyőképhez: ennek a mintának a menüje nyitva; az átnevező / becenév mező szövege.
    property int demoMenuRow: -1
    property string demoRenameText: ""
    property string demoAliasText: ""

    signal mergeRequested()
    signal deleteRequested()
    signal newPersonRequested()
    signal sampleNewPersonRequested(string sampleId)
    signal sampleMoveRequested(string sampleId)

    readonly property bool emptyState: !vm || !vm.hasSelection || (vm.onlySelf && !showSelfDetail)
    readonly property int sampleLimit: 5
    readonly property int meetingLimit: 3

    function startRename() {
        if (!vm || emptyState) return
        renameError.text = ""
        renameField.text = demoRenameText !== "" ? demoRenameText : vm.selectedName
        renaming = true
        renameField.forceActiveFocus()
        if (demoRenameText === "") renameField.selectAll()
    }
    function commitRename() {
        const error = vm.rename(renameField.text)
        if (error !== "") { renameError.text = error; return }
        renaming = false
    }
    function cancelRename() { renaming = false; renameError.text = "" }
    function startAlias() {
        aliasError.text = ""
        aliasField.text = demoAliasText
        aliasEditing = true
        aliasField.forceActiveFocus()
    }
    function commitAlias(keepOpen) {
        if (aliasField.text.trim() === "") { aliasEditing = false; aliasError.text = ""; return }
        const error = vm.addAlias(aliasField.text)
        if (error !== "") { aliasError.text = error; return }
        aliasError.text = ""
        aliasField.text = ""
        aliasEditing = keepOpen
        if (keepOpen) aliasField.forceActiveFocus()
    }
    // A megjegyzés mentése, ha változott (fókuszvesztés, személyváltás, ablak bezárása előtt).
    function commitNote() {
        if (vm && vm.hasSelection && noteArea.text !== vm.note) vm.setNote(noteArea.text)
    }
    function playSelectedSample() {
        if (!vm || vm.sampleCount === 0) return
        vm.toggleSample(selectedSampleId !== "" ? selectedSampleId : vm.samples[0].id)
    }
    function openSampleMenu(row) {
        const item = sampleRepeater.itemAt(row)
        if (item) item.openMenu()
    }

    Connections {
        target: root.vm
        function onSelectionChanged() {
            root.renaming = false
            root.aliasEditing = false
            root.showAllSamples = false
            root.showAllMeetings = false
            root.selectedSampleId = ""
            renameError.text = ""
            aliasError.text = ""
            noteArea.text = root.vm.note
            flick.contentY = 0
        }
        function onDetailChanged() { if (!noteArea.activeFocus) noteArea.text = root.vm.note }
    }
    Component.onCompleted: if (vm) noteArea.text = vm.note

    // ---- üres állapot (P06) ----
    Column {
        visible: root.emptyState
        anchors.centerIn: parent
        width: Math.min(380, parent.width - 56)
        spacing: 10
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 40; height: 40; radius: 10
            color: Theme.sunken
            TIcon { anchors.centerIn: parent; name: "users"; size: 20; color: Theme.textMuted }
        }
        TLabel {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("Még csak te vagy itt")
            font.pixelSize: Theme.fontHeading
            font.weight: Theme.weightSemiBold
            wrapMode: Text.Wrap
        }
        TLabel {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("Nevezd el a beszélőket egy megbeszélés átiratában, vagy vegyél fel valakit előre, hogy a személyválasztóban már szerepeljen.")
            muted: true
            cssLineHeight: 1.5
            wrapMode: Text.Wrap
        }
        Item { width: 1; height: 4 }
        TButton {
            objectName: "emptyNewPerson"
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Új személy")
            iconName: "user-plus"
            iconSize: 15
            onClicked: root.newPersonRequested()
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
                    id: bigAvatar
                    width: 44; height: 44; radius: 22
                    color: root.vm && root.vm.selectedIsSelf ? Theme.accent : Theme.sunken
                    border.width: 1
                    border.color: root.vm && root.vm.selectedIsSelf ? Theme.accent : Theme.border
                    TLabel {
                        anchors.centerIn: parent
                        text: root.vm ? root.vm.selectedMonogram : ""
                        color: root.vm && root.vm.selectedIsSelf ? Theme.textOnAccent : Theme.text
                        font.weight: Theme.weightBold
                    }
                }

                Column {
                    id: headText
                    x: 58
                    width: parent.width - 58 - (actions.visible ? actions.width + 12 : 0)
                    spacing: 4

                    // Név + adatok.
                    TLabel {
                        objectName: "detailName"
                        visible: !root.renaming
                        width: parent.width
                        text: root.vm ? root.vm.selectedName : ""
                        font.pixelSize: Theme.fontTitle
                        font.weight: Theme.weightSemiBold
                        cssLineHeight: 1.25
                        wrapMode: Text.Wrap
                    }
                    TLabel {
                        objectName: "detailMeta"
                        visible: !root.renaming
                        width: parent.width
                        text: root.vm ? root.vm.detailMeta : ""
                        muted: true
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.Wrap
                    }

                    // Átnevezés helyben (P02).
                    Row {
                        visible: root.renaming
                        spacing: 6
                        TTextField {
                            id: renameField
                            objectName: "renameField"
                            width: Math.min(320, headText.width - saveButton.width - cancelButton.width - 12)
                            height: 34
                            font.pixelSize: 18
                            font.weight: Theme.weightSemiBold
                            hasError: renameError.text !== ""
                            Accessible.name: qsTr("A személy új neve")
                            onAccepted: root.commitRename()
                            onTextEdited: renameError.text = ""
                            Keys.onEscapePressed: root.cancelRename()
                        }
                        TButton {
                            id: saveButton
                            objectName: "renameSave"
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
                        text: qsTr("Enter: mentés · Esc: mégse. A régi név becenévként megmarad.")
                        muted: true
                        font.pixelSize: Theme.fontCaption
                        wrapMode: Text.Wrap
                    }
                    TLabel {
                        id: renameError
                        objectName: "renameError"
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
                        objectName: "renameButton"
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
                        objectName: "mergeButton"
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
                    // A saját személy nem törölhető: a gomb nála nincs.
                    TIconButton {
                        objectName: "deleteButton"
                        visible: root.vm && !root.vm.selectedIsSelf
                        width: 30; height: 30
                        iconName: "trash-2"
                        iconSize: 14
                        iconColor: Theme.dangerInk
                        toolTipText: qsTr("Személy törlése")
                        onClicked: root.deleteRequested()
                    }
                }
            }

            // ---- becenevek + megjegyzés ----
            Item {
                width: parent.width
                height: Math.max(aliasColumn.height, noteColumn.height)

                Column {
                    id: aliasColumn
                    width: (parent.width - 16) / 2
                    spacing: 8
                    TSectionLabel { text: qsTr("Becenevek") }
                    Flow {
                        width: parent.width
                        spacing: 6
                        Repeater {
                            model: root.vm ? root.vm.aliases : []
                            Rectangle {
                                id: chip
                                required property string modelData
                                objectName: "aliasChip"
                                width: Math.min(chipLabel.implicitWidth + 10 + 6 + 12 + 6, aliasColumn.width)
                                height: 26
                                radius: 13
                                color: Theme.raised
                                border.width: 1
                                border.color: Theme.border
                                TLabel {
                                    id: chipLabel
                                    x: 10
                                    width: parent.width - 34
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: chip.modelData
                                    font.pixelSize: Theme.fontSmall
                                    elide: Text.ElideRight
                                }
                                T.AbstractButton {
                                    id: removeAlias
                                    objectName: "aliasRemove"
                                    anchors.right: parent.right
                                    anchors.rightMargin: 3
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 20; height: 20
                                    hoverEnabled: true
                                    activeFocusOnTab: true
                                    Accessible.name: qsTr("Becenév törlése: %1").arg(chip.modelData)
                                    onClicked: root.vm.removeAlias(chip.modelData)
                                    Keys.onReturnPressed: click()
                                    background: Rectangle {
                                        radius: 10
                                        color: Theme.stateLayer
                                        opacity: removeAlias.down ? Theme.pressedOpacity : removeAlias.hovered ? Theme.hoverOpacity : 0
                                        TFocusRing { visible: removeAlias.visualFocus; targetRadius: 10 }
                                    }
                                    contentItem: Item {
                                        TIcon { anchors.centerIn: parent; name: "x"; size: 12; color: Theme.textMuted }
                                    }
                                }
                            }
                        }
                        // A „+ Becenév” helyben mezővé alakul.
                        TTextField {
                            id: aliasField
                            objectName: "aliasField"
                            visible: root.aliasEditing
                            width: 110
                            height: 26
                            leftPadding: 10; rightPadding: 10
                            font.pixelSize: Theme.fontSmall
                            hasError: aliasError.text !== ""
                            Accessible.name: qsTr("Új becenév")
                            background: Rectangle {
                                radius: 13
                                color: Theme.raised
                                border.width: 1
                                border.color: aliasField.hasError ? Theme.danger : Theme.accent
                                TFocusRing {
                                    visible: aliasField.activeFocus
                                    targetRadius: 13
                                    border.color: aliasField.hasError ? Theme.dangerSoft : Theme.accentSoft
                                }
                            }
                            onAccepted: root.commitAlias(true)
                            onTextEdited: aliasError.text = ""
                            Keys.onEscapePressed: { root.aliasEditing = false; aliasError.text = ""; addAliasChip.forceActiveFocus() }
                            onActiveFocusChanged: if (!activeFocus && root.aliasEditing && root.demoAliasText === "") root.commitAlias(false)
                        }
                        TChip {
                            id: addAliasChip
                            objectName: "addAlias"
                            height: 26
                            dashed: true
                            text: qsTr("Becenév")
                            font.pixelSize: Theme.fontSmall
                            leftPadding: 10; rightPadding: 10
                            onClicked: root.startAlias()
                        }
                    }
                    TLabel {
                        id: aliasError
                        objectName: "aliasError"
                        visible: text !== ""
                        width: parent.width
                        color: Theme.dangerInk
                        font.pixelSize: Theme.fontCaption
                        wrapMode: Text.Wrap
                    }
                    TLabel {
                        visible: aliasError.text === ""
                        width: parent.width
                        text: qsTr("A keresés és a személyválasztó ezekre is talál.")
                        muted: true
                        font.pixelSize: Theme.fontCaption
                        cssLineHeight: 1.4
                        wrapMode: Text.Wrap
                    }
                }

                Column {
                    id: noteColumn
                    x: aliasColumn.width + 16
                    width: aliasColumn.width
                    spacing: 8
                    TSectionLabel { text: qsTr("Megjegyzés") }
                    TTextArea {
                        id: noteArea
                        objectName: "noteArea"
                        width: parent.width
                        implicitHeight: Math.max(72, contentHeight + topPadding + bottomPadding)
                        leftPadding: 10; rightPadding: 10; topPadding: 8; bottomPadding: 8
                        font.pixelSize: Theme.fontSmall
                        placeholderText: qsTr("Megjegyzés hozzáadása…")
                        Accessible.name: qsTr("Megjegyzés a személyhez")
                        // Automatikus mentés, amikor a mező elveszti a fókuszt.
                        onActiveFocusChanged: if (!activeFocus) root.commitNote()
                        Keys.onEscapePressed: { text = root.vm.note; flick.forceActiveFocus() }
                        background: Rectangle {
                            radius: Theme.radiusControl
                            color: Theme.raised
                            border.width: 1
                            border.color: noteArea.activeFocus ? Theme.accent : Theme.border
                            TFocusRing { visible: noteArea.activeFocus; border.color: Theme.accentSoft }
                        }
                    }
                }
            }

            // ---- hanglenyomat ----
            Column {
                width: parent.width
                spacing: 8
                Row {
                    spacing: 8
                    TSectionLabel { id: fpLabel; text: qsTr("Hanglenyomat") }
                    TLabel {
                        objectName: "samplesSub"
                        anchors.baseline: fpLabel.baseline
                        text: root.vm ? root.vm.samplesSubText : ""
                        muted: true
                        font.pixelSize: Theme.fontCaption
                    }
                }

                // Minták listája.
                Rectangle {
                    visible: root.vm && root.vm.sampleCount > 0
                    width: parent.width
                    height: sampleColumn.height + 2
                    radius: Theme.radiusPopup
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.border
                    Column {
                        id: sampleColumn
                        x: 1; y: 1
                        width: parent.width - 2
                        Repeater {
                            id: sampleRepeater
                            model: !root.vm ? [] : root.showAllSamples ? root.vm.samples
                                 : root.vm.samples.slice(0, root.sampleLimit)
                            PeopleSampleRow {
                                required property var modelData
                                required property int index
                                objectName: "sampleRow"
                                width: sampleColumn.width
                                sample: modelData
                                first: index === 0
                                playing: root.vm.playingSampleId === modelData.id
                                selected: root.selectedSampleId === modelData.id
                                onClicked: root.selectedSampleId = modelData.id
                                onPlayRequested: { root.selectedSampleId = modelData.id; root.vm.toggleSample(modelData.id) }
                                onNewPersonRequested: root.sampleNewPersonRequested(modelData.id)
                                onMoveRequested: root.sampleMoveRequested(modelData.id)
                                onDeleteRequested: root.vm.removeSample(modelData.id)
                            }
                        }
                        Item {
                            visible: root.vm && root.vm.sampleCount > root.sampleLimit
                            width: parent.width
                            height: visible ? 34 : 0
                            Rectangle { width: parent.width; height: 1; color: Theme.border }
                            T.AbstractButton {
                                id: moreSamples
                                objectName: "allSamples"
                                x: 6
                                anchors.verticalCenter: parent.verticalCenter
                                width: moreSamplesLabel.implicitWidth + 12
                                height: 26
                                hoverEnabled: true
                                activeFocusOnTab: true
                                Accessible.role: Accessible.Link
                                Accessible.name: moreSamplesLabel.text
                                onClicked: root.showAllSamples = !root.showAllSamples
                                Keys.onReturnPressed: click()
                                background: Rectangle {
                                    radius: 4
                                    color: Theme.stateLayer
                                    opacity: moreSamples.down ? Theme.pressedOpacity : moreSamples.hovered ? Theme.hoverOpacity : 0
                                    TFocusRing { visible: moreSamples.visualFocus; targetRadius: 4 }
                                }
                                contentItem: Item {
                                    TLabel {
                                        id: moreSamplesLabel
                                        anchors.centerIn: parent
                                        text: root.showAllSamples ? qsTr("Csak a legutóbbiak")
                                            : qsTr("Az összes minta (%1)").arg(root.vm ? root.vm.sampleCount : 0)
                                        color: Theme.accent
                                        font.pixelSize: Theme.fontSmall
                                        font.weight: Theme.weightMedium
                                    }
                                }
                            }
                        }
                    }
                }

                // Még nincs hanglenyomata (P03).
                Item {
                    visible: root.vm && root.vm.sampleCount === 0
                    width: parent.width
                    height: noPrint.height + 28
                    TDashedRect { anchors.fill: parent; radius: Theme.radiusPopup }
                    Rectangle {
                        x: 16; y: 14
                        width: 32; height: 32; radius: 8
                        color: Theme.sunken
                        TIcon { anchors.centerIn: parent; name: "fingerprint"; size: 16; color: Theme.textMuted }
                    }
                    Column {
                        id: noPrint
                        x: 62; y: 14
                        width: parent.width - 62 - 16
                        spacing: 8
                        TLabel {
                            width: parent.width
                            text: qsTr("Még nincs hanglenyomata")
                            font.weight: Theme.weightSemiBold
                            wrapMode: Text.Wrap
                        }
                        TLabel {
                            objectName: "planText"
                            width: parent.width
                            text: !root.vm ? "" : root.vm.planReady ? root.vm.planText : qsTr("Megbeszélések átnézése…")
                            muted: true
                            font.pixelSize: Theme.fontSmall
                            cssLineHeight: 1.45
                            wrapMode: Text.Wrap
                        }
                        Flow {
                            visible: root.vm && root.vm.planReady && root.vm.planPossible
                            width: parent.width
                            spacing: 8
                            TButton {
                                objectName: "createVoiceprint"
                                height: 30
                                enabled: root.vm && !root.vm.creatingVoiceprint
                                // A letiltott gomb szaggatott kerete a szoftveres rajzolóval kilógna
                                // a görgetett területről: készítés közben a haladás-sor váltja.
                                visible: root.vm && !root.vm.creatingVoiceprint
                                text: root.vm ? root.vm.planButtonText : ""
                                variant: "primary"
                                iconName: "fingerprint"
                                iconSize: 14
                                spacing: 6
                                font.pixelSize: Theme.fontSmall
                                leftPadding: 12; rightPadding: 12
                                onClicked: root.vm.createVoiceprint()
                            }
                            Row {
                                visible: root.vm && root.vm.creatingVoiceprint
                                height: 30
                                spacing: 8
                                TSpinner { anchors.verticalCenter: parent.verticalCenter; size: 15; color: Theme.accent }
                                TLabel {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: root.vm ? qsTr("Minta készítése: %1").arg(root.vm.creatingText) : ""
                                    font.pixelSize: Theme.fontSmall
                                }
                            }
                            TLabel {
                                visible: root.vm && !root.vm.creatingVoiceprint
                                height: 30
                                verticalAlignment: Text.AlignVCenter
                                text: root.vm ? root.vm.planNote : ""
                                muted: true
                                font.pixelSize: Theme.fontCaption
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
                    TSectionLabel { id: mtgLabel; text: qsTr("Megbeszélések") }
                    TLabel {
                        anchors.baseline: mtgLabel.baseline
                        visible: root.vm && root.vm.statsReady && root.vm.meetingCount > 0
                        text: !root.vm ? "" : root.vm.meetingCount > root.meetingLimit
                            ? qsTr("%1 · legújabb elöl").arg(root.vm.meetingCount) : String(root.vm.meetingCount)
                        muted: true
                        font.pixelSize: Theme.fontCaption
                    }
                }
                Rectangle {
                    width: parent.width
                    height: meetingColumn.height + 2
                    radius: Theme.radiusPopup
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.border
                    Column {
                        id: meetingColumn
                        x: 1; y: 1
                        width: parent.width - 2
                        TLabel {
                            visible: root.vm && (!root.vm.statsReady || root.vm.meetingCount === 0)
                            width: parent.width
                            leftPadding: 12; rightPadding: 12; topPadding: 10; bottomPadding: 10
                            text: !root.vm ? "" : !root.vm.statsReady ? qsTr("Megbeszélések számolása…")
                                : qsTr("Még egy megbeszélésen sem szerepel.")
                            muted: true
                            font.pixelSize: Theme.fontSmall
                            wrapMode: Text.Wrap
                        }
                        Repeater {
                            model: !root.vm ? [] : root.showAllMeetings ? root.vm.meetings
                                 : root.vm.meetings.slice(0, root.meetingLimit)
                            // A sorok nem műveletek (ebben a változatban innen nem nyílik megbeszélés).
                            Item {
                                id: meetingRow
                                required property var modelData
                                required property int index
                                objectName: "meetingRow"
                                width: meetingColumn.width
                                height: Math.max(meetingTitle.implicitHeight, 18) + 16
                                Rectangle { visible: meetingRow.index > 0; width: parent.width; height: 1; color: Theme.border }
                                TLabel {
                                    id: meetingTitle
                                    x: 12
                                    width: parent.width - 12 - 12 - 86 - 12 - 56 - 12
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: meetingRow.modelData.title
                                    font.pixelSize: Theme.fontSmall
                                    cssLineHeight: 1.4
                                    wrapMode: Text.Wrap
                                }
                                TLabel {
                                    anchors.right: talk.left
                                    anchors.rightMargin: 12
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 86
                                    text: meetingRow.modelData.date
                                    mono: true
                                    muted: true
                                    font.pixelSize: Theme.fontCaption
                                }
                                TLabel {
                                    id: talk
                                    anchors.right: parent.right
                                    anchors.rightMargin: 12
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 56
                                    horizontalAlignment: Text.AlignRight
                                    text: meetingRow.modelData.talk
                                    mono: true
                                    muted: true
                                    font.pixelSize: Theme.fontCaption
                                }
                            }
                        }
                        Item {
                            visible: root.vm && root.vm.meetingCount > root.meetingLimit
                            width: parent.width
                            height: visible ? 34 : 0
                            Rectangle { width: parent.width; height: 1; color: Theme.border }
                            T.AbstractButton {
                                id: moreMeetings
                                objectName: "allMeetings"
                                x: 6
                                anchors.verticalCenter: parent.verticalCenter
                                width: moreMeetingsLabel.implicitWidth + 12
                                height: 26
                                hoverEnabled: true
                                activeFocusOnTab: true
                                Accessible.role: Accessible.Link
                                Accessible.name: moreMeetingsLabel.text
                                onClicked: root.showAllMeetings = !root.showAllMeetings
                                Keys.onReturnPressed: click()
                                background: Rectangle {
                                    radius: 4
                                    color: Theme.stateLayer
                                    opacity: moreMeetings.down ? Theme.pressedOpacity : moreMeetings.hovered ? Theme.hoverOpacity : 0
                                    TFocusRing { visible: moreMeetings.visualFocus; targetRadius: 4 }
                                }
                                contentItem: Item {
                                    TLabel {
                                        id: moreMeetingsLabel
                                        anchors.centerIn: parent
                                        text: root.showAllMeetings ? qsTr("Csak a legutóbbiak")
                                            : qsTr("Az összes megbeszélés (%1)").arg(root.vm ? root.vm.meetingCount : 0)
                                        color: Theme.accent
                                        font.pixelSize: Theme.fontSmall
                                        font.weight: Theme.weightMedium
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
