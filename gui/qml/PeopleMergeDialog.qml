import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// Összevonás (P04): kereshető jelölt-lista hang-hasonlóság szerint (százalék csak akkor, ha
// mindkét félnek van hanglenyomata), a megmaradó név választása, és az eredmény valódi
// számokkal. Az összevonás nem vonható vissza — ezt a párbeszéd ki is mondja.
TDialog {
    id: dialog

    property var vm: null
    property string chosen: ""
    property bool keepSelected: true
    property var candidates: []
    property string errorText: ""
    property alias searchText: search.text

    function reload() {
        candidates = vm ? vm.mergeCandidates(search.text) : []
        // Ha a kiválasztott kiesett a szűrésből, nincs kiválasztás.
        if (chosen !== "" && !candidates.some(c => c.name === chosen)) chosen = ""
    }
    function openFor(query, preselect) {
        errorText = ""
        keepSelected = true
        chosen = ""
        search.text = query || ""
        reload()
        if (preselect) chosen = preselect
        open()
        search.forceActiveFocus()
    }

    width: Math.min(500, (parent ? parent.width : 500) - 32)
    title: vm ? qsTr("%1 összevonása").arg(vm.selectedName) : ""

    TLabel {
        Layout.fillWidth: true
        Layout.topMargin: -8
        text: qsTr("Ha ugyanaz az ember kétszer szerepel. A megbeszélések, minták és becenevek egy személyhez kerülnek.")
        muted: true
        font.pixelSize: Theme.fontSmall
        cssLineHeight: 1.45
        wrapMode: Text.Wrap
    }
    TSearchField {
        id: search
        objectName: "mergeSearch"
        Layout.fillWidth: true
        font.pixelSize: Theme.fontBody
        placeholderText: qsTr("Kivel vonod össze?")
        stateFocused: true
        onTextChanged: dialog.reload()
        Keys.onDownPressed: candidateList.forceActiveFocus()
    }
    ListView {
        id: candidateList
        objectName: "mergeCandidates"
        Layout.fillWidth: true
        Layout.topMargin: -2
        Layout.preferredHeight: Math.min(contentHeight, 4 * 46 + 6)
        visible: count > 0
        clip: true
        spacing: 2
        boundsBehavior: Flickable.StopAtBounds
        model: dialog.candidates
        T.ScrollBar.vertical: TScrollBar {}
        delegate: T.AbstractButton {
            id: cand
            required property var modelData
            readonly property bool on: dialog.chosen === modelData.name
            width: candidateList.width
            height: 44
            hoverEnabled: true
            activeFocusOnTab: true
            Accessible.role: Accessible.RadioButton
            Accessible.name: modelData.name
            Accessible.checked: on
            onClicked: dialog.chosen = modelData.name
            Keys.onReturnPressed: click()
            Keys.onSpacePressed: click()
            background: Rectangle {
                radius: Theme.radiusControl
                color: cand.on ? Theme.accentSoft : "transparent"
                border.width: 1
                border.color: cand.on ? Theme.accentLine : "transparent"
                Rectangle {
                    anchors.fill: parent
                    radius: parent.radius
                    color: Theme.stateLayer
                    opacity: cand.on ? 0 : cand.hovered ? Theme.hoverOpacity : 0
                }
                TFocusRing { visible: cand.visualFocus }
            }
            contentItem: Item {
                Rectangle {
                    id: radio
                    x: 10
                    anchors.verticalCenter: parent.verticalCenter
                    width: 16; height: 16; radius: 8
                    color: Theme.raised
                    border.width: cand.on ? 5 : 1.5
                    border.color: cand.on ? Theme.accent : Theme.borderStrong
                }
                Rectangle {
                    id: av
                    x: 36
                    anchors.verticalCenter: parent.verticalCenter
                    width: 26; height: 26; radius: 13
                    color: Theme.sunken
                    border.width: 1
                    border.color: Theme.border
                    TLabel {
                        anchors.centerIn: parent
                        text: cand.modelData.monogram
                        font.pixelSize: 10
                        font.weight: Theme.weightBold
                    }
                }
                Column {
                    x: 72
                    width: parent.width - 82
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 1
                    TLabel {
                        width: parent.width
                        text: cand.modelData.name
                        font.weight: Theme.weightSemiBold
                        elide: Text.ElideRight
                    }
                    TLabel {
                        width: parent.width
                        text: cand.modelData.meta
                        muted: true
                        font.pixelSize: Theme.fontCaption
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }
    TLabel {
        visible: candidateList.count === 0
        Layout.fillWidth: true
        text: search.text.trim() !== "" ? qsTr("Nincs ilyen személy (becenevekben is kerestem).")
                                        : qsTr("Nincs másik személy, akivel összevonható.")
        muted: true
        font.pixelSize: Theme.fontSmall
        wrapMode: Text.Wrap
    }

    Rectangle {
        visible: dialog.chosen !== ""
        Layout.fillWidth: true
        implicitHeight: keepColumn.implicitHeight + 24
        radius: Theme.radiusPopup
        color: Theme.sunken
        Column {
            id: keepColumn
            x: 12; y: 12
            width: parent.width - 24
            spacing: 10
            TSectionLabel { text: qsTr("Megmaradó név") }
            Flow {
                width: parent.width
                spacing: 20
                SettingsRadio {
                    objectName: "keepSelected"
                    width: Math.min(implicitWidth, parent.width)
                    text: dialog.vm ? dialog.vm.selectedName : ""
                    checked: dialog.keepSelected
                    onClicked: dialog.keepSelected = true
                }
                SettingsRadio {
                    objectName: "keepOther"
                    width: Math.min(implicitWidth, parent.width)
                    text: dialog.chosen
                    checked: !dialog.keepSelected
                    onClicked: dialog.keepSelected = false
                }
            }
            TLabel {
                objectName: "mergeResult"
                width: parent.width
                textFormat: Text.StyledText
                text: dialog.vm && dialog.chosen !== "" ? dialog.vm.mergeResultText(dialog.chosen, dialog.keepSelected) : ""
                font.pixelSize: Theme.fontSmall
                cssLineHeight: 1.45
                wrapMode: Text.Wrap
            }
        }
    }
    TLabel {
        visible: dialog.errorText !== ""
        Layout.fillWidth: true
        text: dialog.errorText
        color: Theme.dangerInk
        font.pixelSize: Theme.fontSmall
        wrapMode: Text.Wrap
    }

    actions: [
        TButton { text: qsTr("Mégse"); onClicked: dialog.reject() },
        TButton {
            objectName: "mergeConfirm"
            text: qsTr("Összevonás")
            variant: "primary"
            // Letiltás helyett rejtve, amíg nincs kivel (a letiltott gomb szaggatott keretet kapna).
            visible: dialog.chosen !== ""
            leftPadding: 16; rightPadding: 16
            onClicked: {
                const error = dialog.vm.merge(dialog.chosen, dialog.keepSelected)
                if (error !== "") { dialog.errorText = error; return }
                dialog.accept()
            }
        }
    ]
}
