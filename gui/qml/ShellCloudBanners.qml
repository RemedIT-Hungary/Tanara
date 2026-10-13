import QtQuick
import QtQuick.Layouts

// Tanara Cloud bars at the top of the content (low / empty balance, update required, server
// notices): level = color + icon + text, at most one action button, and a dismiss x where
// possible. The bridge provides the bars (App.bridge.cloudBanners).
ColumnLayout {
    id: root

    property var bridge: null                // ShellBridge or null
    // [{ key, level: "critical"|"warning"|"info", text, cta, closable }]
    property var banners: bridge ? bridge.cloudBanners : []

    visible: banners.length > 0
    spacing: 6

    Repeater {
        model: root.banners
        TBanner {
            id: banner
            required property var modelData
            Layout.fillWidth: true
            tone: modelData.level === "critical" ? "danger" : modelData.level === "warning" ? "warn" : "accent"
            text: modelData.text
            TButton {
                visible: banner.modelData.cta !== ""
                text: banner.modelData.cta
                size: "small"
                onClicked: if (root.bridge) root.bridge.cloudBannerAction(banner.modelData.key)
            }
            TIconButton {
                visible: banner.modelData.closable
                variant: "flat"
                size: "small"
                iconName: "x"
                iconSize: 14
                toolTipText: qsTr("Elrejtés")
                onClicked: if (root.bridge) root.bridge.cloudBannerDismiss(banner.modelData.key)
            }
        }
    }
}
