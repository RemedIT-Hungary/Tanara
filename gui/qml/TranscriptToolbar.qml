import QtQuick
import QtQuick.Layouts

// Az átirat-szerkesztő felső sávja: „ÁTTEKINTÉS" címke (vagy a kereső), „Bizonytalan N"
// szűrő, „Sávok" kapcsoló, visszavonás / újra, keresés — alatta az áttekintő (beszélőnkénti
// idővonal). Padding 12/24/10, alul 1 px elválasztó.
Item {
    id: root

    required property var vm                // TranscriptEditorViewModel
    property bool searchOpen: false
    // Az épp látható lista-szakasz az idővonalon (0..1).
    property real viewportStart: 0
    property real viewportSize: 0

    signal seekRequested(real fraction)     // kattintás az áttekintőn
    signal searchStepRequested(int direction)

    implicitHeight: column.implicitHeight + 22
    height: implicitHeight

    function openSearch() {
        searchOpen = true
        searchField.forceActiveFocus()
        searchField.selectAll()
    }
    function closeSearch() {
        searchOpen = false
        vm.searchQuery = ""
    }

    // Eszköztár-gomb (28 px): ikon + felirat + billentyű-tipp. `checked`: accent stílus.
    component ToolButton: Item {
        id: tb
        property string text: ""
        property string iconName: ""
        property string hint: ""
        property bool checked: false
        property bool outlined: false
        property bool round: false
        property bool muted: false
        property string toolTipText: ""
        property alias hovered: hover.hovered
        default property alias extra: lead.data
        signal clicked()

        readonly property color ink: !enabled ? Theme.textMuted : checked ? Theme.accent
                                   : muted ? Theme.textMuted : Theme.text
        implicitHeight: 28
        implicitWidth: content.implicitWidth + (text === "" ? 0 : 20)
        opacity: enabled ? 1 : 0.5
        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: text !== "" ? text : toolTipText
        Keys.onSpacePressed: clicked()
        Keys.onReturnPressed: clicked()

        Rectangle {
            anchors.fill: parent
            radius: tb.round ? height / 2 : Theme.radiusControl
            color: tb.checked ? Theme.accentSoft : "transparent"
            border.width: tb.outlined || tb.checked ? 1 : 0
            border.color: tb.checked ? Theme.accent : Theme.borderStrong
            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                color: Theme.stateLayer
                opacity: !tb.enabled ? 0 : tap.pressed ? Theme.pressedOpacity : hover.hovered ? Theme.hoverOpacity : 0
            }
            TFocusRing { visible: tb.activeFocus; targetRadius: parent.radius }
        }
        Row {
            id: content
            anchors.centerIn: parent
            spacing: 6
            Row { id: lead; anchors.verticalCenter: parent.verticalCenter }
            TIcon {
                visible: tb.iconName !== ""
                anchors.verticalCenter: parent.verticalCenter
                name: tb.iconName
                size: 15
                color: tb.ink
                width: tb.text === "" ? 28 : 15
            }
            TLabel {
                visible: tb.text !== ""
                anchors.verticalCenter: parent.verticalCenter
                text: tb.text
                color: tb.ink
                font.pixelSize: Theme.fontCaption
                font.weight: tb.checked || tb.iconName === "panel-left" ? Theme.weightSemiBold : Theme.weightMedium
            }
            TLabel {
                visible: tb.hint !== ""
                anchors.verticalCenter: parent.verticalCenter
                text: tb.hint
                mono: true
                color: tb.checked ? Theme.accent : Theme.textMuted
                opacity: tb.checked ? 0.75 : 1
                font.pixelSize: Theme.fontMicro
            }
        }
        HoverHandler { id: hover }
        TapHandler { id: tap; enabled: tb.enabled; onTapped: tb.clicked() }
        TToolTip { visible: tb.toolTipText !== "" && hover.hovered; text: tb.toolTipText }
    }

    ColumnLayout {
        id: column
        x: Theme.space5
        y: 12
        width: root.width - 2 * Theme.space5
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            TSectionLabel {
                visible: !root.searchOpen
                Layout.fillWidth: true
                text: qsTr("Áttekintés")
            }

            // Keresés az átiratban (a címke helyén).
            RowLayout {
                visible: root.searchOpen
                Layout.fillWidth: true
                spacing: 6
                TSearchField {
                    id: searchField
                    Layout.fillWidth: true
                    Layout.maximumWidth: 280
                    Layout.preferredHeight: 28
                    placeholderText: qsTr("Keresés az átiratban")
                    onTextChanged: root.vm.searchQuery = text
                    Keys.onReturnPressed: event => root.searchStepRequested(event.modifiers & Qt.ShiftModifier ? -1 : 1)
                    Keys.onEnterPressed: event => root.searchStepRequested(event.modifiers & Qt.ShiftModifier ? -1 : 1)
                    Keys.onEscapePressed: root.closeSearch()
                    Connections {
                        target: root.vm
                        function onSearchChanged() {
                            if (root.vm.searchQuery !== "" && !root.searchOpen) root.searchOpen = true
                            if (searchField.text !== root.vm.searchQuery) searchField.text = root.vm.searchQuery
                        }
                    }
                }
                TLabel {
                    visible: root.vm.searchQuery.trim() !== ""
                    text: root.vm.searchMatchCount === 0 ? qsTr("Nincs találat")
                        : qsTr("%1 / %2").arg(root.vm.searchCurrent + 1).arg(root.vm.searchMatchCount)
                    mono: root.vm.searchMatchCount > 0
                    color: root.vm.searchMatchCount === 0 ? Theme.dangerInk : Theme.textMuted
                    font.pixelSize: Theme.fontCaption
                }
                ToolButton {
                    iconName: "chevron-up"
                    enabled: root.vm.searchMatchCount > 0
                    toolTipText: qsTr("Előző találat (Shift+Enter)")
                    onClicked: root.searchStepRequested(-1)
                }
                ToolButton {
                    iconName: "chevron-down"
                    enabled: root.vm.searchMatchCount > 0
                    toolTipText: qsTr("Következő találat (Enter)")
                    onClicked: root.searchStepRequested(1)
                }
                Item { Layout.fillWidth: true }
            }

            ToolButton {
                id: uncertainChip
                round: true
                outlined: true
                checked: root.vm.uncertainOnly
                enabled: root.vm.voiceAvailable || root.vm.uncertainOnly
                text: qsTr("Bizonytalan")
                hint: String(root.vm.uncertainCount)
                toolTipText: !root.vm.voiceAvailable ? root.vm.voiceNote
                           : root.vm.uncertainOnly ? qsTr("Minden sor mutatása")
                           : qsTr("Csak azok a sorok, ahol a beszélő hang alapján kétséges")
                onClicked: root.vm.uncertainOnly = !root.vm.uncertainOnly
                SpeakerHatch {
                    width: 12; height: 12
                    radius: 2
                    stripe: 1.5
                    color: uncertainChip.checked ? Theme.accent : Theme.textMuted
                }
            }
            ToolButton {
                outlined: true
                checked: root.vm.railVisible
                iconName: "panel-left"
                text: qsTr("Sávok")
                hint: root.searchOpen ? "" : "Ctrl+L"
                toolTipText: root.vm.railVisible ? qsTr("Beszélő-sávok elrejtése") : qsTr("Beszélő-sávok mutatása a javításhoz")
                onClicked: root.vm.railVisible = !root.vm.railVisible
            }
            ToolButton {
                iconName: "undo-2"
                text: root.searchOpen ? "" : qsTr("Visszavonás")
                hint: root.searchOpen ? "" : "Ctrl+Z"
                enabled: root.vm.canUndo
                toolTipText: root.vm.canUndo ? qsTr("Visszavonás: %1").arg(root.vm.undoText) : qsTr("Nincs mit visszavonni")
                onClicked: root.vm.undo()
            }
            ToolButton {
                iconName: "redo-2"
                muted: true
                enabled: root.vm.canRedo
                toolTipText: root.vm.canRedo ? qsTr("Újra: %1 (Ctrl+Shift+Z)").arg(root.vm.redoText) : qsTr("Nincs mit újra végrehajtani")
                onClicked: root.vm.redo()
            }
            ToolButton {
                iconName: root.searchOpen ? "x" : "search"
                checked: false
                toolTipText: root.searchOpen ? qsTr("Keresés bezárása") : qsTr("Keresés az átiratban")
                onClicked: root.searchOpen ? root.closeSearch() : root.openSearch()
            }
        }

        // Áttekintő: beszélőnként egy 12 px-es sor (név · idővonal · beszédidő %).
        Item {
            id: overview
            Layout.fillWidth: true
            implicitHeight: lanes.implicitHeight

            readonly property real trackX: 120
            readonly property real trackWidth: Math.max(10, width - trackX - 44)

            Column {
                id: lanes
                width: parent.width
                spacing: 3
                Repeater {
                    model: root.vm.overview
                    Item {
                        id: laneRow
                        required property var modelData
                        readonly property bool other: modelData.colorIndex < 0
                        width: lanes.width
                        height: 12
                        TLabel {
                            width: 110
                            anchors.verticalCenter: parent.verticalCenter
                            text: laneRow.modelData.name
                            color: laneRow.other ? Theme.textMuted : Theme.speakerInk(laneRow.modelData.colorIndex)
                            font.pixelSize: Theme.fontMicro
                            font.weight: Theme.weightSemiBold
                            elide: Text.ElideRight
                        }
                        TranscriptLaneStrip {
                            x: overview.trackX
                            width: overview.trackWidth
                            height: 12
                            segments: laneRow.modelData.segments
                            marks: laneRow.modelData.marks
                            trackColor: Theme.sunken
                            color: laneRow.other ? Theme.borderStrong : Theme.speakerLine(laneRow.modelData.colorIndex)
                            markColor: Theme.accent
                        }
                        TLabel {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            text: laneRow.modelData.pct + "%"
                            mono: true
                            muted: true
                            font.pixelSize: Theme.fontMicro
                        }
                    }
                }
            }

            // A lista épp látható szakasza.
            Rectangle {
                visible: root.viewportSize > 0 && lanes.height > 0
                x: overview.trackX + Math.round(root.viewportStart * overview.trackWidth)
                y: -2
                width: Math.max(4, Math.round(root.viewportSize * overview.trackWidth))
                height: lanes.height + 4
                radius: 2
                color: Theme.alpha(Theme.text, 0.12)
            }

            MouseArea {
                x: overview.trackX
                width: overview.trackWidth
                height: parent.height
                cursorShape: Qt.PointingHandCursor
                onClicked: mouse => root.seekRequested(mouse.x / width)
            }
        }
    }

    TDivider { anchors.bottom: parent.bottom; width: parent.width }
}
