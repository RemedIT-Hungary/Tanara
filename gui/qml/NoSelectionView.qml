import QtQuick
import QtQuick.Layouts

// M02 — nincs kijelölt megbeszélés: „Válassz egy megbeszélést” + az „Ezek várnak rád” kártya
// (átírásra váró felvételek, elavult összefoglalók, sikertelen átírások). Ha semmi nem vár,
// a kártya nem látszik.
Item {
    id: root

    property var shell: null                 // ShellActions vagy null

    LibraryPendingModel { id: pending }

    function open(item) {
        if (!root.shell)
            return
        root.shell.showMeeting(item.meetingId)
        root.shell.showTab(item.tab)
    }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(560, parent.width - 2 * Theme.space5)
        spacing: 0

        TLabel {
            Layout.fillWidth: true
            text: qsTr("Válassz egy megbeszélést")
            font.pixelSize: 22
            font.weight: Theme.weightSemiBold
            wrapMode: Text.Wrap
        }
        TLabel {
            Layout.fillWidth: true
            Layout.topMargin: 6
            text: pending.items.length > 0 ? qsTr("Ezek várnak rád:")
                                           : qsTr("A bal oldali listából nyithatsz meg egyet. Most semmi nem vár rád.")
            muted: true
            font.pixelSize: 15
            wrapMode: Text.Wrap
        }

        Rectangle {
            visible: pending.items.length > 0
            Layout.fillWidth: true
            Layout.topMargin: 16
            implicitHeight: rows.implicitHeight
            radius: Theme.radiusPopup
            color: Theme.surface
            border.width: 1
            border.color: Theme.border

            Column {
                id: rows
                anchors { left: parent.left; right: parent.right }
                Repeater {
                    model: pending.items
                    Item {
                        id: row
                        required property var modelData
                        required property int index
                        width: rows.width
                        height: 60

                        readonly property color soft: modelData.tone === "warn" ? Theme.warnSoft
                                                    : modelData.tone === "danger" ? Theme.dangerSoft : Theme.accentSoft
                        readonly property color ink: modelData.tone === "warn" ? Theme.warnInk
                                                   : modelData.tone === "danger" ? Theme.dangerInk : Theme.accent

                        TDivider {
                            visible: row.index > 0
                            anchors { left: parent.left; right: parent.right; top: parent.top
                                      leftMargin: 1; rightMargin: 1 }
                        }
                        RowLayout {
                            anchors { fill: parent; leftMargin: 14; rightMargin: 12 }
                            spacing: 12
                            Rectangle {
                                implicitWidth: 28; implicitHeight: 28
                                radius: Theme.radiusControl
                                color: row.soft
                                TIcon { anchors.centerIn: parent; name: row.modelData.iconName; size: 15; color: row.ink }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 1
                                TLabel {
                                    Layout.fillWidth: true
                                    text: row.modelData.title
                                    font.weight: Theme.weightMedium
                                    elide: Text.ElideRight
                                }
                                TLabel {
                                    Layout.fillWidth: true
                                    text: row.modelData.subtitle
                                    muted: true
                                    font.pixelSize: Theme.fontCaption
                                    elide: Text.ElideRight
                                }
                            }
                            TButton {
                                text: row.modelData.actionLabel
                                font.weight: Theme.weightSemiBold
                                font.pixelSize: Theme.fontSmall
                                implicitHeight: 32
                                onClicked: root.open(row.modelData)
                            }
                        }
                    }
                }
            }
        }
    }
}
