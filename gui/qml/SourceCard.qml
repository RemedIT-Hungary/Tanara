import QtQuick
import QtQuick.Layouts

// „Miről szól” forrás-kártya (design/handoff-v3 V1/V5, 6. döntés). Ebben a változatban egy
// forrás van: a kézi leírás (Meeting::contextNote — az átírás és az összefoglaló is használja).
// Naptár-kártya később jön (új kártya ugyanide, sosem új fejléc-sor); addig a „Naptár
// összekapcsolása” csak helyőrző.
//   leírás van  → surface kártya: 32 px accentSoft ikon, „Leírás” 14/600 + „kézzel megadva”,
//                 a szöveg 13 px, jobbra „Szerkesztés”
//   nincs       → szaggatott kártya: „Nincs hozzá naptár-esemény” + „Leírás megadása” /
//                 „Naptár összekapcsolása”
//   szerkesztés → szövegmező + Mentés / Mégse (Ctrl+Enter / Esc)
Item {
    id: root

    property string note: ""
    property bool editing: false

    signal saveRequested(string note)

    function startEdit() {
        editor.text = root.note
        root.editing = true
        editor.forceActiveFocus()
    }
    function finish(commit) {
        if (commit && editor.text.trim() !== root.note.trim()) root.saveRequested(editor.text.trim())
        root.editing = false
    }

    implicitHeight: editing ? editBox.implicitHeight : root.note !== "" ? card.implicitHeight : empty.implicitHeight

    // ---- leírás ----
    Rectangle {
        id: card
        objectName: "sourceCard"
        visible: !root.editing && root.note !== ""
        width: parent.width
        implicitHeight: cardRow.implicitHeight + 24
        radius: Theme.radiusPopup
        color: Theme.surface
        border.width: 1
        border.color: Theme.border
        RowLayout {
            id: cardRow
            x: 14; y: 12
            width: parent.width - 28
            spacing: 12
            Rectangle {
                Layout.alignment: Qt.AlignTop
                implicitWidth: 32; implicitHeight: 32
                radius: 7
                color: Theme.accentSoft
                TIcon { anchors.centerIn: parent; name: "file-text"; size: 16; color: Theme.accent }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3
                Row {
                    spacing: 8
                    TLabel { id: cardTitle; text: qsTr("Leírás"); font.pixelSize: Theme.fontBody; font.weight: Theme.weightSemiBold }
                    TLabel { text: qsTr("kézzel megadva"); muted: true; font.pixelSize: Theme.fontCaption; anchors.baseline: cardTitle.baseline }
                }
                TLabel {
                    Layout.fillWidth: true
                    Layout.topMargin: 3
                    text: root.note
                    font.pixelSize: Theme.fontSmall
                    cssLineHeight: 1.45
                    wrapMode: Text.Wrap
                    maximumLineCount: 4
                    elide: Text.ElideRight
                }
            }
            TButton {
                objectName: "sourceEdit"
                Layout.alignment: Qt.AlignTop
                text: qsTr("Szerkesztés")
                variant: "ghost"
                size: "small"
                muted: true
                onClicked: root.startEdit()
            }
        }
    }

    // ---- üres ----
    Item {
        id: empty
        visible: !root.editing && root.note === ""
        width: parent.width
        implicitHeight: emptyRow.implicitHeight + 24
        TDashedRect { anchors.fill: parent; radius: Theme.radiusPopup; color: Theme.borderStrong }
        RowLayout {
            id: emptyRow
            x: 14
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 28
            spacing: 12
            Rectangle {
                implicitWidth: 32; implicitHeight: 32
                radius: 7
                color: Theme.sunken
                TIcon { anchors.centerIn: parent; name: "calendar"; size: 16; color: Theme.textMuted }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                TLabel {
                    Layout.fillWidth: true
                    text: qsTr("Nincs hozzá naptár-esemény")
                    font.pixelSize: 13
                    font.weight: Theme.weightSemiBold
                    wrapMode: Text.Wrap
                }
                TLabel {
                    Layout.fillWidth: true
                    text: qsTr("Egy rövid leírás vagy egy összekapcsolt esemény segít a címkéknél, a résztvevőknél és az átírásnál is. Nem kötelező.")
                    muted: true
                    font.pixelSize: 12
                    cssLineHeight: 1.4
                    wrapMode: Text.Wrap
                }
            }
            TButton {
                objectName: "sourceDescribe"
                text: qsTr("Leírás megadása")
                size: "small"
                onClicked: root.startEdit()
            }
            TButton {
                id: calendarButton
                objectName: "sourceCalendar"
                text: qsTr("Naptár összekapcsolása")
                iconName: "calendar"
                size: "small"
                muted: true
                toolTipText: qsTr("Hamarosan: Google Naptár és Outlook. Addig a leírás segít.")
            }
        }
    }

    // ---- szerkesztés ----
    ColumnLayout {
        id: editBox
        visible: root.editing
        width: parent.width
        spacing: 8
        TTextArea {
            id: editor
            objectName: "sourceEditor"
            Layout.fillWidth: true
            Layout.preferredHeight: 92
            placeholderText: qsTr("Miről szól? Téma, résztvevők, szakszavak, ismert félrehallások („A „…” helyesen: …”).")
            Keys.onEscapePressed: root.finish(false)
            Keys.onPressed: (event) => {
                if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.ControlModifier)) {
                    root.finish(true)
                    event.accepted = true
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            TLabel {
                Layout.fillWidth: true
                text: qsTr("Az átírás és az összefoglaló is felhasználja. Ctrl+Enter: mentés · Esc: mégse")
                muted: true
                font.pixelSize: Theme.fontCaption
                wrapMode: Text.Wrap
            }
            TButton { text: qsTr("Mégse"); size: "small"; variant: "ghost"; onClicked: root.finish(false) }
            TButton { objectName: "sourceSave"; text: qsTr("Mentés"); size: "small"; variant: "primary"; onClicked: root.finish(true) }
        }
    }
}
