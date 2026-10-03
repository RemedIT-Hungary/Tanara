import QtQuick
import QtQuick.Templates as T

// Legördülő választó (Beállítások). Két fajtája:
//   választó:      SettingsCombo { options: [{ value, label, tag? }]; value: …; onPicked: (v) => … }
//   szerkeszthető: SettingsCombo { editable: true; options: ["a", "b"]; value: …; onPicked: … }
//                  — szabad szöveg is beírható (modellnév, nyelv); a lista csak segít.
// A lista a mező alatt nyílik (raised, sugár 8, árnyék), elemei 38 px magasak; az opcionális
// `tag` zöld pirulaként áll a sor végén.
Item {
    id: root

    property var options: []
    property string value: ""
    property string placeholder: ""
    property bool editable: false
    property bool mono: false
    property bool hasError: false
    property int fieldHeight: Theme.controlHeight
    property bool demoOpen: false          // képernyőképhez: nyitva jelenik meg
    readonly property alias popupOpen: popup.opened
    signal picked(string value)

    implicitWidth: 260
    implicitHeight: fieldHeight

    function optValue(o) { return typeof o === "string" ? o : o.value }
    function optLabel(o) { return typeof o === "string" ? o : o.label }
    function optTag(o) { return typeof o === "string" ? "" : (o.tag || "") }
    readonly property string currentLabel: {
        for (let i = 0; i < options.length; ++i)
            if (optValue(options[i]) === value) return optLabel(options[i])
        return value
    }
    function openList() { if (root.options.length > 0) popup.open() }

    Component.onCompleted: if (demoOpen) Qt.callLater(openList)
    // A gépelés megszakítja a `text: value` kötést: a kívülről jövő változást (eldobás,
    // visszaállítás) így vezetjük vissza a mezőbe.
    onValueChanged: if (field.text !== value) field.text = value

    // ---- választó (nem szerkeszthető) ----
    T.AbstractButton {
        id: button
        visible: !root.editable
        anchors.fill: parent
        hoverEnabled: true
        activeFocusOnTab: true
        Accessible.role: Accessible.ComboBox
        Accessible.name: root.currentLabel
        onClicked: popup.opened ? popup.close() : root.openList()
        Keys.onReturnPressed: click()
        Keys.onSpacePressed: click()
        Keys.onDownPressed: root.openList()

        background: Rectangle {
            radius: Theme.radiusControl
            color: root.enabled ? Theme.raised : Theme.sunken
            border.width: 1
            border.color: root.hasError ? Theme.danger
                        : (button.visualFocus || popup.opened) ? Theme.accent
                        : root.enabled ? Theme.borderStrong : Theme.border
            TFocusRing {
                visible: button.visualFocus || popup.opened
                border.color: Theme.accentSoft
            }
        }
        contentItem: Item {
            TLabel {
                x: 10
                width: parent.width - 10 - 30
                anchors.verticalCenter: parent.verticalCenter
                text: root.currentLabel !== "" ? root.currentLabel : root.placeholder
                color: root.currentLabel !== "" && root.enabled ? Theme.text : Theme.textMuted
                mono: root.mono
                font.pixelSize: root.mono || root.fieldHeight < 34 ? Theme.fontSmall : Theme.fontBody
                elide: Text.ElideRight
            }
            TIcon {
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                name: "chevron-down"
                size: 15
                color: Theme.textMuted
            }
        }
    }

    // ---- szerkeszthető ----
    TTextField {
        id: field
        visible: root.editable
        anchors.fill: parent
        implicitHeight: root.fieldHeight
        rightPadding: 34
        mono: root.mono
        hasError: root.hasError
        placeholderText: root.placeholder
        text: root.value
        font.pixelSize: root.mono || root.fieldHeight < 34 ? Theme.fontSmall : Theme.fontBody
        onTextEdited: root.picked(text)
        Keys.onDownPressed: root.openList()

        T.AbstractButton {
            id: chevron
            anchors.right: parent.right
            width: 32; height: parent.height
            visible: root.options.length > 0
            hoverEnabled: true
            focusPolicy: Qt.NoFocus
            Accessible.name: qsTr("Lista megnyitása")
            onClicked: popup.opened ? popup.close() : root.openList()
            contentItem: Item {
                TIcon {
                    anchors.centerIn: parent
                    name: "chevron-down"
                    size: 15
                    color: chevron.hovered ? Theme.text : Theme.textMuted
                }
            }
        }
    }

    T.Popup {
        id: popup
        y: root.height + 4
        width: root.width
        implicitHeight: Math.min(list.contentHeight + 8, 6 * 38 + 8)
        padding: 4
        margins: 8
        focus: true
        popupType: T.Popup.Item
        closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutsideParent

        background: TSurface { radius: Theme.radiusPopup }
        contentItem: ListView {
            id: list
            clip: true
            model: root.options
            boundsBehavior: Flickable.StopAtBounds
            T.ScrollBar.vertical: TScrollBar {}
            delegate: T.ItemDelegate {
                id: item
                required property var modelData
                required property int index
                readonly property bool current: root.optValue(modelData) === root.value
                width: list.width
                height: 38
                hoverEnabled: true
                Accessible.name: root.optLabel(modelData)
                onClicked: {
                    popup.close()
                    if (!current) root.picked(root.optValue(modelData))
                }
                background: Rectangle {
                    radius: 5
                    color: item.hovered || item.current || (root.demoOpen && item.index === 0)
                         ? Theme.sunken : "transparent"
                }
                contentItem: Item {
                    TLabel {
                        x: 10
                        width: parent.width - 20 - (tag.visible ? tag.width + 10 : 0)
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.optLabel(item.modelData)
                        mono: root.mono
                        font.pixelSize: root.mono ? Theme.fontSmall : Theme.fontBody
                        font.weight: item.current ? Theme.weightSemiBold : Theme.weightRegular
                        elide: Text.ElideRight
                    }
                    TPill {
                        id: tag
                        visible: text !== ""
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.optTag(item.modelData)
                        tone: "success"
                    }
                }
            }
        }
        enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationFast } }
        exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationFast } }
    }
}
