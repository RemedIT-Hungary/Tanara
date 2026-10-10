import QtQuick

// Szennyezett mag (handoff-v3 E2): egy beszélő megerősített sorai két különböző hangra esnek —
// amíg ez nincs rendben, minden újraellenőrzés rossz irányba visz, ezért az Átnézendő nézet
// tetején áll. warnSoft sáv: szétválasztás-ikon · cím + (N / M sor) · „Szétválasztás" ·
// „Miért?" (a beszélő „Miért ő?" panelje).
//   ContaminatedCoreBanner { info: editorVm.contaminated }
Rectangle {
    id: root

    // TranscriptEditorViewModel.contaminated: { speakerKey, name, title, detail, count, lines, … }
    property var info: ({})

    signal splitRequested(string speakerKey)
    signal whyRequested(string speakerKey, Item anchor)

    implicitHeight: Math.max(44, label.implicitHeight + 18)
    radius: Theme.radiusPopup
    color: Theme.warnSoft
    border.width: 1
    border.color: Theme.warnLine

    function escaped(t) { return String(t || "").replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;") }

    TIcon {
        id: icon
        x: 12
        anchors.verticalCenter: parent.verticalCenter
        name: "split"
        size: 16
        color: Theme.warnInk
    }
    TLabel {
        id: label
        objectName: "contaminatedTitle"
        anchors.left: icon.right
        anchors.leftMargin: 12
        anchors.right: actions.left
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        wrapMode: Text.Wrap
        textFormat: Text.StyledText
        font.pixelSize: Theme.fontSmall + 0.5
        text: "<b>" + root.escaped(root.info.title) + "</b> <font color=\"" + Theme.textMuted + "\">"
              + qsTr("(%1 / %2 sor)").arg(root.info.count || 0).arg(root.info.lines || 0) + "</font>"
        HoverHandler { id: labelHover }
        TToolTip { visible: labelHover.hovered && (root.info.detail || "") !== ""; text: root.info.detail || ""; delay: 300 }
    }
    Row {
        id: actions
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        spacing: 8
        TButton {
            focusPolicy: Qt.TabFocus     // kattintásra a fókusz a szerkesztőé marad (B, 1, Ctrl+Z)
            objectName: "splitSpeaker"
            size: "small"
            text: qsTr("Szétválasztás")
            toolTipText: (root.info.detail || "") + qsTr(" — visszavonható: Ctrl+Z")
            onClicked: root.splitRequested(root.info.speakerKey || "")
        }
        TButton {
            focusPolicy: Qt.TabFocus     // kattintásra a fókusz a szerkesztőé marad (B, 1, Ctrl+Z)
            id: why
            objectName: "contaminatedWhy"
            size: "small"
            variant: "ghost"
            text: qsTr("Miért?")
            onClicked: root.whyRequested(root.info.speakerKey || "", why)
        }
    }
}
