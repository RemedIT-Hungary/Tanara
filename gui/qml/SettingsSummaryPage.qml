import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// B07 — Összefoglaló: célnyelv, az öt prompt két csoportban, fülekben („módosítva” jelzéssel és
// visszaállítással), egyenközű szerkesztő sorszámokkal és {{VÁLTOZÓ}}-kiemeléssel, a
// változók jelmagyarázata és a csak olvasható kimeneti séma.
Column {
    id: root

    property var vm: null
    signal resetRequested()            // megerősítést kér a befoglaló ablak
    signal schemaRequested()

    readonly property var currentGroup: {
        const groups = root.vm ? root.vm.promptGroups : []
        for (let i = 0; i < groups.length; ++i)
            if (groups[i].value === (root.vm ? root.vm.promptGroup : "")) return groups[i]
        return null
    }

    spacing: 14

    GridLayout {
        width: parent.width
        columns: 2
        columnSpacing: 16
        rowSpacing: 4
        TLabel {
            Layout.preferredWidth: 150
            text: qsTr("Összefoglaló nyelve")
            font.weight: Theme.weightSemiBold
        }
        SettingsCombo {
            Layout.preferredWidth: 260
            editable: true
            options: root.vm ? root.vm.summaryLanguageOptions : []
            value: root.vm ? root.vm.summaryLanguage : ""
            placeholder: qsTr("pl. magyar")
            onPicked: (v) => root.vm.summaryLanguage = v
        }
        Item { Layout.preferredWidth: 150; implicitHeight: 1 }
        TLabel {
            Layout.fillWidth: true
            text: qsTr("Bármilyen nyelvet beírhatsz; független a felület nyelvétől.")
            muted: true
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.Wrap
        }
    }

    Rectangle { width: parent.width; height: 1; color: Theme.border }

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
                enabled: root.vm && root.vm.promptModified
                toolTipText: qsTr("Visszaállítás a beépített utasításra")
                onClicked: root.resetRequested()
            }
        }

        // Két csoport: gyors összefoglaló (egy lépésben / részjegyzet / összefésülés) és
        // témánkénti elemzés; alatta egy mondat arról, mikor melyik utasítás fut.
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

        // Csoportonként egy fülsor; a fülek modellje állandó (a promptTabs indexei), így
        // gépelés közben („módosítva” jelzés) nem épülnek újra.
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

        // ---- szerkesztő: sorszám-oszlop + szöveg ----
        Rectangle {
            id: editorBox
            width: parent.width
            height: 318
            radius: Theme.radiusControl
            color: Theme.sunken
            border.width: 1
            border.color: editor.activeFocus ? Theme.accent : Theme.border
            clip: true

            readonly property int lineHeightPx: 20      // 12 px × 1.65

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

                // Sorszámok: a dokumentum sorai (bekezdései) szerint; a tördelt sor folytatása üres.
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

        // ---- változók ----
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

        // ---- kimeneti séma ----
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

    // ---- a szerkesztő és a nézetmodell összehangolása ----
    property bool loading: false
    property var lineTops: [0]

    function loadText() {
        if (!root.vm) return
        root.loading = true
        if (editor.text !== root.vm.promptText) editor.text = root.vm.promptText
        highlighter.applyLineHeight(editorBox.lineHeightPx)   // 12 px × 1,65 (a betöltés törli a formátumot)
        root.loading = false
        root.relayout()
    }
    // A dokumentum minden sorának (bekezdésének) felső éle a szerkesztőben — a sorszámokhoz.
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
