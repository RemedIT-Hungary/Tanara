import QtQuick
import QtQuick.Layouts

// Nem-modális értesítés a tartalom alján (a régi állapotsor-üzenetek és a Tanara Cloud
// költség-értesítései helyett): szöveg, opcionálisan másolható hibaazonosító és „Napló a
// weben” hivatkozás, „Visszavonás” (undoKey: a visszavonható lépés gazdája, pl. "tags"), bezáró ×.
// Magától eltűnik; az új üzenet a régit váltja.
//   toast.show(qsTr("Elkészült az átirat"), "", "", false)
//   toast.show(qsTr("Javaslat elutasítva: #Nordvik"), "", "", false, "tags")
Item {
    id: root

    property string text: ""
    property string tone: ""                 // "" | "danger"
    property string requestId: ""
    property bool usageLink: false
    property string undoKey: ""
    property bool shown: false
    signal usageLinkActivated()
    signal undoActivated(string undoKey)

    function show(text, tone, requestId, usageLink, undoKey) {
        root.text = text
        root.tone = tone || ""
        root.requestId = requestId || ""
        root.usageLink = !!usageLink
        root.undoKey = undoKey || ""
        root.shown = true
        // Költség / hiba: tovább marad kint (van rajta teendő vagy azonosító).
        hideTimer.interval = root.requestId !== "" || root.usageLink ? 20000
                           : root.tone === "danger" ? 12000 : 6000
        hideTimer.restart()
    }
    function hide() { root.shown = false; hideTimer.stop() }

    readonly property bool danger: tone === "danger"

    visible: opacity > 0
    opacity: shown ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
    implicitWidth: row.implicitWidth + 20
    implicitHeight: row.implicitHeight + 16

    Timer { id: hideTimer; onTriggered: root.shown = false }
    // Vágólapra másolás (a QML-nek nincs közvetlen vágólap-API-ja).
    TextEdit { id: clip; visible: false }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusPopup
        color: root.danger ? Theme.dangerSoft : Theme.raised
        border.width: 1
        border.color: root.danger ? Theme.dangerLine : Theme.border
        TShadow { radius: parent.radius }
        HoverHandler { onHoveredChanged: if (root.shown) { hovered ? hideTimer.stop() : hideTimer.restart() } }
    }

    RowLayout {
        id: row
        anchors { fill: parent; leftMargin: 14; rightMargin: 6; topMargin: 8; bottomMargin: 8 }
        spacing: 10

        TIcon {
            name: root.danger ? "triangle-alert" : "circle-check"
            size: 16
            color: root.danger ? Theme.dangerInk : Theme.accent
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            TLabel {
                Layout.fillWidth: true
                Layout.maximumWidth: 480
                text: root.text
                font.pixelSize: Theme.fontSmall
                cssLineHeight: 1.4
                wrapMode: Text.Wrap
                maximumLineCount: 4
                elide: Text.ElideRight
            }
            RowLayout {
                visible: root.requestId !== ""
                spacing: 8
                TLabel {
                    text: qsTr("Hibaazonosító: %1").arg(root.requestId)
                    mono: true
                    muted: true
                    font.pixelSize: Theme.fontMicro
                }
                TButton {
                    text: qsTr("Másolás")
                    size: "small"
                    variant: "ghost"
                    iconName: "copy"
                    onClicked: { clip.text = root.requestId; clip.selectAll(); clip.copy() }
                }
            }
        }
        TButton {
            visible: root.usageLink
            text: qsTr("Napló a weben")
            trailingIconName: "external-link"
            size: "small"
            variant: "ghost"
            onClicked: root.usageLinkActivated()
        }
        TButton {
            visible: root.undoKey !== ""
            text: qsTr("Visszavonás")
            size: "small"
            variant: "ghost"
            onClicked: root.undoActivated(root.undoKey)
        }
        TIconButton {
            variant: "flat"
            size: "small"
            iconName: "x"
            iconSize: 14
            toolTipText: qsTr("Bezárás")
            onClicked: root.hide()
        }
    }
}
