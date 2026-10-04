import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// A megbeszélés-megjegyzés szerkesztője (MeetingNoteModel) — az átirat előtti nézet 1. lépése
// és az Összefoglaló fül közös része. Fentről lefelé: sablon-javaslatok a korábbi, hasonló
// című megbeszélésekből (kattintásra a mezőbe kerülnek; ha a mezőben már van szöveg, rákérdez:
// csere vagy hozzáfűzés), a mező, alatta a magyarázat és az észlelt hívás.
// Mentés: késleltetve (900 ms) és fókuszvesztéskor; a piszkozatot a modell a megbeszélés
// azonosítójával együtt őrzi, így sosem kerülhet másik megbeszélésbe.
ColumnLayout {
    id: root

    required property var model            // MeetingNoteModel
    property string helperText: ""
    property string placeholderText: qsTr("Például: Résztvevők: Kovács Anna, Szabó Bence. A „Nordwig” helyesen: Nordvik. A „kvarc modul” helyesen: Qvarko-modul.")
    property string accessibleName: qsTr("Megjegyzés a megbeszéléshez")
    // Szűk oszlopban (az Összefoglaló jobb oldalán) a javaslat-kártyák tömörebbek.
    property bool compact: false
    property int fieldHeight: 96

    spacing: 8

    function commit() { saveTimer.stop(); root.model.commitDraft() }
    // A javaslat SZÖVEGÉVEL dolgozunk (nem indexszel): a piszkozat mentése újraszámolhatja a listát.
    function pick(text) {
        saveTimer.stop()
        if (area.text.trim() === "") root.apply(text, "replace")
        else { choice.suggestion = text; choice.open() }
    }
    function apply(text, mode) {
        root.model.applySuggestion(text, mode)   // a függő piszkozatot is menti
        area.text = root.model.note
    }

    // ---- sablon-javaslatok ----
    ColumnLayout {
        visible: root.model.suggestions.length > 0
        Layout.fillWidth: true
        Layout.bottomMargin: 4
        spacing: 6

        TLabel {
            Layout.fillWidth: true
            text: qsTr("Korábbi, hasonló megbeszélések megjegyzései — kattintásra a mezőbe kerül:")
            muted: true
            font.pixelSize: Theme.fontCaption
            wrapMode: Text.Wrap
        }
        Repeater {
            model: root.model.suggestions
            T.AbstractButton {
                id: sug
                required property var modelData
                Layout.fillWidth: true
                implicitHeight: sugCol.implicitHeight + 16
                hoverEnabled: true
                Accessible.name: qsTr("Javaslat: %1, %2").arg(modelData.title).arg(modelData.dateText)
                focusPolicy: Qt.TabFocus      // kattintásra a mező tartja a fókuszt
                onClicked: root.pick(modelData.note)

                background: Rectangle {
                    radius: Theme.radiusControl
                    color: Theme.raised
                    border.width: 1
                    border.color: sug.hovered ? Theme.borderStrong : Theme.border
                    Rectangle {
                        anchors.fill: parent
                        radius: parent.radius
                        color: Theme.stateLayer
                        opacity: sug.down ? Theme.pressedOpacity : sug.hovered ? Theme.hoverOpacity : 0
                    }
                    TFocusRing { visible: sug.visualFocus }
                }
                contentItem: Item {
                    ColumnLayout {
                        id: sugCol
                        x: 10; y: 8
                        width: sug.width - 20
                        spacing: 2
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            TIcon { name: "copy"; size: 13; color: Theme.textMuted }
                            TLabel {
                                Layout.fillWidth: true
                                text: sug.modelData.title
                                font.pixelSize: Theme.fontSmall
                                font.weight: Theme.weightMedium
                                elide: Text.ElideRight
                            }
                            TLabel {
                                visible: !root.compact
                                text: sug.modelData.dateText
                                muted: true
                                font.pixelSize: Theme.fontCaption
                            }
                        }
                        TLabel {
                            visible: root.compact
                            Layout.fillWidth: true
                            text: sug.modelData.dateText
                            muted: true
                            font.pixelSize: Theme.fontCaption
                        }
                        TLabel {
                            Layout.fillWidth: true
                            text: sug.modelData.preview
                            muted: true
                            font.pixelSize: Theme.fontSmall
                            wrapMode: Text.Wrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }

    // ---- a mező ----
    TTextArea {
        id: area
        Layout.fillWidth: true
        Layout.preferredHeight: Math.max(root.fieldHeight, implicitHeight)
        placeholderText: root.placeholderText
        Component.onCompleted: text = root.model.note
        onEditingFinished: root.commit()
        onActiveFocusChanged: if (!activeFocus) root.commit()
        // A piszkozatot a modell a megbeszélés azonosítójával együtt őrzi: a késleltetett
        // mentés így sosem kerülhet másik megbeszélésbe.
        onTextChanged: if (activeFocus) { root.model.draft(text); saveTimer.restart() }
        Accessible.name: root.accessibleName
        Timer { id: saveTimer; interval: 900; onTriggered: root.commit() }
        Connections {
            target: root.model
            function onNoteChanged() {
                if (!area.activeFocus && area.text !== root.model.note)
                    area.text = root.model.note
            }
            function onMeetingIdChanged() { saveTimer.stop(); area.text = root.model.note }
        }
    }

    TLabel {
        visible: root.helperText !== ""
        Layout.fillWidth: true
        text: root.helperText
        muted: true
        font.pixelSize: Theme.fontSmall
        wrapMode: Text.Wrap
    }

    // ---- észlelt hívás (a figyelőtől; csak tájékoztatás) ----
    RowLayout {
        visible: root.model.detectedCallApp !== ""
        Layout.fillWidth: true
        spacing: 6
        TIcon { name: "radar"; size: 13; color: Theme.textMuted }
        TLabel {
            Layout.fillWidth: true
            text: qsTr("Észlelt hívás: %1").arg(root.model.detectedCallApp)
            muted: true
            font.pixelSize: Theme.fontCaption
            elide: Text.ElideRight
        }
    }

    TDialog {
        id: choice
        property string suggestion: ""
        title: qsTr("Már van megjegyzés")
        TLabel {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: qsTr("Lecseréled a mostanit a korábbi megbeszélés megjegyzésére, vagy hozzáfűzöd a végéhez?")
        }
        actions: [
            TButton { text: qsTr("Mégse"); variant: "ghost"; onClicked: choice.reject() },
            TButton { text: qsTr("Hozzáfűzés"); onClicked: { choice.accept(); root.apply(choice.suggestion, "append") } },
            TButton { text: qsTr("Csere"); variant: "primary"; onClicked: { choice.accept(); root.apply(choice.suggestion, "replace") } }
        ]
    }
}
