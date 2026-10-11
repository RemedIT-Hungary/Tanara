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
// Átirat előtt a kártya alatt egy tömör sor (a régi átirat előtti oldal jegyzet-szerkesztőjéből):
//   „Sablon hasonló megbeszélésből:” + chipek (MeetingNoteModel.suggestions; a tooltip az eleje;
//   kattintásra a szerkesztőbe kerül: üres mezőbe csere, különben hozzáfűzés — a mentés a
//   felhasználóé) — csak ha nincs leírás vagy éppen szerkeszt; és az „Észlelt hívás: Teams” sor
//   (MeetingNoteModel.detectedCallApp, csak tájékoztatás).
Item {
    id: root

    property string note: ""
    property bool editing: false
    // [{ title, dateText, preview, note }] — MeetingNoteModel.suggestions (üres → nincs sor).
    property var suggestions: []
    property string detectedCallApp: ""

    readonly property bool templatesShown: suggestions.length > 0 && (editing || note === "")
    readonly property real mainHeight: editing ? editBox.implicitHeight : note !== "" ? card.implicitHeight : empty.implicitHeight

    signal saveRequested(string note)

    // Sablon a szerkesztőbe: üres mezőbe csere, különben új sorba hozzáfűzés.
    function useTemplate(text) {
        if (!root.editing) root.startEdit()
        const current = editor.text.trim()
        editor.text = current === "" ? text : current + "\n" + text
        editor.cursorPosition = editor.text.length
        editor.forceActiveFocus()
    }

    function startEdit() {
        editor.text = root.note
        root.editing = true
        editor.forceActiveFocus()
    }
    function finish(commit) {
        if (commit && editor.text.trim() !== root.note.trim()) root.saveRequested(editor.text.trim())
        root.editing = false
    }

    implicitHeight: mainHeight + (extras.visible ? extras.implicitHeight + 8 : 0)

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
    // ---- sablonok + észlelt hívás (átirat előtt) ----
    ColumnLayout {
        id: extras
        objectName: "sourceExtras"
        visible: root.templatesShown || root.detectedCallApp !== ""
        y: root.mainHeight + 8
        width: parent.width
        spacing: 6
        Flow {
            objectName: "sourceTemplates"
            visible: root.templatesShown
            Layout.fillWidth: true
            spacing: 6
            TLabel {
                height: 24
                verticalAlignment: Text.AlignVCenter
                text: qsTr("Sablon hasonló megbeszélésből:")
                muted: true
                font.pixelSize: Theme.fontCaption
            }
            Repeater {
                model: root.templatesShown ? root.suggestions : []
                TChip {
                    id: tpl
                    required property var modelData
                    objectName: "sourceTemplateChip"
                    tone: "neutral"
                    iconName: "copy"
                    text: modelData.dateText ? modelData.title + " · " + modelData.dateText : modelData.title
                    focusPolicy: Qt.TabFocus
                    Accessible.name: qsTr("Sablon: %1").arg(modelData.title)
                    onClicked: root.useTemplate(modelData.note)
                    TToolTip {
                        visible: tpl.hovered && (tpl.modelData.preview || "") !== ""
                        delay: 300
                        text: tpl.modelData.preview || ""
                    }
                }
            }
        }
        RowLayout {
            objectName: "sourceDetectedCall"
            visible: root.detectedCallApp !== ""
            Layout.fillWidth: true
            spacing: 6
            TIcon { name: "radar"; size: 13; color: Theme.textMuted }
            TLabel {
                Layout.fillWidth: true
                text: qsTr("Észlelt hívás: %1").arg(root.detectedCallApp)
                muted: true
                font.pixelSize: Theme.fontCaption
                elide: Text.ElideRight
            }
        }
    }
}
