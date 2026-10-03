import QtQuick
import QtQuick.Layouts

// A Sávok fül egy sora (M09): lejátszás-gomb, forrás-ikon, barátságos név (helyben
// átnevezhető) + alatta a nyers eszköznév, hullámforma, hossz, és az állapot szerinti művelet
// (eldobott → „Visszaállítás”, hiányzó fájl → „Megkeresés…”).
Item {
    id: root

    property string displayName: ""
    property string friendlyName: ""
    property bool renamed: false
    property string rawName: ""
    property string iconName: "audio-lines"
    property bool active: true
    property bool missing: false
    property string durationText: ""
    property var peaks: []
    property string peaksState: "none"        // none | loading | ready | failed
    property real peakReference: 0
    property int colorIndex: 0
    property bool playing: false              // ez a sáv szól épp előnézetként
    property bool canPlay: true               // van lejátszó és megvan a fájl
    property bool first: false
    property real nameWidth: 300

    signal playRequested()
    signal renameRequested(string name)
    signal restoreRequested()
    signal locateRequested()

    readonly property bool dropped: !active
    property bool editing: false

    function beginRename() {
        nameField.text = root.displayName
        root.editing = true
        nameField.forceActiveFocus()
        nameField.selectAll()
    }
    function commitRename() {
        if (!root.editing) return
        root.editing = false
        if (nameField.text.trim() !== root.displayName)
            root.renameRequested(nameField.text.trim())
    }

    implicitHeight: 60

    TDivider {
        visible: !root.first
        anchors { left: parent.left; right: parent.right; top: parent.top }
    }
    HoverHandler { id: hover }

    RowLayout {
        anchors { fill: parent; leftMargin: 14; rightMargin: 14 }
        spacing: 14
        // Az eldobott sáv halványabb, de a művelete (Visszaállítás) teljes erővel látszik.
        TIconButton {
            iconName: root.playing ? "pause" : "play"
            iconSize: 13
            radius: 16
            implicitWidth: 32; implicitHeight: 32
            enabled: root.canPlay
            opacity: root.dropped ? 0.75 : 1
            toolTipText: root.missing ? qsTr("A hangfájl hiányzik")
                       : root.playing ? qsTr("Előnézet leállítása") : qsTr("Sáv meghallgatása")
            onClicked: root.playRequested()
        }
        TIcon {
            name: root.iconName
            size: 16
            color: Theme.textMuted
            opacity: root.dropped ? 0.75 : 1
        }

        // ---- név + nyers eszköznév ----
        ColumnLayout {
            Layout.preferredWidth: root.nameWidth
            Layout.minimumWidth: root.nameWidth
            Layout.maximumWidth: root.nameWidth
            spacing: 3
            opacity: root.dropped ? 0.75 : 1

            RowLayout {
                visible: !root.editing
                Layout.fillWidth: true
                spacing: 8
                TLabel {
                    id: nameLabel
                    Layout.maximumWidth: root.nameWidth - (pill.visible ? pill.implicitWidth + 8 : 0)
                                         - (renameButton.visible ? 30 : 0)
                    text: root.displayName
                    font.weight: Theme.weightSemiBold
                    elide: Text.ElideRight
                    TapHandler { onDoubleTapped: root.beginRename() }
                }
                TPill {
                    id: pill
                    visible: root.missing || root.dropped
                    text: root.missing ? qsTr("hiányzik a fájl") : qsTr("eldobott · csendes")
                    tone: root.missing ? "danger" : "neutral"
                }
                TIconButton {
                    id: renameButton
                    visible: hover.hovered || activeFocus
                    iconName: "pencil"
                    iconSize: 13
                    variant: "flat"
                    implicitWidth: 22; implicitHeight: 22
                    iconColor: Theme.textMuted
                    toolTipText: qsTr("Sáv átnevezése")
                    onClicked: root.beginRename()
                }
                Item { Layout.fillWidth: true }
            }
            TTextField {
                id: nameField
                visible: root.editing
                Layout.fillWidth: true
                implicitHeight: 26
                leftPadding: 6; rightPadding: 6
                font.weight: Theme.weightSemiBold
                placeholderText: root.friendlyName
                Accessible.name: qsTr("A sáv neve")
                Keys.onReturnPressed: root.commitRename()
                Keys.onEnterPressed: root.commitRename()
                Keys.onEscapePressed: root.editing = false
                onActiveFocusChanged: if (!activeFocus) root.commitRename()
            }
            TLabel {
                Layout.fillWidth: true
                text: root.editing ? qsTr("Enter: mentés · Esc: mégse · üresen: „%1”").arg(root.friendlyName)
                                   : root.rawName
                mono: !root.editing
                muted: true
                font.pixelSize: Theme.fontMicro
                elide: Text.ElideRight
            }
        }

        // ---- hullámforma ----
        WaveformItem {
            id: wave
            Layout.fillWidth: true
            implicitHeight: 30
            peaks: root.peaks
            reference: root.peakReference
            color: loading ? Theme.border
                 : root.dropped || root.missing ? Theme.borderStrong : Theme.speakerLine(root.colorIndex)
            flat: root.missing || (root.dropped && root.peaksState !== "ready")
            loading: root.peaksState === "loading"
            visible: !root.missing
            // Számolás közben lüktet (a képernyőképen állókép).
            SequentialAnimation on opacity {
                running: wave.loading && wave.visible
                loops: Animation.Infinite
                NumberAnimation { from: 1; to: 0.45; duration: 700; easing.type: Easing.InOutSine }
                NumberAnimation { from: 0.45; to: 1; duration: 700; easing.type: Easing.InOutSine }
                onRunningChanged: if (!running) wave.opacity = 1
            }
        }
        Item { visible: root.missing; Layout.fillWidth: true }

        TLabel {
            Layout.preferredWidth: 52
            text: root.durationText
            mono: true; muted: true
            font.pixelSize: Theme.fontCaption
            horizontalAlignment: Text.AlignRight
            opacity: root.dropped ? 0.75 : 1
        }
        Item {
            Layout.preferredWidth: 120
            implicitHeight: 28
            TButton {
                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                visible: root.missing || root.dropped
                text: root.missing ? qsTr("Megkeresés…") : qsTr("Visszaállítás")
                size: "small"
                onClicked: root.missing ? root.locateRequested() : root.restoreRequested()
            }
        }
    }
}
