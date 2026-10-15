import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// The summary card's prompts (S2, Settings > Processing > Összefoglaló > "Haladó"): the five prompts
// in two groups, in tabs (with a "modified" indicator and reset), a monospaced editor with line
// numbers and {{VARIABLE}} highlighting, a legend of the variables and the read-only output schema.
// `locked` (the Cloud prompt lock: the summary runs on Tanara Cloud): the editors are read-only and
// one line says why; the own prompts stay and apply on the local / own-key path.
Column {
    id: root

    property var vm: null
    property bool locked: false
    signal resetRequested()            // the enclosing window asks for confirmation
    signal schemaRequested()

    readonly property var currentGroup: {
        const groups = root.vm ? root.vm.promptGroups : []
        for (let i = 0; i < groups.length; ++i)
            if (groups[i].value === (root.vm ? root.vm.promptGroup : "")) return groups[i]
        return null
    }

    spacing: 14

    // The Cloud prompt lock.
    Rectangle {
        objectName: "promptLock"
        visible: root.locked
        width: parent.width
        implicitHeight: lockText.implicitHeight + 20
        radius: Theme.radiusControl
        color: Theme.sunken
        border.width: 1
        border.color: Theme.border
        TIcon { x: 12; y: 11; name: "lock"; size: 14; color: Theme.textMuted }
        TLabel {
            id: lockText
            x: 34; y: 10
            width: parent.width - 46
            text: qsTr("Tanara Cloudon a sablon kötött; a saját utasítások helyi vagy saját kulcsos útvonalon érvényesek.")
            font.pixelSize: Theme.fontSmall
            cssLineHeight: 1.45
            wrapMode: Text.Wrap
        }
    }

    Column {
        width: parent.width
        spacing: 10

        RowLayout {
            width: parent.width
            spacing: 10
            TSectionLabel {
                Layout.fillWidth: true
                text: qsTr("Utasítások a modellnek")
            }
            TPill {
                visible: root.vm && root.vm.promptModified
                text: qsTr("módosítva")
                tone: "warn"
            }
            TButton {
                variant: "ghost"
                size: "small"
                iconName: "rotate-ccw"
                iconSize: 13
                text: qsTr("Alapértelmezett")
                enabled: !root.locked && root.vm && root.vm.promptModified
                toolTipText: qsTr("Visszaállítás a beépített utasításra")
                onClicked: root.resetRequested()
            }
        }

        // Two groups: quick summary (one step / partial note / merge) and per-topic analysis; below it a
        // sentence about when which instruction runs.
        SettingsSegmented {
            options: root.vm ? root.vm.promptGroups : []
            value: root.vm ? root.vm.promptGroup : "quick"
            onPicked: (v) => root.vm.promptGroup = v
        }
        TLabel {
            width: parent.width
            text: root.currentGroup ? root.currentGroup.note : ""
            muted: true
            font.pixelSize: Theme.fontSmall
            cssLineHeight: 1.5
            wrapMode: Text.Wrap
        }

        // One tab row per group; the tabs' model is constant (the promptTabs indices), so they are not
        // rebuilt while typing (the "modified" indicator).
        Repeater {
            model: root.vm ? root.vm.promptGroups : []
            TTabBar {
                id: groupTabs
                required property var modelData
                readonly property bool active: root.vm && root.vm.promptGroup === modelData.value
                visible: active
                width: parent.width
                currentIndex: active ? Math.max(0, modelData.tabs.indexOf(root.vm.promptIndex)) : 0
                onCurrentIndexChanged: {
                    if (!active || currentIndex < 0 || currentIndex >= modelData.tabs.length) return
                    const idx = modelData.tabs[currentIndex]
                    if (root.vm.promptIndex !== idx) root.vm.promptIndex = idx
                }
                Repeater {
                    model: groupTabs.modelData.tabs
                    TTabButton {
                        required property int modelData
                        readonly property var tab: root.vm ? root.vm.promptTabs[modelData] : null
                        text: tab ? tab.label : ""
                        font.pixelSize: Theme.fontSmall
                        pillText: tab && tab.modified ? "•" : ""
                    }
                }
            }
        }

        // ---- editor: line number column + text ----
        Rectangle {
            id: editorBox
            width: parent.width
            height: 318
            radius: Theme.radiusControl
            color: Theme.sunken
            opacity: root.locked ? 0.75 : 1
            border.width: 1
            border.color: editor.activeFocus && !root.locked ? Theme.accent : Theme.border
            clip: true

            readonly property int lineHeightPx: 20      // 12 px x 1.65

            Flickable {
                id: flick
                anchors.fill: parent
                anchors.margins: 1
                contentWidth: width
                contentHeight: Math.max(height, editor.implicitHeight)
                boundsBehavior: Flickable.StopAtBounds
                clip: true
                T.ScrollBar.vertical: TScrollBar {}

                function ensureVisible(r) {
                    if (contentY > r.y) contentY = Math.max(0, r.y - 10)
                    else if (contentY + height < r.y + r.height + 10) contentY = r.y + r.height + 10 - height
                }

                // Line numbers follow the document's lines (paragraphs); a wrapped line's continuation is blank.
                Column {
                    id: gutter
                    x: 0; y: editor.topPadding
                    width: 36
                    Repeater {
                        model: editor.lineCount > 0 ? root.lineTops.length : 0
                        Item {
                            required property int index
                            width: gutter.width
                            height: (index + 1 < root.lineTops.length ? root.lineTops[index + 1]
                                                                      : editor.contentHeight) - root.lineTops[index]
                            TLabel {
                                anchors.right: parent.right
                                anchors.rightMargin: 8
                                height: editorBox.lineHeightPx
                                verticalAlignment: Text.AlignVCenter
                                text: index + 1
                                mono: true; muted: true
                                font.pixelSize: 12
                            }
                        }
                    }
                }
                Rectangle { x: 36; width: 1; height: flick.contentHeight; color: Theme.border }

                TextEdit {
                    id: editor
                    objectName: "promptEditor"
                    x: 37
                    readOnly: root.locked
                    width: flick.width - 37
                    leftPadding: 12; rightPadding: 14; topPadding: 10; bottomPadding: 10
                    wrapMode: TextEdit.Wrap
                    textFormat: TextEdit.PlainText
                    selectByMouse: true
                    persistentSelection: false
                    activeFocusOnTab: true
                    color: Theme.text
                    selectionColor: Theme.accent
                    selectedTextColor: Theme.textOnAccent
                    font.family: Theme.fontMono
                    font.pixelSize: 12
                    tabStopDistance: 28
                    Accessible.name: qsTr("A modellnek adott utasítás")
                    onCursorRectangleChanged: if (activeFocus) flick.ensureVisible(cursorRectangle)
                    onTextChanged: {
                        if (root.vm && !root.loading && text !== root.vm.promptText) root.vm.promptText = text
                        root.relayout()
                    }
                    onWidthChanged: root.relayout()
                    Component.onCompleted: root.loadText()
                }
            }
        }

        // ---- variables ----
        Flow {
            width: parent.width
            spacing: 14
            Repeater {
                model: root.vm ? root.vm.promptVariables : []
                Row {
                    required property var modelData
                    spacing: 6
                    Rectangle {
                        width: tokenLabel.implicitWidth + 8
                        height: 18
                        radius: 3
                        color: Theme.accentSoft
                        TLabel {
                            id: tokenLabel
                            anchors.centerIn: parent
                            text: modelData.token
                            mono: true
                            color: Theme.accent
                            font.pixelSize: Theme.fontCaption
                        }
                    }
                    TLabel {
                        text: modelData.description
                        muted: true
                        font.pixelSize: Theme.fontCaption
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }
            TLabel {
                text: qsTr("Az átirat és a megbeszélés kontextusa az utasítás után, külön üzenetben megy a modellnek.")
                muted: true
                font.pixelSize: Theme.fontCaption
            }
        }

        // ---- output schema ----
        Rectangle {
            width: parent.width
            height: Math.max(40, schemaText.implicitHeight + 18)
            radius: Theme.radiusControl
            color: "transparent"
            border.width: 1
            border.color: Theme.border
            TIcon { x: 12; anchors.verticalCenter: parent.verticalCenter; name: "braces"; size: 14; color: Theme.textMuted }
            Row {
                id: schemaText
                x: 34
                width: parent.width - 34 - schemaButton.width - 16
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6
                TLabel {
                    id: schemaCaption
                    text: qsTr("Kimeneti forma:")
                    font.pixelSize: Theme.fontSmall
                }
                TLabel {
                    width: schemaText.width - schemaCaption.width - 6
                    anchors.baseline: schemaCaption.baseline
                    text: root.vm ? root.vm.schemaSummary : ""
                    mono: true; muted: true
                    font.pixelSize: Theme.fontCaption
                    elide: Text.ElideRight
                }
            }
            TButton {
                id: schemaButton
                anchors.right: parent.right
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                variant: "ghost"
                size: "small"
                text: qsTr("Megtekintés")
                onClicked: root.schemaRequested()
            }
        }
    }

    // ---- syncing the editor with the view model ----
    property bool loading: false
    property var lineTops: [0]

    function loadText() {
        if (!root.vm) return
        root.loading = true
        if (editor.text !== root.vm.promptText) editor.text = root.vm.promptText
        highlighter.applyLineHeight(editorBox.lineHeightPx)   // 12 px x 1.65 (loading clears the format)
        root.loading = false
        root.relayout()
    }
    // Top edge of every document line (paragraph) in the editor -- for the line numbers.
    function relayout() {
        const text = editor.text
        const tops = []
        let pos = 0
        for (;;) {
            tops.push(editor.positionToRectangle(pos).y - editor.topPadding)
            const nl = text.indexOf("\n", pos)
            if (nl < 0) break
            pos = nl + 1
        }
        root.lineTops = tops
    }

    Connections {
        target: root.vm
        function onPromptTextChanged() { root.loadText() }
    }
    onVmChanged: loadText()

    SettingsPromptHighlighter {
        id: highlighter
        document: editor.textDocument
        variableColor: Theme.accent
        variableBackground: Theme.accentSoft
        unknownColor: Theme.warnInk
    }
}
