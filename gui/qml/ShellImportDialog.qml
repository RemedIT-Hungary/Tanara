import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// „Hangfájl importálása” — meglévő hang- / videófájlokból új megbeszélés (fájlonként egy
// sáv; a többcsatornás fájl kérésre csatornánként külön sávra bontva). Az M10 párbeszédablak-
// minta szélesebb (560 px) változata, két állapottal:
//   űrlap  — a fájlok a beolvasott adataikkal, cím + dátum, „saját mikrofon”, sávszám szóban;
//   futás  — valós haladás, „Megszakítás”, és „Háttérben folytatom” (az ablak bezárható, az
//            importálás megy tovább; az oldalsáv ShellImportStrip-je mutatja).
// Az adat a ShellImportModelből jön (`model`); a natív fájlválasztót a `shell` nyitja.
TDialog {
    id: root

    property var model: null                 // ShellImportModel
    property var shell: null                 // ShellActions

    readonly property bool running: model ? model.running : false

    // Megnyitás a megadott fájlokkal (üres lista: csak megnyílik — futó importálásnál a
    // haladást mutatja, különben az üres űrlapot a „Fájlok kiválasztása…” gombbal).
    function openWith(files) {
        if (model && !model.running && files && files.length > 0)
            model.addFiles(files)
        open()
    }
    function pickMore() {
        if (!root.shell || !root.model)
            return
        const picked = root.shell.pickAudioFiles()
        if (picked.length > 0)
            root.model.addFiles(picked)
    }

    objectName: "importDialog"
    width: Math.min(560, (parent ? parent.width : 560) - 32)
    title: running ? qsTr("Importálás folyamatban") : qsTr("Hangfájl importálása")
    // Mellé kattintásra ne vesszen el a kitöltött űrlap; Esc = „Mégse” / „Háttérben folytatom”.
    closePolicy: T.Popup.CloseOnEscape
    onRejected: if (model && !model.running) model.reset()

    // ---- futás -------------------------------------------------------------------------
    ColumnLayout {
        visible: root.running
        Layout.fillWidth: true
        spacing: 10

        TLabel {
            Layout.fillWidth: true
            text: root.model ? root.model.runningTitle : ""
            font.weight: Theme.weightSemiBold
            elide: Text.ElideRight
        }
        TProgressBar {
            objectName: "importProgress"
            Layout.fillWidth: true
            thickness: 6
            indeterminate: !root.model || root.model.percent < 0
            value: root.model && root.model.percent >= 0 ? root.model.percent / 100 : 0
        }
        TLabel {
            Layout.fillWidth: true
            text: root.model ? root.model.progressText : ""
            mono: true
            muted: true
            font.pixelSize: Theme.fontCaption
        }
        TLabel {
            Layout.fillWidth: true
            Layout.topMargin: 2
            text: qsTr("Közben nyugodtan dolgozhatsz tovább: az importálás a háttérben is megy, "
                       + "a végén az új megbeszélés megjelenik a könyvtárban. Az eredeti fájlokhoz "
                       + "nem nyúlunk.")
            muted: true
            font.pixelSize: Theme.fontSmall
            cssLineHeight: 1.5
            wrapMode: Text.Wrap
        }
    }

    // ---- űrlap -------------------------------------------------------------------------
    TBanner {
        visible: !root.running && root.model && root.model.error !== ""
        Layout.fillWidth: true
        tone: "danger"
        title: qsTr("Az importálás nem sikerült")
        text: root.model ? root.model.error : ""
    }

    // Még nincs fájl: ejtési felület + választó gomb.
    TDashedRect {
        visible: !root.running && root.model && root.model.fileCount === 0
        Layout.fillWidth: true
        Layout.preferredHeight: 148
        radius: Theme.radiusPopup
        color: emptyDrop.containsDrag ? Theme.accent : Theme.borderStrong
        fillColor: emptyDrop.containsDrag ? Theme.accentSoft : "transparent"
        ColumnLayout {
            anchors.centerIn: parent
            width: parent.width - 2 * Theme.space5
            spacing: 8
            TIcon { Layout.alignment: Qt.AlignHCenter; name: "import"; size: 22; color: Theme.textMuted }
            TLabel {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("Húzd ide a hang- vagy videófájlokat")
                font.weight: Theme.weightSemiBold
                wrapMode: Text.Wrap
            }
            TButton {
                objectName: "importPickFirst"
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 2
                text: qsTr("Fájlok kiválasztása…")
                size: "small"
                enabled: root.shell !== null
                onClicked: root.pickMore()
            }
        }
        DropArea {
            id: emptyDrop
            anchors.fill: parent
            onDropped: (drop) => { if (drop.hasUrls && root.model) { root.model.addFiles(drop.urls); drop.accept() } }
        }
    }

    // A fájlok listája.
    Rectangle {
        visible: !root.running && root.model && root.model.fileCount > 0
        Layout.fillWidth: true
        // Alacsony ablakban rövidebb lista (görgethető), hogy az űrlap és a gombok kiférjenek.
        implicitHeight: Math.min(fileColumn.implicitHeight,
                                 Math.max(120, Math.min(264, (root.parent ? root.parent.height : 820) - 556))) + 2
        radius: Theme.radiusPopup
        color: listDrop.containsDrag ? Theme.accentSoft : Theme.raised
        border.width: 1
        border.color: listDrop.containsDrag ? Theme.accent : Theme.border

        Flickable {
            id: fileFlick
            anchors { fill: parent; margins: 1 }
            contentHeight: fileColumn.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            T.ScrollBar.vertical: TScrollBar {}

            Column {
                id: fileColumn
                width: fileFlick.width
                Repeater {
                    model: root.model ? root.model.files : []
                    Item {
                        id: fileRow
                        required property var modelData
                        required property int index
                        width: fileColumn.width
                        implicitHeight: rowLayout.implicitHeight + 20
                        height: implicitHeight

                        TDivider { visible: fileRow.index > 0; anchors { left: parent.left; right: parent.right; top: parent.top } }
                        ColumnLayout {
                            id: rowLayout
                            anchors { left: parent.left; right: parent.right; top: parent.top
                                      leftMargin: 12; rightMargin: 6; topMargin: 10 }
                            spacing: 8

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10
                                TIcon {
                                    Layout.alignment: Qt.AlignTop
                                    Layout.topMargin: 2
                                    name: fileRow.modelData.state === "error" ? "circle-alert"
                                        : fileRow.modelData.video ? "file-video" : "file-audio"
                                    size: 16
                                    color: fileRow.modelData.state === "error" ? Theme.danger : Theme.textMuted
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 1
                                    TLabel {
                                        Layout.fillWidth: true
                                        text: fileRow.modelData.name
                                        font.weight: Theme.weightMedium
                                        elide: Text.ElideMiddle
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 6
                                        TSpinner {
                                            visible: fileRow.modelData.state === "probing"
                                            size: 12
                                            color: Theme.textMuted
                                        }
                                        TLabel {
                                            Layout.fillWidth: true
                                            text: fileRow.modelData.state === "probing" ? qsTr("Adatok beolvasása…")
                                                : fileRow.modelData.state === "error" ? fileRow.modelData.error
                                                : fileRow.modelData.meta
                                            mono: fileRow.modelData.state === "ok"
                                            color: fileRow.modelData.state === "error" ? Theme.dangerInk : Theme.textMuted
                                            font.pixelSize: Theme.fontCaption
                                            wrapMode: Text.Wrap
                                        }
                                    }
                                }
                                TIconButton {
                                    Layout.alignment: Qt.AlignTop
                                    iconName: "x"
                                    iconSize: 14
                                    variant: "flat"
                                    size: "small"
                                    toolTipText: qsTr("Kivétel a listából")
                                    onClicked: root.model.removeFile(fileRow.index)
                                }
                            }
                            // Többcsatornás fájl: a bontás látható, egy mozdulattal állítható döntés.
                            RowLayout {
                                visible: fileRow.modelData.canSplit
                                Layout.fillWidth: true
                                Layout.leftMargin: 26
                                Layout.rightMargin: 8
                                spacing: 12
                                TSwitch {
                                    objectName: "importSplit" + fileRow.index
                                    Layout.alignment: Qt.AlignTop
                                    checked: fileRow.modelData.split
                                    onToggled: root.model.setSplit(fileRow.index, checked)
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 1
                                    TLabel {
                                        Layout.fillWidth: true
                                        text: qsTr("Csatornánként külön sávra")
                                        font.pixelSize: Theme.fontSmall
                                        font.weight: Theme.weightMedium
                                    }
                                    TLabel {
                                        Layout.fillWidth: true
                                        text: fileRow.modelData.splitHint
                                        muted: true
                                        font.pixelSize: Theme.fontCaption
                                        cssLineHeight: 1.45
                                        wrapMode: Text.Wrap
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        DropArea {
            id: listDrop
            anchors.fill: parent
            onDropped: (drop) => { if (drop.hasUrls && root.model) { root.model.addFiles(drop.urls); drop.accept() } }
        }
    }

    RowLayout {
        visible: !root.running && root.model && root.model.fileCount > 0
        Layout.fillWidth: true
        Layout.topMargin: -4
        spacing: 10
        TButton {
            objectName: "importAddFile"
            text: qsTr("Fájl hozzáadása…")
            iconName: "plus"
            size: "small"
            enabled: root.shell !== null
            onClicked: root.pickMore()
        }
        TLabel {
            Layout.fillWidth: true
            text: qsTr("Több fájl egyszerre indul: mindegyik külön sáv, közös kezdettel.")
            muted: true
            font.pixelSize: Theme.fontCaption
            elide: Text.ElideRight
        }
    }

    // Cím + dátum.
    RowLayout {
        visible: !root.running && root.model && root.model.fileCount > 0
        Layout.fillWidth: true
        spacing: 12
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            TLabel { text: qsTr("Cím"); font.pixelSize: Theme.fontSmall; font.weight: Theme.weightMedium }
            TTextField {
                id: titleField
                objectName: "importTitle"
                Layout.fillWidth: true
                text: root.model ? root.model.title : ""
                placeholderText: qsTr("A megbeszélés címe")
                onTextEdited: root.model.title = text
            }
        }
        ColumnLayout {
            Layout.preferredWidth: 176
            Layout.maximumWidth: 176
            spacing: 4
            TLabel { text: qsTr("Mikor készült?"); font.pixelSize: Theme.fontSmall; font.weight: Theme.weightMedium }
            TTextField {
                id: dateField
                objectName: "importDate"
                Layout.fillWidth: true
                mono: true
                text: root.model ? root.model.dateText : ""
                placeholderText: "2026-03-05 14:30"
                hasError: root.model ? (!root.model.dateValid && !root.model.probing) : false
                onTextEdited: root.model.dateText = text
            }
        }
    }
    TLabel {
        visible: !root.running && root.model && root.model.fileCount > 0 && text !== ""
        Layout.fillWidth: true
        Layout.topMargin: -8
        horizontalAlignment: Text.AlignRight
        text: root.model ? root.model.dateHint : ""
        color: root.model && !root.model.dateValid ? Theme.dangerInk : Theme.textMuted
        font.pixelSize: Theme.fontCaption
        wrapMode: Text.Wrap
    }

    // Címkék (C07, T09): többchipes mező + javaslat-sor a cím (fájlnév) alapján. Az importált
    // megbeszélésre kerülnek, amikor elkészült.
    ColumnLayout {
        visible: !root.running && root.model && root.model.fileCount > 0
        Layout.fillWidth: true
        spacing: 4

        TagInputModel {
            id: importTagInput
            controller: App.controller
            excludeIds: root.model ? root.model.tagIds : []
        }
        TLabel { text: qsTr("Címkék"); font.pixelSize: Theme.fontSmall; font.weight: Theme.weightMedium }
        TagField {
            objectName: "importTags"
            Layout.fillWidth: true
            tags: root.model ? root.model.tags : []
            inputModel: importTagInput
            onAddRequested: (name, isNew) => root.model.addTag(name)
            onRemoveRequested: (id) => root.model.removeTag(id)
        }
        RowLayout {
            objectName: "importTagSuggestions"
            visible: root.model !== null && root.model.suggestions.length > 0
            Layout.fillWidth: true
            Layout.topMargin: 4
            spacing: 8
            TLabel {
                text: qsTr("Javasolt:")
                muted: true
                font.pixelSize: Theme.fontCaption
            }
            Repeater {
                // Legfeljebb kettő látszik (mint a fejléc sorában).
                model: root.model ? root.model.suggestions.slice(0, 2) : []
                TagChip {
                    required property var modelData
                    required property int index
                    kind: modelData.isNew ? "llmNew" : "suggested"
                    text: modelData.name
                    toolTipText: modelData.reason
                    onClicked: root.model.acceptSuggestion(index)
                    onRemoveRequested: root.model.dismissSuggestion(index)
                }
            }
            TLabel {
                Layout.fillWidth: true
                text: root.model ? root.model.suggestionReason : ""
                muted: true
                font.pixelSize: Theme.fontCaption
                elide: Text.ElideRight
            }
        }
    }

    // Saját mikrofon (elhagyható).
    ColumnLayout {
        visible: !root.running && root.model && root.model.trackCount > 0 && !root.model.probing
        Layout.fillWidth: true
        spacing: 6

        TCheckBox {
            objectName: "importOwnSingle"
            visible: root.model && root.model.trackCount === 1
            Layout.fillWidth: true
            text: qsTr("Ez az én mikrofonom felvétele")
            checked: root.model ? root.model.ownTrack === 0 : false
            onToggled: {
                root.model.ownTrack = checked ? 0 : -1
                checked = Qt.binding(() => root.model ? root.model.ownTrack === 0 : false)
            }
        }
        TLabel {
            visible: root.model && root.model.trackCount > 1
            text: qsTr("Melyik sáv a te mikrofonod?")
            font.pixelSize: Theme.fontSmall
            font.weight: Theme.weightMedium
        }
        Flow {
            visible: root.model && root.model.trackCount > 1
            Layout.fillWidth: true
            spacing: 6
            TChip {
                objectName: "importOwnNone"
                text: qsTr("Egyik sem")
                checkable: true
                checked: root.model ? root.model.ownTrack < 0 : true
                onClicked: {
                    root.model.ownTrack = -1
                    checked = Qt.binding(() => root.model ? root.model.ownTrack < 0 : true)
                }
            }
            Repeater {
                model: root.model && root.model.trackCount > 1 ? root.model.trackNames : []
                TChip {
                    required property string modelData
                    required property int index
                    objectName: "importOwn" + index
                    text: modelData
                    iconName: checked ? "mic" : ""
                    checkable: true
                    checked: root.model ? root.model.ownTrack === index : false
                    onClicked: {
                        root.model.ownTrack = index
                        checked = Qt.binding(() => root.model ? root.model.ownTrack === index : false)
                    }
                }
            }
        }
        TLabel {
            Layout.fillWidth: true
            text: qsTr("Elhagyható. A megjelölt sáv a te neveden szerepel majd; a többi sávon a "
                       + "beszélőket az átírás választja szét.")
            muted: true
            font.pixelSize: Theme.fontCaption
            cssLineHeight: 1.45
            wrapMode: Text.Wrap
        }
    }

    // A végeredmény szóban.
    ColumnLayout {
        visible: !root.running && root.model && root.model.fileCount > 0
        Layout.fillWidth: true
        spacing: 2
        TDivider { Layout.fillWidth: true; Layout.bottomMargin: 8 }
        TLabel {
            objectName: "importSummary"
            Layout.fillWidth: true
            text: root.model ? root.model.trackSummary : ""
            font.weight: Theme.weightSemiBold
            wrapMode: Text.Wrap
        }
        TLabel {
            Layout.fillWidth: true
            text: qsTr("Az eredeti fájlok érintetlenek maradnak. Az átírást utána te indítod.")
            muted: true
            font.pixelSize: Theme.fontSmall
            cssLineHeight: 1.5
            wrapMode: Text.Wrap
        }
    }

    actions: [
        TButton {
            visible: !root.running
            text: qsTr("Mégse")
            font.weight: Theme.weightSemiBold
            onClicked: root.reject()
        },
        TButton {
            objectName: "importStart"
            visible: !root.running
            text: qsTr("Importálás")
            iconName: "import"
            variant: "primary"
            enabled: root.model ? root.model.canStart : false
            onClicked: root.model.start()
        },
        TButton {
            objectName: "importCancel"
            visible: root.running
            text: root.model && root.model.cancelling ? qsTr("Megszakítás…") : qsTr("Megszakítás")
            enabled: root.model ? !root.model.cancelling : false
            variant: "dangerGhost"
            font.weight: Theme.weightSemiBold
            onClicked: root.model.cancel()
        },
        TButton {
            objectName: "importBackground"
            visible: root.running
            text: qsTr("Háttérben folytatom")
            variant: "primary"
            onClicked: root.close()
        }
    ]

    // Elkészült → az ablak bezárul (a megbeszélést a ShellActions jelöli ki).
    Connections {
        target: root.model
        function onImported(meetingId) { root.close() }
    }
}
