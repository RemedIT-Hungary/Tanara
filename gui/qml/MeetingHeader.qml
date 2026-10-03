import QtQuick
import QtQuick.Layouts

// Megbeszélés-fejléc: cím (20/600, helyben átnevezhető), meta-sor („2026. okt. 1. · 1:16:04 ·
// 6 beszélő”), jobbra „Résztvevők azonosítása” (csak ha van átirat — előtte az átirat előtti
// nézet kínálja) és a „…” menü: Átnevezés, Megnyitás mappában, Újra-átírás…, Törlés….
Item {
    id: root

    property var shell: null                 // ShellActions vagy null
    property var meeting: null               // ShellMeetingModel vagy null (önálló képernyőkép)
    property string meetingId: meeting ? meeting.meetingId : ""

    // Önállóan renderelve kitalált minta látszik.
    readonly property string title: meeting ? meeting.title : "Negyedéves partnertalálkozó"
    readonly property string metaText: meeting ? meeting.metaText : "2026. okt. 1. · 1:16:04 · 6 beszélő"
    readonly property bool hasTranscript: meeting ? meeting.hasTranscript : true
    readonly property bool canIdentify: meeting ? meeting.canIdentify && !meeting.identifyRunning : true

    function startRename() {
        titleEdit.text = root.title
        titleEdit.visible = true
        titleEdit.forceActiveFocus()
        titleEdit.selectAll()
    }
    function finishRename(commit) {
        if (!titleEdit.visible)
            return
        titleEdit.visible = false
        // A fókusz ne maradjon a (láthatatlan) mezőben: a Szóköz / nyilak oda mennének.
        if (titleEdit.activeFocus) root.forceActiveFocus()
        if (commit && root.shell && titleEdit.text.trim() !== "" && titleEdit.text.trim() !== root.title)
            root.shell.renameMeeting(root.meetingId, titleEdit.text)
    }
    onMeetingIdChanged: finishRename(false)

    implicitHeight: layout.implicitHeight

    RowLayout {
        id: layout
        anchors { left: parent.left; right: parent.right }
        spacing: Theme.space3

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Item {
                Layout.fillWidth: true
                implicitHeight: 28

                TLabel {
                    id: titleLabel
                    visible: !titleEdit.visible
                    anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter }
                    text: root.title
                    font.pixelSize: Theme.fontTitle
                    font.weight: Theme.weightSemiBold
                    elide: Text.ElideRight
                    TapHandler { onDoubleTapped: root.startRename() }
                    HoverHandler { id: titleHover }
                    TToolTip {
                        visible: titleHover.hovered && titleLabel.truncated
                        text: root.title
                    }
                }
                TTextField {
                    id: titleEdit
                    objectName: "meetingTitleEdit"
                    visible: false
                    anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter
                              leftMargin: -8 }
                    leftPadding: 7
                    implicitHeight: 30
                    font.pixelSize: Theme.fontTitle
                    font.weight: Theme.weightSemiBold
                    Accessible.name: qsTr("A megbeszélés címe")
                    onAccepted: root.finishRename(true)
                    Keys.onEscapePressed: root.finishRename(false)
                    onActiveFocusChanged: if (!activeFocus) root.finishRename(true)
                }
            }
            TLabel {
                Layout.fillWidth: true
                text: root.metaText
                muted: true
                font.pixelSize: Theme.fontSmall
                elide: Text.ElideRight
            }
        }

        TButton {
            visible: root.hasTranscript
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 3
            text: qsTr("Résztvevők azonosítása")
            iconName: "fingerprint"
            enabled: root.canIdentify
            font.weight: Theme.weightSemiBold
            font.pixelSize: Theme.fontSmall
            toolTipText: qsTr("A névtelen beszélők párosítása az ismert hanglenyomatokkal. "
                              + "Megszakítható, bármikor újrafuttatható.")
            onClicked: if (root.shell) root.shell.identifyParticipants(root.meetingId)
        }
        TIconButton {
            id: moreButton
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 3
            iconName: "ellipsis"
            toolTipText: qsTr("További műveletek")
            down: pressed || moreMenu.visible
            onClicked: moreMenu.popup(moreButton, moreButton.width - moreMenu.width, moreButton.height + 4)

            TMenu {
                id: moreMenu
                TMenuItem {
                    text: qsTr("Átnevezés")
                    iconName: "pencil"
                    shortcutText: "F2"
                    onTriggered: root.startRename()
                }
                TMenuItem {
                    text: qsTr("Megnyitás mappában")
                    iconName: "folder-open"
                    onTriggered: if (root.shell) root.shell.revealInFolder(root.meetingId)
                }
                TMenuItem {
                    text: qsTr("Újra-átírás…")
                    iconName: "rotate-ccw"
                    enabled: root.hasTranscript
                    onTriggered: if (root.shell) root.shell.retranscribe(root.meetingId)
                }
                TMenuSeparator {}
                TMenuItem {
                    text: qsTr("Törlés…")
                    iconName: "trash-2"
                    danger: true
                    onTriggered: if (root.shell) root.shell.requestDelete(root.meetingId)
                }
            }
        }
    }
}
