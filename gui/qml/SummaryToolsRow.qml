import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// Az összefoglaló fülek eszköz-sora (S1/S2, 46 px): generálás-adat („Gyors összefoglaló ·
// okt. 1. 17:05 · LM Studio · 1298 megszólalásból”) · „Források” kapcsoló (be: accentSoft) ·
// Másolás · Újragenerálás. A héj (U1) a fejléc alatti eszköz-sorba teszi; amíg ott nincs
// helye, a SummaryTab teteje mutatja (showSectionSwitch: ideiglenes váltó a két forma közt,
// amíg a „Memó” nem külön fül).
//   SummaryToolsRow { vm: summaryTab.vm; meetingId: …; shell: … }
Rectangle {
    id: root

    required property var vm           // SummaryViewModel
    property string meetingId: ""
    property var shell: null
    property bool showSectionSwitch: false

    readonly property bool memoReady: vm.memoState === "ready"

    implicitHeight: 46
    implicitWidth: row.implicitWidth + 44
    color: "transparent"

    function copy() {
        const part = !root.memoReady ? "all" : root.vm.section === "memo" ? "memo" : "exec"
        if (!root.vm.copyToClipboard(part) || !root.shell) return
        root.shell.toast(part === "exec" ? qsTr("A vezetői összefoglaló a vágólapra került.")
                       : part === "memo" ? qsTr("A memó a vágólapra került.")
                       : qsTr("Az összefoglaló a vágólapra került."))
    }
    function regenerate() {
        root.vm.note.commitDraft()   // a függő megjegyzés-piszkozat már az új futásba kerüljön
        if (root.vm.mode === "topics" && root.vm.hasTopics) root.vm.topicsOpen = true
        else if (root.shell) root.shell.startQuickSummary(root.meetingId)
    }

    Rectangle {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 1
        color: Theme.border
    }

    RowLayout {
        id: row
        anchors { fill: parent; leftMargin: 24; rightMargin: 20; bottomMargin: 1 }
        spacing: 10

        // Ideiglenes: a két forma közti váltó, amíg a fejlécben nincs külön „Memó” fül.
        SettingsSegmented {
            visible: root.showSectionSwitch && root.vm.memoState !== "none"
            implicitHeight: 30
            value: root.vm.section
            options: [
                { value: "exec", label: qsTr("Vezetői összefoglaló") },
                { value: "memo", label: qsTr("Memó") },
            ]
            onPicked: (v) => root.vm.section = v
        }

        TLabel {
            objectName: "summaryGenerationLine"
            Layout.fillWidth: true
            text: root.vm.generationLine
            muted: true
            font.pixelSize: Theme.fontSmall
            elide: Text.ElideRight
        }

        // „Források”: pirula-kapcsoló (be: accentSoft + accent keret és felirat).
        T.AbstractButton {
            id: sourcesToggle
            objectName: "sourcesToggle"
            visible: root.vm.hasStatements
            checkable: true
            checked: root.vm.sourcesVisible
            onToggled: root.vm.sourcesVisible = checked
            hoverEnabled: true
            activeFocusOnTab: true
            implicitHeight: 28
            implicitWidth: toggleRow.implicitWidth + 20
            font.family: Theme.fontSans
            font.pixelSize: Theme.fontSmall
            font.weight: Theme.weightSemiBold
            Accessible.role: Accessible.CheckBox
            Accessible.name: qsTr("Források")
            Keys.onReturnPressed: toggle()
            TToolTip {
                visible: sourcesToggle.hovered
                text: sourcesToggle.checked ? qsTr("Az idő-chipek és a forrás-jelölések elrejtése")
                                            : qsTr("Minden állítás mellett: hol hangzott el")
            }
            background: Rectangle {
                radius: 14
                color: sourcesToggle.checked ? Theme.accentSoft : Theme.raised
                border.width: 1
                border.color: sourcesToggle.checked ? Theme.accent : Theme.borderStrong
                Rectangle {
                    anchors.fill: parent
                    radius: parent.radius
                    color: Theme.stateLayer
                    opacity: sourcesToggle.down ? Theme.pressedOpacity : sourcesToggle.hovered ? Theme.hoverOpacity : 0
                }
                TFocusRing { visible: sourcesToggle.visualFocus; targetRadius: 14 }
            }
            contentItem: Item {
                Row {
                    id: toggleRow
                    anchors.centerIn: parent
                    spacing: 7
                    TIcon {
                        name: "quote"
                        size: 14
                        color: sourcesToggle.checked ? Theme.accent : Theme.text
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: qsTr("Források")
                        font: sourcesToggle.font
                        color: sourcesToggle.checked ? Theme.accent : Theme.text
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }
        }

        TButton {
            objectName: "summaryCopy"
            text: qsTr("Másolás")
            size: "small"
            iconName: "copy"
            iconSize: 14
            leftPadding: 10; rightPadding: 10
            enabled: root.vm.hasSummary
            toolTipText: !root.memoReady ? "" : root.vm.section === "memo" ? qsTr("A memó másolása")
                                                                        : qsTr("A vezetői összefoglaló másolása (források nélkül)")
            onClicked: root.copy()
        }
        TButton {
            objectName: "summaryRegenerate"
            text: qsTr("Újragenerálás")
            size: "small"
            iconName: "refresh-cw"
            iconSize: 14
            leftPadding: 10; rightPadding: 10
            enabled: root.vm.canRun && !root.vm.jobRunning
            toolTipText: root.vm.canRun ? "" : (root.vm.blocker.reason || "")
            onClicked: root.regenerate()
        }
    }
}
