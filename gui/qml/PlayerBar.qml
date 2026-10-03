import QtQuick

// HELYKITÖLTŐ — Lejátszó: lejátszás/szünet, idő, kereső-sáv, sebesség, hangerő
// A szeletet építő agent ennek a fájlnak a TELJES tartalmát lecseréli (a fájlnév marad).
Item {
    implicitHeight: Theme.playerHeight
    TPlaceholder {
        anchors.fill: parent
        label: "PlayerBar.qml"
        note: "Lejátszó: lejátszás/szünet, idő, kereső-sáv, sebesség, hangerő"
    }
}
