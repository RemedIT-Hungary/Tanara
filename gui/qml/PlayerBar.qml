import QtQuick
import QtQuick.Layouts

// Lejátszó-sáv (52 px): kerek lejátszás / szünet, idő („00:17 / 30:34”, mono), mi szól
// („lekevert hang”), pozíció-csúszka (húzható), sebesség („1×”) és hangerő. A PlayerControllert
// `player`-ként kapja; nélküle (önálló képernyőkép) álló mintaállapot látszik.
// A térkép-dokkban (MapDock) csúszka nélkül, egy rövid tippel áll: ott a térkép a keresősáv.
Item {
    id: root

    property var player: null                // PlayerController vagy null
    property string label: qsTr("lekevert hang")
    property bool showSlider: true
    property string hint: ""

    readonly property bool available: player ? player.available : true
    readonly property bool playing: player ? player.playing : false
    readonly property int positionMs: player ? player.positionMs : 0
    readonly property int durationMs: player ? player.durationMs : 1834000
    // A térkép-dokk tesztjeiben egyszerű ál-lejátszó is jöhet: a hiányzó mezőnél az alapérték.
    readonly property real rate: player && player.rate !== undefined ? player.rate : 1.0
    readonly property real volume: player && player.volume !== undefined ? player.volume : 1.0
    readonly property var rates: [0.75, 1.0, 1.25, 1.5, 2.0]

    function clock(ms) {
        const total = Math.max(0, Math.floor(ms / 1000))
        const h = Math.floor(total / 3600), m = Math.floor((total % 3600) / 60), s = total % 60
        const two = (n) => (n < 10 ? "0" : "") + n
        return h > 0 ? h + ":" + two(m) + ":" + two(s) : two(m) + ":" + two(s)
    }
    function rateLabel(r) {
        return String(Math.round(r * 100) / 100).replace(".", Qt.locale().decimalPoint) + "×"
    }

    RowLayout {
        anchors { fill: parent; leftMargin: Theme.space5; rightMargin: Theme.space5 }
        spacing: 14

        TIconButton {
            objectName: "playButton"
            implicitWidth: 32; implicitHeight: 32
            variant: "solid"
            radius: 16
            iconName: root.playing ? "pause" : "play"
            iconSize: 15
            enabled: root.available
            toolTipText: !root.available ? qsTr("Ehhez a megbeszéléshez nincs lejátszható hang")
                       : root.playing ? qsTr("Szünet (Szóköz)") : qsTr("Lejátszás (Szóköz)")
            onClicked: if (root.player) root.player.toggle()
        }
        TLabel {
            text: root.clock(seek.dragging ? seek.dragValue * root.durationMs : root.positionMs)
                  + " / " + root.clock(root.durationMs)
            mono: true
            muted: true
            font.pixelSize: Theme.fontCaption
        }
        TLabel {
            visible: text !== ""
            text: root.player && root.player.previewPath ? qsTr("sáv-előnézet") : root.showSlider ? root.label : root.hint
            Layout.fillWidth: !root.showSlider
            elide: Text.ElideRight
            muted: true
            font.pixelSize: Theme.fontCaption
        }
        ShellSlider {
            id: seek
            objectName: "seekSlider"
            visible: root.showSlider
            Layout.fillWidth: true
            enabled: root.available && root.durationMs > 0
            value: root.durationMs > 0 ? root.positionMs / root.durationMs : 0
            keyStep: root.durationMs > 0 ? 5000 / root.durationMs : 0.01
            accessibleName: qsTr("Pozíció")
            onMoved: (v) => { if (root.player) root.player.seek(Math.round(v * root.durationMs)) }
        }
        TButton {
            id: rateButton
            implicitHeight: 22
            leftPadding: 6; rightPadding: 6
            size: "small"
            radius: 5
            text: root.rateLabel(root.rate)
            font.family: Theme.fontMono
            font.pixelSize: Theme.fontMicro
            font.weight: Theme.weightSemiBold
            toolTipText: qsTr("Lejátszási sebesség")
            down: pressed || rateMenu.visible
            onClicked: rateMenu.popup(rateButton, rateButton.width - rateMenu.width, -rateMenu.height - 4)

            TMenu {
                id: rateMenu
                Repeater {
                    model: root.rates
                    TMenuItem {
                        required property real modelData
                        text: root.rateLabel(modelData)
                        checked: Math.abs(root.rate - modelData) < 0.01
                        onTriggered: if (root.player) root.player.rate = modelData
                    }
                }
            }
        }
        TIconButton {
            id: volumeButton
            variant: "flat"
            size: "small"
            iconName: root.volume <= 0.001 ? "volume-x" : "volume-2"
            iconColor: Theme.textMuted
            toolTipText: qsTr("Hangerő")
            down: pressed || volumePopup.visible
            onClicked: volumePopup.visible ? volumePopup.close() : volumePopup.open()

            TPopover {
                id: volumePopup
                x: volumeButton.width - width
                y: -height - 6
                width: 196
                padding: 10
                RowLayout {
                    anchors.fill: parent
                    spacing: 10
                    TIconButton {
                        variant: "flat"
                        size: "small"
                        property real lastVolume: 1.0
                        iconName: root.volume <= 0.001 ? "volume-x" : "volume-2"
                        toolTipText: root.volume <= 0.001 ? qsTr("Némítás feloldása") : qsTr("Némítás")
                        onClicked: {
                            if (!root.player) return
                            if (root.player.volume > 0.001) {
                                lastVolume = root.player.volume
                                root.player.volume = 0
                            } else {
                                root.player.volume = lastVolume
                            }
                        }
                    }
                    ShellSlider {
                        Layout.fillWidth: true
                        knobAlwaysVisible: true
                        value: root.volume
                        accessibleName: qsTr("Hangerő")
                        onMoved: (v) => { if (root.player) root.player.volume = v }
                    }
                }
            }
        }
    }
}
