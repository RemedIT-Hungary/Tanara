import QtQuick
import QtQuick.Layouts

// B02 — Rögzítés: alapértelmezett források (a felvevő eszközlistája élő szintmérővel és
// átnevezéssel), „minden eszköz” kapcsoló, hangminőség, lekeverés.
Column {
    id: root

    property var vm: null
    // Melyik sor nevét szerkeszti épp a felhasználó (-1 = egyiket sem).
    property int editingRow: -1

    spacing: 20

    // Az eszközlista újraépült (csatlakoztatás / leválasztás): a sorszám már mást jelenthet.
    Connections {
        target: root.vm ? root.vm.devices : null
        function onModelReset() { root.editingRow = -1 }
    }

    function startRename(row) { root.editingRow = row }
    function finishRename(row, name, commit) {
        if (root.editingRow !== row) return
        root.editingRow = -1
        if (commit) root.vm.renameDevice(row, name)
    }

    Column {
        width: parent.width
        spacing: 10
        TSectionLabel { text: qsTr("Alapértelmezett források") }
        TLabel {
            width: parent.width
            text: qsTr("Ezek kapnak sávot, amikor megnyitod a felvevőt; felvételenként ott is módosíthatod. A nevükre kattintva átnevezheted őket.")
            muted: true
            font.pixelSize: Theme.fontSmall
            cssLineHeight: 1.45
            wrapMode: Text.Wrap
        }

        Rectangle {
            width: parent.width
            height: devCol.implicitHeight + 12
            radius: Theme.radiusPopup
            color: Theme.surface
            border.width: 1
            border.color: Theme.border

            Column {
                id: devCol
                x: 12; y: 4
                width: parent.width - 24

                TLabel {
                    visible: root.vm && root.vm.deviceCount === 0
                    width: parent.width
                    topPadding: 12; bottomPadding: 8
                    text: qsTr("Nem találtam hangeszközt. Csatlakoztass mikrofont, vagy ellenőrizd a rendszer hangbeállításait.")
                    muted: true
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.Wrap
                }

                Repeater {
                    model: root.vm ? root.vm.devices : null
                    Column {
                        id: dev
                        required property int index
                        required property string name
                        required property string rawName
                        required property string defaultName
                        required property bool renamed
                        required property int group
                        required property bool groupFirst
                        required property bool selected
                        required property bool isDefault
                        required property int level
                        required property int peak
                        readonly property bool editing: root.editingRow === index
                        width: devCol.width

                        RecorderGroupHeader {
                            visible: dev.groupFirst
                            height: visible ? 28 : 0
                            topPadding: 10
                            group: dev.group
                        }
                        Item {
                            width: parent.width
                            height: 46

                            RecorderSwitch {
                                id: sw
                                anchors.verticalCenter: parent.verticalCenter
                                checked: dev.selected
                                toolTipText: dev.selected ? qsTr("Alapból sávot kap") : qsTr("Alapból nem kap sávot")
                                onToggled: {
                                    root.vm.toggleDevice(dev.index)
                                    checked = Qt.binding(function() { return dev.selected })
                                }
                            }
                            Column {
                                x: 30 + 12
                                width: parent.width - x - 12 - meter.width
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3

                                // ---- név + ceruza (kattintásra szerkeszthető) ----
                                Item {
                                    width: parent.width
                                    height: dev.editing ? 28 : nameLabel.implicitHeight
                                    visible: true

                                    Row {
                                        visible: !dev.editing
                                        spacing: 6
                                        TLabel {
                                            id: nameLabel
                                            width: Math.min(implicitWidth, dev.width - 30 - 12 - 12 - meter.width - 20)
                                            text: dev.name
                                            color: dev.selected ? Theme.text : Theme.textMuted
                                            font.pixelSize: 13
                                            font.weight: dev.selected ? Theme.weightSemiBold : Theme.weightRegular
                                            elide: Text.ElideRight
                                        }
                                        TIcon {
                                            name: "pencil"
                                            size: 12
                                            color: nameHover.hovered ? Theme.text : Theme.textMuted
                                            anchors.verticalCenter: parent.verticalCenter
                                        }
                                        HoverHandler { id: nameHover; cursorShape: Qt.PointingHandCursor }
                                        TapHandler { onTapped: root.startRename(dev.index) }
                                    }
                                    TToolTip {
                                        visible: nameHover.hovered && !dev.editing
                                        text: dev.renamed ? qsTr("Átnevezés (eredeti név: %1)").arg(dev.defaultName)
                                                          : qsTr("Átnevezés")
                                    }
                                    TTextField {
                                        id: nameEdit
                                        visible: dev.editing
                                        width: Math.min(parent.width, 260)
                                        implicitHeight: 28
                                        leftPadding: 8; rightPadding: 8
                                        font.pixelSize: 13
                                        font.weight: Theme.weightSemiBold
                                        placeholderText: dev.defaultName
                                        Accessible.name: qsTr("Az eszköz neve")
                                        // Az üres név visszaállítja a gyári (rövidített) nevet.
                                        onAccepted: root.finishRename(dev.index, text, true)
                                        Keys.onEscapePressed: root.finishRename(dev.index, "", false)
                                        onActiveFocusChanged: if (!activeFocus && dev.editing) root.finishRename(dev.index, text, true)
                                        onVisibleChanged: {
                                            if (visible) {
                                                text = dev.name
                                                forceActiveFocus()
                                                selectAll()
                                            }
                                        }
                                        Component.onCompleted: if (visible) { text = dev.name; cursorPosition = text.length }
                                    }
                                }
                                Row {
                                    id: metaRow
                                    width: parent.width
                                    spacing: 6
                                    RecorderDefaultPill {
                                        id: defPill
                                        visible: dev.isDefault
                                    }
                                    TLabel {
                                        id: rawLabel
                                        width: metaRow.width - (defPill.visible ? defPill.width + 6 : 0)
                                        text: dev.rawName
                                        mono: true; muted: true
                                        font.pixelSize: 11
                                        elide: Text.ElideRight
                                        anchors.verticalCenter: parent.verticalCenter
                                        HoverHandler { id: rawHover }
                                        TToolTip { visible: rawHover.hovered && rawLabel.truncated; text: dev.rawName }
                                    }
                                }
                            }
                            VuMeter {
                                id: meter
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                level: dev.level; peak: dev.peak
                                active: dev.selected
                            }
                        }
                    }
                }
            }
        }

        SettingsSwitchRow {
            width: parent.width
            text: qsTr("Minden eszköz rögzítése automatikusan")
            helper: qsTr("Ha nem tudod előre, honnan jön a hang. A csendes sávokat utólag eldobottnak jelöli, de megtartja.")
            checked: root.vm ? root.vm.autoRecordAll : false
            onToggled: (on) => root.vm.autoRecordAll = on
        }
    }

    Rectangle { width: parent.width; height: 1; color: Theme.border }

    GridLayout {
        width: parent.width
        columns: 2
        columnSpacing: 16
        rowSpacing: 14

        TLabel {
            Layout.preferredWidth: 140
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 6
            text: qsTr("Hangminőség")
            font.weight: Theme.weightSemiBold
        }
        Column {
            Layout.fillWidth: true
            spacing: 6
            SettingsSegmented {
                options: root.vm ? root.vm.audioQualityOptions : []
                value: root.vm ? root.vm.audioQuality : ""
                onPicked: (v) => root.vm.audioQuality = v
            }
            TLabel {
                width: parent.width
                text: root.vm ? root.vm.audioQualityHint : ""
                muted: true
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.Wrap
            }
        }

        TLabel {
            Layout.preferredWidth: 140
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 2
            text: qsTr("Lekeverés")
            font.weight: Theme.weightSemiBold
        }
        Column {
            Layout.fillWidth: true
            spacing: 8
            SettingsRadio {
                width: parent.width
                text: qsTr("Automatikusan, a felvétel után")
                checked: root.vm && root.vm.mixdownMode === "auto"
                onClicked: root.vm.mixdownMode = "auto"
            }
            SettingsRadio {
                width: parent.width
                text: qsTr("Kézzel, a Sávok fülön")
                checked: root.vm && root.vm.mixdownMode === "manual"
                onClicked: root.vm.mixdownMode = "manual"
            }
            TLabel {
                width: parent.width
                text: qsTr("A lekevert fájlból megy a visszahallgatás és az átírás. Ha még nincs meg, az átírás előtt magától elkészül.")
                muted: true
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.Wrap
            }
        }
    }
}
