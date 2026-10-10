import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// „Ki volt ott?" (handoff-v3 V2): a hangelemzés résztvevő-jelöltjeinek jóváhagyása. Fátyol +
// 820 px-es ablak; cím + mono meta (modellek · kor), segítő szöveg; csoportok (Biztos / Kétséges /
// Meghívott, de nem hallottuk / a párbeszédben hozzáadottak), az üres csoport rejtve; hozzáadás-
// mező (PersonPicker) + címke-alapú javaslat; lábléc: info a kötetlen nyers beszélőkről,
// „Kihagyás" (skipApproval), „Tovább · N résztvevő" (approveParticipants). Elemzés közben
// forgó jel; a bezárás (Esc) döntés nélkül hagyja („később").
//
// A ShellDialogs mintájára a Main.qml hostolja:  ParticipantsDialog { shell: shellActions }
// A shell.openParticipants(id) / maybeOfferParticipants(id) jelére magától nyílik.
// Képernyőkép: demoState = allSure | doubts | invitedNotHeard | addPerson | running | noTranscriptYet
Item {
    id: root

    property var shell: null                 // ShellActions
    property string demoState: ""
    property alias viewModel: vm
    readonly property bool opened: dialog.visible

    function openFor(meetingId) {
        vm.meetingId = meetingId
        vm.reload()
        dialog.open()
    }

    Connections {
        target: root.shell
        ignoreUnknownSignals: true
        function onParticipantsDialogRequested(meetingId) { root.openFor(meetingId) }
    }

    ParticipantsViewModel {
        id: vm
        demoState: root.demoState
    }

    Component.onCompleted: {
        if (root.demoState === "") return
        dialog.open()
        if (root.demoState === "addPerson")
            Qt.callLater(() => { addPicker.initialQuery = "Na"; addPicker.open() })
    }

    function approve() {
        const n = vm.checkedCount
        if (!vm.approve()) return
        dialog.close()
        if (root.shell)
            root.shell.toast(qsTr("Résztvevők jóváhagyva: %n.", "", n))
    }
    function skip() {
        if (vm.skip()) dialog.close()
    }

    // A sor elnevezése (ismeretlen hang).
    PersonPicker {
        id: namePicker
        objectName: "participantsNamePicker"
        property string rowId: ""
        participants: vm
        anonymousText: ""
        hintText: qsTr("A bejelölt hang ennek a személynek a nevére kerül.")
        onPersonChosen: (name) => vm.nameRow(rowId, name)
    }

    T.Dialog {
        id: dialog
        objectName: "participantsDialog"
        parent: T.Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(820, (parent ? parent.width : 820) - 32)
        height: Math.min(implicitHeight, (parent ? parent.height : 820) - 32)
        implicitHeight: implicitContentHeight + topPadding + bottomPadding
        padding: 0
        modal: true
        focus: true
        closePolicy: T.Popup.CloseOnEscape
        popupType: T.Popup.Item

        background: TSurface { radius: Theme.radiusDialog }
        T.Overlay.modal: Rectangle { color: Theme.scrim }
        enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationFast; easing.type: Theme.easing } }
        exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationFast } }

        contentItem: ColumnLayout {
            spacing: 0

            // ---- cím + meta + segítő szöveg ----
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 22
                Layout.rightMargin: 22
                Layout.topMargin: 18
                Layout.bottomMargin: 12
                spacing: 5
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    TLabel {
                        text: qsTr("Ki volt ott?")
                        font.pixelSize: 18
                        font.weight: Theme.weightSemiBold
                    }
                    Item { Layout.fillWidth: true }
                    TLabel {
                        objectName: "participantsMeta"
                        Layout.alignment: Qt.AlignVCenter
                        mono: true
                        muted: true
                        font.pixelSize: Theme.fontMicro + 1
                        text: vm.running ? qsTr("hanglenyomat-elemzés · fut") : vm.metaText
                    }
                }
                TLabel {
                    Layout.fillWidth: true
                    muted: true
                    wrapMode: Text.Wrap
                    cssLineHeight: 1.45
                    text: vm.hasTranscript
                          ? qsTr("Vedd ki, akit tévesen talált, és add hozzá, akit nem ismert fel. A bejelöltek "
                                 + "nevére kerülnek a beszélőik; később is módosíthatod.")
                          : qsTr("Vedd ki, akit tévesen talált, és add hozzá, akit nem ismert fel. Az átirat "
                                 + "ebből köti a beszélőket, amikor megérkezik; később is módosíthatod.")
                }
            }

            // ---- csoportok (görgethető, ha nem fér ki) ----
            Flickable {
                id: flick
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredHeight: body.implicitHeight
                Layout.minimumHeight: Math.min(body.implicitHeight, 120)
                contentHeight: body.implicitHeight
                clip: contentHeight > height
                interactive: contentHeight > height
                boundsBehavior: Flickable.StopAtBounds
                T.ScrollBar.vertical: TScrollBar {}

                Column {
                    id: body
                    x: 22
                    width: flick.width - 44
                    spacing: 6
                    bottomPadding: 12

                    // Elemzés közben: forgó jel + mit várunk.
                    Rectangle {
                        objectName: "participantsRunning"
                        visible: vm.running
                        width: parent.width
                        height: visible ? 58 : 0
                        radius: Theme.radiusPopup
                        color: Theme.surface
                        border.color: Theme.border
                        Row {
                            x: 14
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 12
                            TSpinner { anchors.verticalCenter: parent.verticalCenter; size: 18; running: vm.running }
                            Column {
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 2
                                TLabel {
                                    text: qsTr("Hanglenyomat-elemzés fut…")
                                    font.weight: Theme.weightSemiBold
                                }
                                TLabel {
                                    muted: true
                                    font.pixelSize: Theme.fontCaption
                                    text: qsTr("A jelöltek pár másodperc múlva itt lesznek. Addig is felvehetsz résztvevőt kézzel.")
                                }
                            }
                        }
                    }

                    // Nincs jelölt (és nem is fut elemzés).
                    Item {
                        visible: !vm.running && vm.rowCount === 0
                        width: parent.width
                        height: visible ? 58 : 0
                        TDashedRect { anchors.fill: parent; radius: Theme.radiusPopup }
                        TLabel {
                            x: 14
                            width: parent.width - 28
                            anchors.verticalCenter: parent.verticalCenter
                            muted: true
                            wrapMode: Text.Wrap
                            text: qsTr("Még nincs jelölt: a hangelemzés senkit sem talált. Vedd fel kézzel, akik ott voltak.")
                        }
                    }

                    Repeater {
                        model: vm.groups
                        Column {
                            id: group
                            required property var modelData
                            objectName: "participantGroup_" + modelData.key
                            width: body.width
                            spacing: 5
                            topPadding: 6

                            RowLayout {
                                width: parent.width
                                spacing: 8
                                TLabel {
                                    muted: true
                                    font.pixelSize: Theme.fontMicro
                                    font.weight: Theme.weightSemiBold
                                    font.letterSpacing: Theme.labelSpacing
                                    text: group.modelData.label
                                }
                                TLabel {
                                    muted: true
                                    font.pixelSize: Theme.fontCaption
                                    text: group.modelData.hint
                                }
                                Item { Layout.fillWidth: true }
                                TLabel {
                                    mono: true
                                    muted: true
                                    font.pixelSize: Theme.fontCaption
                                    font.weight: Theme.weightMedium
                                    text: group.modelData.count
                                }
                            }

                            Rectangle {
                                width: parent.width
                                height: rows.implicitHeight + 2
                                radius: Theme.radiusPopup
                                color: Theme.surface
                                border.color: Theme.border
                                Column {
                                    id: rows
                                    x: 1; y: 1
                                    width: parent.width - 2
                                    Repeater {
                                        model: group.modelData.rows
                                        ParticipantRow {
                                            required property var modelData
                                            required property int index
                                            width: rows.width
                                            row: modelData
                                            showTopBorder: index > 0
                                            onToggled: (on) => vm.setChecked(modelData.id, on)
                                            onRemoveRequested: vm.removeAdded(modelData.id)
                                            onNameRequested: (anchor) => {
                                                namePicker.rowId = modelData.id
                                                namePicker.parent = anchor
                                                namePicker.y = anchor.height + 6
                                                namePicker.open()
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // ---- hozzáadás + címke-alapú javaslat ----
                    Item { width: 1; height: 2 }
                    Row {
                        spacing: 8
                        Rectangle {
                            id: addField
                            objectName: "participantsAdd"
                            width: 230
                            height: 30
                            radius: Theme.radiusControl
                            color: Theme.sunken
                            border.color: addHover.hovered || addPicker.visible ? Theme.borderStrong : Theme.border
                            Row {
                                x: 10
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 7
                                TIcon { anchors.verticalCenter: parent.verticalCenter; name: "plus"; size: 13; color: Theme.textMuted }
                                TLabel {
                                    anchors.verticalCenter: parent.verticalCenter
                                    muted: true
                                    font.pixelSize: Theme.fontSmall
                                    text: qsTr("Résztvevő hozzáadása…")
                                }
                            }
                            HoverHandler { id: addHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: { addPicker.initialQuery = ""; addPicker.open() } }

                            PersonPicker {
                                id: addPicker
                                objectName: "participantsAddPicker"
                                // Ha alatta nem fér el (a párbeszéd alja), a mező fölé nyílik.
                                y: addField.mapToItem(root, 0, addField.height).y + implicitHeight + 14 > root.height
                                   ? -implicitHeight - 6 : addField.height + 6
                                participants: vm
                                anonymousText: ""
                                hintText: qsTr("A felvett résztvevő a „Tovább”-bal kerül a megbeszélésre.")
                                onPersonChosen: (name) => vm.addPerson(name)
                            }
                        }
                        TLabel {
                            visible: vm.tagSuggestions.length > 0
                            anchors.verticalCenter: parent.verticalCenter
                            leftPadding: 6
                            muted: true
                            font.pixelSize: Theme.fontCaption
                            text: vm.tagSuggestionLabel
                        }
                        Repeater {
                            model: vm.tagSuggestions
                            Row {
                                required property var modelData
                                required property int index
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 8
                                T.AbstractButton {
                                    id: sugg
                                    objectName: "participantsTagSuggestion_" + index
                                    anchors.verticalCenter: parent.verticalCenter
                                    implicitHeight: 24
                                    implicitWidth: suggRow.implicitWidth + 3 + 8
                                    hoverEnabled: true
                                    Accessible.name: qsTr("Javasolt résztvevő: %1").arg(modelData.name)
                                    onClicked: vm.acceptTagSuggestion(index)
                                    background: Item {
                                        Rectangle {
                                            anchors.fill: parent
                                            radius: 12
                                            color: Theme.accentSoft
                                            visible: sugg.hovered
                                        }
                                        TDashedRect { anchors.fill: parent; radius: 12; color: Theme.accent }
                                    }
                                    contentItem: Item {
                                        Row {
                                            id: suggRow
                                            x: 3
                                            anchors.verticalCenter: parent.verticalCenter
                                            spacing: 5
                                            TDashedRect {
                                                width: 16; height: 16; radius: 8
                                                anchors.verticalCenter: parent.verticalCenter
                                                color: Theme.accent
                                            }
                                            TLabel {
                                                anchors.verticalCenter: parent.verticalCenter
                                                color: Theme.accent
                                                font.pixelSize: Theme.fontSmall
                                                font.weight: Theme.weightMedium
                                                text: "+ " + modelData.name
                                            }
                                        }
                                    }
                                }
                                TLabel {
                                    anchors.verticalCenter: parent.verticalCenter
                                    muted: true
                                    font.pixelSize: Theme.fontMicro + 1
                                    text: modelData.countText
                                }
                            }
                        }
                    }
                }
            }

            // ---- lábléc ----
            TDivider { Layout.fillWidth: true }
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 22
                Layout.rightMargin: 22
                Layout.topMargin: 12
                Layout.bottomMargin: 16
                spacing: 10
                TIcon {
                    visible: info.text !== ""
                    name: "info"
                    size: 15
                    color: Theme.textMuted
                }
                TLabel {
                    id: info
                    objectName: "participantsInfo"
                    Layout.fillWidth: true
                    muted: true
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontCaption
                    text: vm.infoText
                }
                TButton {
                    objectName: "participantsSkip"
                    variant: "ghost"
                    muted: true
                    text: vm.running ? qsTr("Később") : qsTr("Kihagyás")
                    toolTipText: vm.running ? "" : qsTr("Nem kötünk senkit; a jelöltek megmaradnak, később is jóváhagyhatod.")
                    onClicked: vm.running ? dialog.close() : root.skip()
                }
                TButton {
                    objectName: "participantsApprove"
                    variant: "primary"
                    enabled: !vm.running
                    text: qsTr("Tovább · %n résztvevő", "", vm.checkedCount)
                    onClicked: root.approve()
                }
            }
        }
    }
}
