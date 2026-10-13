import QtQuick

// Text field for Settings: shows the view model's value (`value`) and reports typing through
// the `edited` signal. A change from outside (discard, restore, another provider) shows up
// even if the user has already typed in the field.
//   SettingsTextField { value: vm.userName; onEdited: (t) => vm.userName = t }
TTextField {
    id: control

    property string value: ""
    signal edited(string text)

    text: value
    // While typing, a cleared number field (0 in the model) must not be written back as "0".
    function sync() {
        if (text !== value && !(activeFocus && text.trim() === "" && value === "0")) text = value
    }
    onValueChanged: sync()
    onActiveFocusChanged: if (!activeFocus) sync()
    onTextEdited: edited(text)
}
