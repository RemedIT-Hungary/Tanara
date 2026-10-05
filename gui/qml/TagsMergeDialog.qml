import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// Címkék összevonása (T11): 500 px. Jelöltek: előbb a hasonló nevűek, aztán a gyakran együtt
// járók; „MEGMARADÓ NÉV” rádiógombok; az eredmény valódi számokkal („16 megbeszélés (1 közös)”),
// és hogy a megszűnő név beírásakor a mező a megmaradót ajánlja. Visszavonható (Ctrl+Z / toast).
TDialog {
    id: dialog

    property var vm: null
    property string tagId: ""
    property string chosen: ""
    property bool keepSelected: true
    property var candidates: []
    property string errorText: ""

    readonly property string tagName: vm && tagId !== "" ? vm.tagName(tagId) : ""
    readonly property string chosenName: vm && chosen !== "" ? vm.tagName(chosen) : ""

    function openFor(id, preselect) {
        tagId = id
        errorText = ""
        keepSelected = true
        candidates = vm ? vm.mergeCandidates(id) : []
        chosen = preselect ? preselect : candidates.length > 0 ? candidates[0].id : ""
        open()
    }

    width: Math.min(500, (parent ? parent.width : 500) - 32)
    title: qsTr("#%1 összevonása").arg(tagName)

    TLabel {
        Layout.fillWidth: true
        Layout.topMargin: -8
        text: qsTr("Ha ugyanazt jelenti két név. A megbeszélések egy címke alá kerülnek, a profilok összeadódnak.")
        muted: true
        font.pixelSize: Theme.fontSmall
        cssLineHeight: 1.45
        wrapMode: Text.Wrap
    }

    Column {
        Layout.fillWidth: true
        visible: dialog.candidates.length > 0
        spacing: 2
        Repeater {
            model: dialog.candidates
            T.AbstractButton {
                id: cand
                required property var modelData
                readonly property bool on: dialog.chosen === modelData.id
                objectName: "mergeCandidate"
                width: parent.width
                height: 50
                hoverEnabled: true
                activeFocusOnTab: true
                Accessible.role: Accessible.RadioButton
                Accessible.name: modelData.name
                Accessible.checked: on
                onClicked: dialog.chosen = modelData.id
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
                    TLabel {
                        id: hash
                        x: 36
                        anchors.verticalCenter: parent.verticalCenter
                        text: "#"
                        mono: true
                        muted: true
                        font.pixelSize: Theme.fontSmall
                    }
                    Column {
                        x: 54
                        width: parent.width - 64
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
    }
    TLabel {
        visible: dialog.candidates.length === 0
        Layout.fillWidth: true
        text: qsTr("Nincs hasonló nevű vagy gyakran együtt járó címke.")
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
                    text: dialog.tagName
                    checked: dialog.keepSelected
                    onClicked: dialog.keepSelected = true
                }
                SettingsRadio {
                    objectName: "keepOther"
                    width: Math.min(implicitWidth, parent.width)
                    text: dialog.chosenName
                    checked: !dialog.keepSelected
                    onClicked: dialog.keepSelected = false
                }
            }
            TLabel {
                objectName: "mergeResult"
                width: parent.width
                textFormat: Text.StyledText
                text: dialog.vm && dialog.chosen !== ""
                      ? (dialog.keepSelected ? dialog.vm.mergeResultText(dialog.chosen, dialog.tagId)
                                             : dialog.vm.mergeResultText(dialog.tagId, dialog.chosen))
                      : ""
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
                const error = dialog.keepSelected ? dialog.vm.merge(dialog.chosen, dialog.tagId)
                                                  : dialog.vm.merge(dialog.tagId, dialog.chosen)
                if (error !== "") { dialog.errorText = error; return }
                dialog.accept()
            }
        }
    ]
}
