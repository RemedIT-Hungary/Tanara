import QtQuick
import QtQuick.Layouts

// Téma-kártya (M08): fogantyú az átrendezéshez, cím, leírás, állapot-pirula, és az állapot
// szerinti tartalom — kész: az elemzés eredménye; fut / sorban áll: csík megszakítással;
// hibás: üzenet + „Újra”; vár: semmi. A műveletek (szerkesztés, törlés, futtatás) a kártya
// fölé húzott egérrel vagy billentyű-fókusszal jelennek meg. A szerkesztés helyben történik.
Rectangle {
    id: root

    property string topicId: ""
    property string title: ""
    property string summary: ""
    property string topicState: "waiting"     // waiting | queued | running | done | failed
    property string error: ""
    property string errorDetail: ""
    property bool hasResult: false
    property string resultText: ""
    property var resultDecisions: []
    property var resultActions: []
    property bool canRun: true                // a szolgáltató kész (különben a futtatás tiltva)
    property string blockedReason: ""
    property bool editing: false
    property bool dragging: false
    property bool expanded: false             // a hosszú elemzés teljes szövege látszik

    signal runRequested()
    signal cancelRequested()
    signal removeRequested()
    signal saveRequested(string title, string summary)
    signal moveRequested(int delta)           // billentyűs átrendezés: -1 fel, +1 le
    // Húzás a fogantyúnál: y a kártya koordinátáiban.
    signal dragMoved(real y)
    signal dragFinished()

    readonly property bool failed: topicState === "failed"
    readonly property bool active: topicState === "running" || topicState === "queued"
    readonly property bool showTools: hover.hovered || toolFocus.activeFocus || editing

    function beginEdit() {
        titleField.text = root.title
        summaryField.text = root.summary
        root.editing = true
        titleField.forceActiveFocus()
    }
    function commitEdit() {
        if (titleField.text.trim() === "") { titleField.forceActiveFocus(); return }
        root.saveRequested(titleField.text, summaryField.text)
        root.editing = false
    }

    implicitHeight: body.implicitHeight + 24
    radius: Theme.radiusPopup
    color: Theme.surface
    border.width: 1
    border.color: failed ? Theme.dangerLine : dragging ? Theme.accent : Theme.border
    opacity: dragging ? 0.92 : 1

    HoverHandler { id: hover }

    // ---- fogantyú (húzás + Ctrl+fel/le) ----
    Item {
        id: grip
        x: 4; y: 8
        width: 24; height: 28
        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: qsTr("Téma áthelyezése: %1").arg(root.title)
        Keys.onPressed: (event) => {
            if (!(event.modifiers & Qt.ControlModifier)) return
            if (event.key === Qt.Key_Up) { root.moveRequested(-1); event.accepted = true }
            else if (event.key === Qt.Key_Down) { root.moveRequested(1); event.accepted = true }
        }
        TIcon {
            anchors.centerIn: parent
            name: "grip-vertical"
            size: 16
            color: gripArea.containsMouse || gripArea.pressed ? Theme.text : Theme.textMuted
        }
        Rectangle {
            anchors.fill: parent
            radius: 4
            color: "transparent"
            border.width: grip.activeFocus ? 2 : 0
            border.color: Theme.accentLine
        }
        MouseArea {
            id: gripArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
            preventStealing: true
            onPressed: root.dragging = true
            onPositionChanged: (mouse) => { if (pressed) root.dragMoved(mapToItem(root, mouse.x, mouse.y).y) }
            onReleased: { root.dragging = false; root.dragFinished() }
            onCanceled: { root.dragging = false; root.dragFinished() }
        }
        TToolTip {
            visible: gripArea.containsMouse && !gripArea.pressed
            text: qsTr("Húzd az átrendezéshez (Ctrl+↑ / Ctrl+↓)")
        }
    }

    ColumnLayout {
        id: body
        x: 36; y: 12
        width: root.width - 36 - 14
        spacing: 5

        // ================= megjelenítés =================
        RowLayout {
            visible: !root.editing
            Layout.fillWidth: true
            spacing: 10
            TLabel {
                Layout.fillWidth: true
                text: root.title
                font.pixelSize: 15
                font.weight: Theme.weightSemiBold
                wrapMode: Text.Wrap
            }
            FocusScope {
                id: toolFocus
                implicitWidth: tools.implicitWidth
                implicitHeight: 22
                opacity: root.showTools ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
                Row {
                    id: tools
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    TIconButton {
                        visible: !root.active
                        iconName: root.hasResult ? "rotate-ccw" : "play"
                        iconSize: 14
                        variant: "flat"; size: "small"
                        enabled: root.canRun
                        toolTipText: !root.canRun ? root.blockedReason
                                   : root.hasResult ? qsTr("Téma újraelemzése") : qsTr("Téma elemzése")
                        onClicked: root.runRequested()
                    }
                    TIconButton {
                        iconName: "pencil"
                        iconSize: 14
                        variant: "flat"; size: "small"
                        toolTipText: qsTr("Cím és leírás szerkesztése")
                        onClicked: root.beginEdit()
                    }
                    TIconButton {
                        iconName: "trash-2"
                        iconSize: 14
                        variant: "flat"; size: "small"
                        toolTipText: qsTr("Téma törlése")
                        onClicked: root.removeRequested()
                    }
                }
            }
            TPill {
                text: root.topicState === "done" ? qsTr("kész")
                    : root.topicState === "running" ? qsTr("fut")
                    : root.topicState === "queued" ? qsTr("sorban áll")
                    : root.failed ? qsTr("hibás") : qsTr("vár")
                tone: root.topicState === "done" ? "success"
                    : root.topicState === "running" ? "accent"
                    : root.failed ? "danger" : "neutral"
            }
        }
        TLabel {
            visible: !root.editing && text !== ""
            Layout.fillWidth: true
            text: root.summary
            muted: true
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.Wrap
        }

        // kész: az elemzés eredménye
        ColumnLayout {
            visible: !root.editing && root.hasResult && !root.active
            Layout.fillWidth: true
            Layout.topMargin: 4
            spacing: 6
            // Hosszú elemzés összecsukva jelenik meg (6 sor), kérésre a teljes szöveg.
            Item {
                id: resultBox
                readonly property real lineHeight: 21
                readonly property real collapsedHeight: 6 * lineHeight
                readonly property bool isLong: resultLabel.implicitHeight > collapsedHeight + lineHeight
                visible: root.resultText !== ""
                Layout.fillWidth: true
                implicitHeight: isLong && !root.expanded ? collapsedHeight : resultLabel.implicitHeight
                clip: true
                TLabel {
                    id: resultLabel
                    width: parent.width
                    text: root.resultText
                    textFormat: Text.MarkdownText
                    cssLineHeight: 1.5
                    wrapMode: Text.Wrap
                }
            }
            TButton {
                visible: resultBox.visible && resultBox.isLong
                text: root.expanded ? qsTr("Kevesebb") : qsTr("Teljes elemzés")
                variant: "ghost"; size: "small"
                implicitHeight: 24
                leftPadding: 0; rightPadding: 6
                muted: true
                trailingIconName: root.expanded ? "chevron-up" : "chevron-down"
                iconSize: 13
                spacing: 4
                onClicked: root.expanded = !root.expanded
            }
            Repeater {
                model: [
                    { heading: qsTr("Döntések"), items: root.resultDecisions },
                    { heading: qsTr("Teendők"), items: root.resultActions },
                ]
                ColumnLayout {
                    id: group
                    required property var modelData
                    visible: group.modelData.items.length > 0
                    Layout.fillWidth: true
                    spacing: 3
                    TLabel {
                        text: group.modelData.heading
                        muted: true
                        font.pixelSize: Theme.fontSmall
                        font.weight: Theme.weightSemiBold
                    }
                    Repeater {
                        model: group.modelData.items
                        RowLayout {
                            id: point
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 8
                            Rectangle {
                                implicitWidth: 4; implicitHeight: 4; radius: 2
                                color: Theme.borderStrong
                                Layout.alignment: Qt.AlignTop
                                Layout.topMargin: 9
                                Layout.leftMargin: 2
                            }
                            TLabel {
                                Layout.fillWidth: true
                                text: typeof point.modelData === "string" ? point.modelData
                                    : point.modelData.text
                                      + (point.modelData.owner ? " — " + point.modelData.owner : "")
                                      + (point.modelData.due ? " (" + point.modelData.due + ")" : "")
                                cssLineHeight: 1.5
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }
            }
        }

        // fut / sorban áll: a modell nem ad százalékot → futó csík; külön megszakítható
        RowLayout {
            visible: !root.editing && root.active
            Layout.fillWidth: true
            Layout.topMargin: 4
            spacing: 10
            TProgressBar {
                Layout.fillWidth: true
                thickness: 5
                indeterminate: root.topicState === "running"
                value: 0
            }
            TLabel {
                text: root.topicState === "running" ? qsTr("elemzés…") : qsTr("sorban áll")
                muted: true
                font.pixelSize: Theme.fontCaption
            }
            TButton {
                text: qsTr("Megszakítás")
                variant: "ghost"; size: "small"
                implicitHeight: 24
                onClicked: root.cancelRequested()
            }
        }

        // hibás: üzenet + Újra
        RowLayout {
            visible: !root.editing && root.failed
            Layout.fillWidth: true
            Layout.topMargin: 2
            spacing: 10
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                TLabel {
                    Layout.fillWidth: true
                    text: root.error
                    color: Theme.dangerInk
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.Wrap
                }
                TLabel {
                    visible: text !== ""
                    Layout.fillWidth: true
                    text: root.errorDetail
                    mono: true; muted: true
                    font.pixelSize: Theme.fontMicro
                    wrapMode: Text.WrapAnywhere
                }
            }
            TButton {
                text: qsTr("Újra")
                size: "small"
                iconName: "rotate-ccw"
                iconSize: 13
                spacing: 6
                enabled: root.canRun
                toolTipText: root.canRun ? "" : root.blockedReason
                onClicked: root.runRequested()
            }
        }

        // ================= szerkesztés =================
        ColumnLayout {
            visible: root.editing
            Layout.fillWidth: true
            spacing: 8
            TTextField {
                id: titleField
                Layout.fillWidth: true
                placeholderText: qsTr("A téma címe")
                hasError: root.editing && text.trim() === ""
                font.weight: Theme.weightSemiBold
                Keys.onReturnPressed: root.commitEdit()
                Keys.onEscapePressed: root.editing = false
                Accessible.name: qsTr("A téma címe")
            }
            TTextArea {
                id: summaryField
                Layout.fillWidth: true
                placeholderText: qsTr("Rövid leírás: mire figyeljen az elemzés (elhagyható)")
                font.pixelSize: Theme.fontSmall
                implicitHeight: Math.max(52, contentHeight + topPadding + bottomPadding)
                Keys.onEscapePressed: root.editing = false
                Accessible.name: qsTr("A téma leírása")
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                TLabel {
                    Layout.fillWidth: true
                    text: root.hasResult ? qsTr("A meglévő elemzés megmarad; új szöveghez futtasd újra a témát.") : ""
                    muted: true
                    font.pixelSize: Theme.fontCaption
                    wrapMode: Text.Wrap
                }
                TButton {
                    text: qsTr("Mégse")
                    variant: "ghost"; size: "small"
                    onClicked: root.editing = false
                }
                TButton {
                    text: qsTr("Mentés")
                    variant: "primary"; size: "small"
                    onClicked: root.commitEdit()
                }
            }
        }
    }
}
