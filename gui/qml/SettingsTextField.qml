import QtQuick

// Szövegmező a Beállításokhoz: a nézetmodell értékét mutatja (`value`), a gépelést az
// `edited` jelben adja vissza. A kívülről jövő változás (eldobás, visszaállítás, másik
// szolgáltató) akkor is megjelenik, ha a felhasználó már írt a mezőbe.
//   SettingsTextField { value: vm.userName; onEdited: (t) => vm.userName = t }
TTextField {
    id: control

    property string value: ""
    signal edited(string text)

    text: value
    // Gépelés közben a kiürített szám-mező (a modellben 0) ne íródjon vissza "0"-ként.
    function sync() {
        if (text !== value && !(activeFocus && text.trim() === "" && value === "0")) text = value
    }
    onValueChanged: sync()
    onActiveFocusChanged: if (!activeFocus) sync()
    onTextEdited: edited(text)
}
