import QtQuick
import QtQuick.Layouts

// Tanara Cloud sávok a tartalom tetején (K-08 kevés / elfogyott egyenleg, K-10 frissítés
// kell, szerver-közlemények): szint = szín + ikon + szöveg, legfeljebb egy teendő-gomb, és
// ahol lehet, elrejtő ×. A sávokat a híd adja (App.bridge.cloudBanners).
ColumnLayout {
    id: root

    property var bridge: null                // ShellBridge vagy null
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
