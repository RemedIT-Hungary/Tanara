import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// Right-hand panel for multi-select: "3 meetings selected" + key hints, the selected list (title,
// tags, date), TAGS: chips of the shared tags (on all: solid border "3/3"; on some only: dashed
// "1/3" - click applies to all; x removes from all), input "Add tag to all 3 meetings...",
// "Clear selection". Every bulk change is one undo step (LibrarySelectionModel -> TagService
// group).
Rectangle {
    id: root

    property var selection: null              // LibrarySelectionModel
    readonly property int count: selection ? selection.count : 0

    color: Theme.bg
    focus: visible
    Keys.onEscapePressed: if (root.selection) root.selection.clear()

    // The panel covers the meeting view below it: mouse / hover must not pass through.
    // Clicking the empty area takes focus away from the field (so Esc arrives here).
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.AllButtons
        onPressed: root.forceActiveFocus()
        onWheel: (wheel) => { wheel.accepted = true }
    }

    TagInputModel {
        id: inputModel
        excludeIds: root.selection ? root.selection.fullTagIds : []
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: column.implicitHeight + 56 + 32
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        T.ScrollBar.vertical: TScrollBar { opacity: active ? 1 : 0 }

        ColumnLayout {
            id: column
            width: Math.min(560, flick.width - 2 * Theme.space5)
            x: (flick.width - width) / 2
            y: 56
            spacing: 18

            // ---- title + hints ----
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                TLabel {
                    objectName: "selectionTitle"
                    Layout.fillWidth: true
                    text: qsTr("%n megbeszélés kijelölve", "", root.count)
                    font.pixelSize: 22
                    font.weight: Theme.weightSemiBold
                    elide: Text.ElideRight
                }
                TLabel {
                    Layout.fillWidth: true
                    text: qsTr("Ctrl+kattintás: hozzáadás vagy elvétel · Shift+kattintás: tartomány · Esc: kijelölés megszüntetése")
                    muted: true
                    font.pixelSize: Theme.fontSmall
                    cssLineHeight: 1.45
                    wrapMode: Text.Wrap
                }
            }

            // ---- the selected meetings ----
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: itemsColumn.implicitHeight
                radius: Theme.radiusPopup
                color: Theme.surface
                border.width: 1
                border.color: Theme.border

                Column {
                    id: itemsColumn
                    width: parent.width
                    Repeater {
                        model: root.selection ? root.selection.items : []
                        Item {
                            id: itemRow
                            required property var modelData
                            required property int index
                            width: itemsColumn.width
                            implicitHeight: itemGrid.implicitHeight + 18
                            TDivider {
                                visible: itemRow.index > 0
                                anchors { left: parent.left; right: parent.right; top: parent.top }
                            }
                            GridLayout {
                                id: itemGrid
                                anchors { left: parent.left; right: parent.right; top: parent.top
                                          leftMargin: 12; rightMargin: 12; topMargin: 9 }
                                columns: 2
                                columnSpacing: 12
                                rowSpacing: 4
                                TLabel {
                                    Layout.fillWidth: true
                                    text: itemRow.modelData.title
                                    font.weight: Theme.weightSemiBold
                                    elide: Text.ElideRight
                                    wrapMode: Text.NoWrap
                                }
                                TLabel {
                                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                                    text: itemRow.modelData.dateText
                                    mono: true
                                    muted: true
                                    font.pixelSize: Theme.fontCaption
                                }
                                TLabel {
                                    visible: itemRow.modelData.tagsText !== ""
                                    Layout.fillWidth: true
                                    Layout.columnSpan: 2
                                    text: itemRow.modelData.tagsText
                                    muted: true
                                    font.pixelSize: Theme.fontCaption
                                    elide: Text.ElideRight
                                    wrapMode: Text.NoWrap
                                }
                            }
                        }
                    }
                }
            }

            // ---- TAGS ----
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 10

                TSectionLabel {
                    text: qsTr("Címkék")
                }
                TLabel {
                    visible: root.selection ? root.selection.tagRows.length === 0 : true
                    Layout.fillWidth: true
                    text: qsTr("A kijelölt megbeszéléseken még nincs címke.")
                    muted: true
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.Wrap
                }
                Repeater {
                    model: root.selection ? root.selection.tagRows : []
                    RowLayout {
                        id: tagRow
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 10
                        TagChip {
                            kind: tagRow.modelData.full ? "applied" : "partial"
                            text: tagRow.modelData.name
                            countText: tagRow.modelData.countText
                            removable: true
                            removeAlwaysVisible: true
                            toolTipText: tagRow.modelData.full ? ""
                                         : qsTr("Kattintásra mind a %n megbeszélésre felkerül", "", root.count)
                            onClicked: if (!tagRow.modelData.full) root.selection.addTagToAll(tagRow.modelData.id)
                            onRemoveRequested: root.selection.removeTagFromAll(tagRow.modelData.id)
                        }
                        TLabel {
                            Layout.fillWidth: true
                            text: tagRow.modelData.note
                            muted: true
                            font.pixelSize: Theme.fontSmall
                            elide: Text.ElideRight
                            wrapMode: Text.NoWrap
                        }
                    }
                }
                TagInput {
                    id: addInput
                    objectName: "bulkTagInput"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    standalone: true
                    model: inputModel
                    placeholderText: qsTr("Címke hozzáadása mind a %n megbeszéléshez…", "", root.count)
                    onTagChosen: (name, isNew) => root.selection.addTagToAll(name)
                    onClosed: root.forceActiveFocus()
                }
                TLabel {
                    Layout.fillWidth: true
                    text: qsTr("A szaggatott keretes címke csak némelyiken van rajta: kattintásra mindegyikre felkerül. Egy lépésben visszavonható.")
                    muted: true
                    font.pixelSize: Theme.fontCaption
                    cssLineHeight: 1.45
                    wrapMode: Text.Wrap
                }
            }

            TButton {
                text: qsTr("Kijelölés megszüntetése")
                onClicked: if (root.selection) root.selection.clear()
            }
        }
    }
}
