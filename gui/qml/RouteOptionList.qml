import QtQuick
import QtQuick.Layouts

// The route choice of one processing step (S5): one row per route of the step's capability
// (OverviewViewModel step row's routeOptions: { route, label, detail, enabled, reason, isDefault,
// isCloud }; the stored default carries a check) and the "Ez legyen az alapértelmezett" checkbox. A row: chosen(route, makeDefault).
// Used by the split button's menu (RouteMenu) and the shell's route dialog.
// Keyboard: Up / Down move between the enabled rows, Enter / Space chooses.
ColumnLayout {
    id: root

    property var options: []
    property string title: qsTr("Hol fusson most?")
    property bool showDefaultBox: true
    property alias makeDefault: defaultBox.checked
    property int current: -1                 // the keyboard row (-1: none)

    signal chosen(string route, bool makeDefault)

    function firstEnabled(from, step) {
        const n = root.options.length
        for (let i = 0, k = from; i < n; ++i) {
            k = (k + step + n) % n
            if (root.options[k].enabled) return k
        }
        return -1
    }
    function focusFirst() {
        root.current = root.firstEnabled(-1, 1)
        rowsColumn.forceActiveFocus()
    }
    function choose(index) {
        const o = root.options[index]
        if (o && o.enabled) root.chosen(o.route, defaultBox.checked)
    }

    spacing: 2

    TLabel {
        Layout.leftMargin: 8
        Layout.topMargin: 2
        Layout.bottomMargin: 4
        visible: root.title !== ""
        text: root.title
        muted: true
        font.pixelSize: Theme.fontCaption
        font.weight: Theme.weightSemiBold
    }
    ColumnLayout {
        id: rowsColumn
        objectName: "routeRows"
        Layout.fillWidth: true
        spacing: 2
        focus: true
        Keys.onDownPressed: root.current = root.firstEnabled(root.current, 1)
        Keys.onUpPressed: root.current = root.firstEnabled(root.current < 0 ? 0 : root.current, -1)
        Keys.onReturnPressed: root.choose(root.current)
        Keys.onEnterPressed: root.choose(root.current)
        Keys.onSpacePressed: root.choose(root.current)

        Repeater {
            model: root.options
            Rectangle {
                id: row
                required property var modelData
                required property int index
                objectName: "routeOption_" + modelData.route
                readonly property bool on: modelData.enabled === true
                Layout.fillWidth: true
                implicitHeight: rowContent.implicitHeight + 14
                radius: 5
                color: row.on && (hover.hovered || root.current === row.index) ? Theme.sunken : "transparent"
                HoverHandler { id: hover; enabled: row.on; cursorShape: Qt.PointingHandCursor }
                TapHandler { enabled: row.on; onTapped: root.choose(row.index) }
                RowLayout {
                    id: rowContent
                    x: 8; y: 7; width: parent.width - 16
                    spacing: 10
                    TIcon {
                        Layout.alignment: Qt.AlignTop
                        name: row.modelData.isCloud ? "cloud" : row.modelData.route === "external" ? "key-round" : "monitor"
                        size: 16
                        color: row.on ? Theme.text : Theme.textMuted
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        TLabel {
                            Layout.fillWidth: true
                            text: row.modelData.label
                            font.weight: Theme.weightSemiBold
                            color: row.on ? Theme.text : Theme.textMuted
                            wrapMode: Text.Wrap
                        }
                        TLabel {
                            Layout.fillWidth: true
                            visible: text !== ""
                            text: row.modelData.detail || ""
                            muted: true
                            font.pixelSize: Theme.fontCaption
                            wrapMode: Text.Wrap
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            visible: !row.on && (row.modelData.reason || "") !== ""
                            spacing: 5
                            TIcon { Layout.alignment: Qt.AlignTop; name: "circle-alert"; size: 12; color: Theme.warnInk }
                            TLabel {
                                Layout.fillWidth: true
                                text: row.modelData.reason || ""
                                color: Theme.warnInk
                                font.pixelSize: Theme.fontCaption
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                    // The stored default: a check (its name for the screen reader).
                    TIcon {
                        Layout.alignment: Qt.AlignTop
                        visible: row.modelData.isDefault === true
                        name: "check"
                        size: 15
                        color: Theme.accent
                        Accessible.name: qsTr("alapértelmezett")
                    }
                }
            }
        }
    }
    TMenuSeparator { Layout.fillWidth: true; visible: root.showDefaultBox }
    TCheckBox {
        id: defaultBox
        objectName: "routeMakeDefault"
        visible: root.showDefaultBox
        Layout.fillWidth: true
        Layout.leftMargin: 8
        Layout.bottomMargin: 4
        text: qsTr("Ez legyen az alapértelmezett")
        font.pixelSize: Theme.fontSmall
    }
}
