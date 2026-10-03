import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// B03 — Hívásfigyelő: fő kapcsoló az élő „Most: … használja a mikrofont” sorral, indítás
// bejelentkezéskor, rákérdezés a megbeszélés végén (csend-léptetővel), a figyelt
// alkalmazások chipjei (a most észlelt kiemelve), új alkalmazás felvétele, haladó beállítás.
Column {
    id: root

    property var vm: null
    property bool advancedOpen: false
    property bool demoAddOpen: false       // képernyőképhez: a hozzáadás-panel nyitva

    spacing: 20

    Component.onCompleted: if (demoAddOpen) Qt.callLater(function() { addPopup.openFresh() })

    // ---- fő kapcsoló ----
    Rectangle {
        width: parent.width
        height: masterRow.implicitHeight + 28
        radius: Theme.radiusPopup
        color: Theme.surface
        border.width: 1
        border.color: Theme.border

        SettingsSwitchRow {
            id: masterRow
            x: 16; y: 14
            width: parent.width - 32
            large: true
            text: qsTr("Hívásfigyelés")
            helper: qsTr("A tálcán fut, és szól, ha egy hívásalkalmazás használni kezdi a mikrofont. A felvételt mindig te indítod.")
            checked: root.vm ? root.vm.detectorEnabled : false
            onToggled: (on) => root.vm.detectorEnabled = on

            Row {
                spacing: 8
                Rectangle {
                    width: 14; height: 14; radius: 7
                    anchors.verticalCenter: parent.verticalCenter
                    color: root.vm && root.vm.liveCallActive ? Theme.accentSoft : "transparent"
                    Rectangle {
                        anchors.centerIn: parent
                        width: 8; height: 8; radius: 4
                        color: root.vm && root.vm.liveCallActive ? Theme.accent : Theme.borderStrong
                    }
                }
                TLabel {
                    textFormat: Text.StyledText
                    font.pixelSize: Theme.fontSmall
                    color: root.vm && root.vm.liveCallActive ? Theme.text : Theme.textMuted
                    text: !root.vm ? ""
                        : !root.vm.detectorAvailable ? qsTr("Most: az észlelés ezen a gépen nem érhető el")
                        : root.vm.liveCallActive
                            ? qsTr("Most: <b>%1</b> használja a mikrofont").arg(root.vm.liveCallApp)
                            : qsTr("Most: egyik figyelt alkalmazás sem használja a mikrofont")
                }
            }
        }
    }

    Column {
        width: parent.width
        spacing: 14
        SettingsSwitchRow {
            width: parent.width
            text: qsTr("Induljon el a bejelentkezéskor")
            checked: root.vm ? root.vm.watcherAutostart : false
            onToggled: (on) => root.vm.watcherAutostart = on
        }
        SettingsSwitchRow {
            width: parent.width
            text: qsTr("Kérdezzen rá, ha véget ért a megbeszélés")
            helper: qsTr("Felvétel közben, ha a hívás véget ér vagy a sávokon csend van. Magától sosem állít le.")
            checked: root.vm ? root.vm.askStopOnCallEnd : false
            onToggled: (on) => root.vm.askStopOnCallEnd = on

            Row {
                spacing: 10
                TLabel {
                    text: qsTr("Csend után:")
                    font.pixelSize: Theme.fontSmall
                    anchors.verticalCenter: parent.verticalCenter
                }
                SettingsStepper {
                    width: 150
                    from: 0; to: 60
                    value: root.vm ? root.vm.silenceAskMinutes : 0
                    accessibleName: qsTr("Csend után ennyi perccel kérdezzen")
                    textFor: function(v) { return v === 0 ? qsTr("kikapcsolva") : qsTr("%n perc", "", v) }
                    onMoved: (v) => root.vm.silenceAskMinutes = v
                }
            }
        }
    }

    Rectangle { width: parent.width; height: 1; color: Theme.border }

    // ---- figyelt alkalmazások ----
    Column {
        width: parent.width
        spacing: 10
        topPadding: -2
        TSectionLabel { text: qsTr("Figyelt alkalmazások") }
        TLabel {
            width: parent.width
            text: qsTr("Ezekre jelez, ha mikrofont használnak. Folyamatnév vagy annak egy része.")
            muted: true
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.Wrap
        }
        Flow {
            width: parent.width
            spacing: 6
            Repeater {
                model: root.vm ? root.vm.watchedApps : []
                Rectangle {
                    id: chip
                    required property var modelData
                    required property int index
                    width: chipRow.implicitWidth + 16
                    height: 30
                    radius: 15
                    color: modelData.active ? Theme.accentSoft : Theme.raised
                    border.width: 1
                    border.color: modelData.active ? Theme.accentLine : Theme.border
                    Row {
                        id: chipRow
                        x: 10
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8
                        TLabel {
                            text: chip.modelData.label
                            font.pixelSize: Theme.fontSmall
                            font.weight: Theme.weightMedium
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        TLabel {
                            text: chip.modelData.match
                            mono: true; muted: true
                            font.pixelSize: 11
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        T.AbstractButton {
                            id: removeBtn
                            width: 16; height: 16
                            anchors.verticalCenter: parent.verticalCenter
                            hoverEnabled: true
                            activeFocusOnTab: true
                            Accessible.name: qsTr("%1 eltávolítása").arg(chip.modelData.label)
                            onClicked: root.vm.removeWatchedApp(chip.index)
                            Keys.onReturnPressed: click()
                            background: Rectangle {
                                radius: 8
                                color: Theme.stateLayer
                                opacity: removeBtn.hovered || removeBtn.visualFocus ? 0.12 : 0
                            }
                            contentItem: Item {
                                TIcon {
                                    anchors.centerIn: parent
                                    name: "x"
                                    size: 13
                                    color: removeBtn.hovered ? Theme.text : Theme.textMuted
                                }
                            }
                        }
                    }
                    TToolTip {
                        visible: chipHover.hovered && chip.modelData.active
                        text: qsTr("Most ez az alkalmazás használja a mikrofont")
                    }
                    HoverHandler { id: chipHover }
                }
            }
            T.AbstractButton {
                id: addChip
                width: addRow.implicitWidth + 20
                height: 30
                hoverEnabled: true
                activeFocusOnTab: true
                Accessible.name: qsTr("Alkalmazás hozzáadása")
                onClicked: addPopup.opened ? addPopup.close() : addPopup.openFresh()
                Keys.onReturnPressed: click()
                background: Item {
                    TDashedRect { anchors.fill: parent; radius: 15 }
                    Rectangle {
                        anchors.fill: parent
                        radius: 15
                        color: Theme.stateLayer
                        opacity: addChip.down ? Theme.pressedOpacity : addChip.hovered ? Theme.hoverOpacity : 0
                    }
                    TFocusRing { visible: addChip.visualFocus; targetRadius: 15 }
                }
                contentItem: Item {
                    Row {
                        id: addRow
                        anchors.centerIn: parent
                        spacing: 6
                        TIcon { name: "plus"; size: 13; color: Theme.textMuted; anchors.verticalCenter: parent.verticalCenter }
                        TLabel {
                            text: qsTr("Alkalmazás hozzáadása")
                            muted: true
                            font.pixelSize: Theme.fontSmall
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }

                TPopover {
                    id: addPopup
                    y: addChip.height + 6
                    width: 320
                    property var running: []
                    property string errorText: ""

                    function openFresh() {
                        running = root.vm.runningApps()
                        errorText = ""
                        addField.text = ""
                        open()
                        addField.forceActiveFocus()
                    }
                    function add(match) {
                        const err = root.vm.addWatchedApp(match)
                        if (err === "") close()
                        else errorText = err
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 8
                        TLabel {
                            text: qsTr("Folyamatnév vagy annak egy része")
                            font.pixelSize: Theme.fontSmall
                            font.weight: Theme.weightSemiBold
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            TTextField {
                                id: addField
                                Layout.fillWidth: true
                                implicitHeight: 32
                                mono: true
                                placeholderText: qsTr("pl. signal")
                                hasError: addPopup.errorText !== ""
                                onAccepted: addPopup.add(text)
                                onTextEdited: addPopup.errorText = ""
                            }
                            TButton {
                                text: qsTr("Hozzáadás")
                                size: "small"
                                variant: "primary"
                                implicitHeight: 32
                                enabled: addField.text.trim() !== ""
                                onClicked: addPopup.add(addField.text)
                            }
                        }
                        TLabel {
                            visible: addPopup.errorText !== ""
                            Layout.fillWidth: true
                            text: addPopup.errorText
                            color: Theme.dangerInk
                            font.pixelSize: Theme.fontCaption
                            wrapMode: Text.Wrap
                        }
                        TLabel {
                            visible: addPopup.running.length > 0
                            text: qsTr("Most futó alkalmazások")
                            muted: true
                            font.pixelSize: 11
                            font.weight: Theme.weightSemiBold
                            font.letterSpacing: 0.66
                            font.capitalization: Font.AllUppercase
                            topPadding: 2
                        }
                        ListView {
                            id: runList
                            visible: addPopup.running.length > 0
                            Layout.fillWidth: true
                            Layout.preferredHeight: Math.min(contentHeight, 5 * 30)
                            clip: true
                            model: addField.text.trim() === "" ? addPopup.running
                                 : addPopup.running.filter(a => a.match.indexOf(addField.text.trim().toLowerCase()) >= 0)
                            boundsBehavior: Flickable.StopAtBounds
                            T.ScrollBar.vertical: TScrollBar {}
                            delegate: T.ItemDelegate {
                                id: runItem
                                required property var modelData
                                width: runList.width
                                height: 30
                                hoverEnabled: true
                                Accessible.name: modelData.label
                                onClicked: addPopup.add(modelData.match)
                                background: Rectangle {
                                    radius: 5
                                    color: runItem.hovered ? Theme.sunken : "transparent"
                                }
                                contentItem: Item {
                                    TLabel {
                                        x: 8
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: runItem.modelData.label
                                        font.pixelSize: Theme.fontSmall
                                    }
                                    TLabel {
                                        anchors.right: parent.right
                                        anchors.rightMargin: 12
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: runItem.modelData.match
                                        mono: true; muted: true
                                        font.pixelSize: 11
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        TLabel {
            visible: root.vm && root.vm.errors.watchedApps !== undefined
            width: parent.width
            text: root.vm && root.vm.errors.watchedApps !== undefined ? root.vm.errors.watchedApps : ""
            color: Theme.dangerInk
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.Wrap
        }
    }

    Rectangle { width: parent.width; height: 1; color: Theme.border }

    // ---- haladó ----
    Column {
        width: parent.width
        spacing: 12
        topPadding: -6
        T.AbstractButton {
            id: advToggle
            width: advRow.implicitWidth + 8
            height: 28
            hoverEnabled: true
            activeFocusOnTab: true
            Accessible.name: qsTr("Haladó")
            onClicked: root.advancedOpen = !root.advancedOpen
            Keys.onReturnPressed: click()
            Keys.onSpacePressed: click()
            background: Item { TFocusRing { visible: advToggle.visualFocus } }
            contentItem: Item {
                Row {
                    id: advRow
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8
                    TIcon {
                        name: root.advancedOpen ? "chevron-down" : "chevron-right"
                        size: 14
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    TLabel {
                        text: qsTr("Haladó")
                        font.weight: Theme.weightMedium
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    TLabel {
                        text: qsTr("ellenőrzés %n másodpercenként", "", root.vm ? root.vm.detectorIntervalSec : 8)
                        muted: true
                        font.pixelSize: Theme.fontSmall
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }
        }
        Row {
            visible: root.advancedOpen
            x: 22
            spacing: 10
            TLabel {
                text: qsTr("Ellenőrzés gyakorisága:")
                font.pixelSize: Theme.fontSmall
                anchors.verticalCenter: parent.verticalCenter
            }
            SettingsStepper {
                width: 150
                from: 3; to: 60
                value: root.vm ? root.vm.detectorIntervalSec : 8
                accessibleName: qsTr("Ellenőrzés gyakorisága másodpercben")
                textFor: function(v) { return qsTr("%n mp", "", v) }
                onMoved: (v) => root.vm.detectorIntervalSec = v
            }
        }
        TLabel {
            visible: root.advancedOpen
            x: 22
            width: parent.width - 22
            text: qsTr("Rövidebb idő: hamarabb szól, de többször néz körül.")
            muted: true
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.Wrap
        }
    }
}
