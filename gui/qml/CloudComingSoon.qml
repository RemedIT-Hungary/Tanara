import QtQuick
import QtQuick.Layouts

// "Tanara Cloud - coming soon": the one shared notice that stands in for every Tanara Cloud
// surface (the GUI offers no Cloud yet). Card form (compact: false) for pages and panels,
// one-line form (compact: true) for hints next to other controls. The link opens the product
// site in the browser.
Rectangle {
    id: root

    property bool compact: false
    readonly property string siteUrl: "https://tanara.remedit.eu"
    readonly property string siteLabel: "tanara.remedit.eu"

    objectName: "cloudComingSoon"
    implicitWidth: 420
    implicitHeight: compact ? row.implicitHeight + 16 : col.implicitHeight + 32
    radius: compact ? Theme.radiusControl : Theme.radiusPopup
    color: compact ? Theme.accentSoft : Theme.surface
    border.width: 1
    border.color: compact ? Theme.accentLine : Theme.border

    // ---- card ----
    ColumnLayout {
        id: col
        visible: !root.compact
        x: 16; y: 16
        width: parent.width - 32
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            TIcon { name: "cloud"; size: 16; color: Theme.accent }
            TLabel {
                objectName: "cloudComingSoonTitle"
                text: qsTr("Tanara Cloud — hamarosan")
                font.pixelSize: 15
                font.weight: Theme.weightSemiBold
            }
            Item { Layout.fillWidth: true }
        }
        TLabel {
            Layout.fillWidth: true
            text: qsTr("A Tanara Cloud előkészületben van. Részletek és értesítés:")
            font.pixelSize: Theme.fontSmall
            cssLineHeight: 1.45
            wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.fillWidth: true
            TButton {
                objectName: "cloudComingSoonLink"
                variant: "ghost"
                size: "small"
                text: root.siteLabel
                trailingIconName: "external-link"
                iconSize: 12
                onClicked: Qt.openUrlExternally(root.siteUrl)
            }
            Item { Layout.fillWidth: true }
        }
    }

    // ---- compact (wraps the link under the text in a narrow column) ----
    Flow {
        id: row
        visible: root.compact
        x: 12; y: 8
        width: parent.width - 20
        spacing: 8
        Item {
            width: 15; height: 24
            TIcon { name: "cloud"; size: 15; color: Theme.accent; anchors.verticalCenter: parent.verticalCenter }
        }
        TLabel {
            objectName: "cloudComingSoonTitle"
            text: qsTr("Tanara Cloud — hamarosan.")
            font.pixelSize: Theme.fontSmall
            font.weight: Theme.weightSemiBold
            height: 24
            verticalAlignment: Text.AlignVCenter
        }
        TButton {
            objectName: "cloudComingSoonLink"
            variant: "ghost"
            size: "small"
            implicitHeight: 24
            text: root.siteLabel
            trailingIconName: "external-link"
            iconSize: 12
            onClicked: Qt.openUrlExternally(root.siteUrl)
        }
    }
}
