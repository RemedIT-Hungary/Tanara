import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// B04–B06 — Szolgáltatások: „Saját kulcs” vagy „Tanara Cloud” (ha a build és a beállítás
// engedi), alatta a két szolgáltató-kártya vagy a Cloud-panel / várólista-ajánlat.
Column {
    id: root

    property var vm: null
    property bool demoDropdown: false      // B04 képernyőkép: az STT-lista nyitva
    signal logoutRequested()

    readonly property string availability: vm ? vm.cloudAvailability : "none"
    readonly property bool cloudView: vm && vm.serviceMode === "cloud" && availability !== "none"

    spacing: 16

    // ---- módválasztó ----
    RowLayout {
        visible: root.availability !== "none"
        width: parent.width
        spacing: 10

        component ModeCard: T.AbstractButton {
            id: mode
            property string iconName: ""
            property string caption: ""
            property string pill: ""
            Layout.fillWidth: true
            Layout.preferredWidth: 100
            Layout.fillHeight: true
            implicitHeight: modeCol.implicitHeight + 24
            hoverEnabled: true
            activeFocusOnTab: true
            Accessible.role: Accessible.RadioButton
            Accessible.name: text
            Accessible.checked: checked
            Keys.onReturnPressed: click()
            Keys.onSpacePressed: click()

            background: Rectangle {
                radius: Theme.radiusPopup
                color: mode.checked ? Theme.raised : Theme.surface
                border.width: mode.checked ? 1.5 : 1
                border.color: mode.checked ? Theme.accent : Theme.border
                Rectangle {
                    anchors.fill: parent
                    radius: parent.radius
                    color: Theme.stateLayer
                    opacity: mode.checked ? 0 : mode.down ? Theme.pressedOpacity : mode.hovered ? Theme.hoverOpacity : 0
                }
                TFocusRing { visible: mode.visualFocus; targetRadius: Theme.radiusPopup }
            }
            contentItem: Item {
                Rectangle {
                    x: 14; y: 14
                    width: 16; height: 16; radius: 8
                    color: Theme.raised
                    border.width: mode.checked ? 5 : 1.5
                    border.color: mode.checked ? Theme.accent : Theme.borderStrong
                }
                Column {
                    id: modeCol
                    x: 40; y: 12
                    width: parent.width - 54
                    spacing: 3
                    Row {
                        spacing: 6
                        TIcon { name: mode.iconName; size: 15; anchors.verticalCenter: parent.verticalCenter }
                        TLabel {
                            text: mode.text
                            font.weight: Theme.weightSemiBold
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        TPill {
                            visible: mode.pill !== ""
                            text: mode.pill
                            tone: "accent"
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                    TLabel {
                        width: parent.width
                        text: mode.caption
                        muted: true
                        font.pixelSize: Theme.fontCaption
                        cssLineHeight: 1.4
                        wrapMode: Text.Wrap
                    }
                }
            }
        }

        ModeCard {
            text: qsTr("Saját kulcs")
            iconName: "key-round"
            caption: qsTr("A te fiókod a szolgáltatóknál; nekik fizetsz közvetlenül.")
            checked: !root.cloudView
            onClicked: root.vm.serviceMode = "own"
        }
        ModeCard {
            text: qsTr("Tanara Cloud")
            iconName: "cloud"
            caption: qsTr("Bejelentkezés és egyenleg; nincs kulcskezelés.")
            pill: root.availability === "teaser" ? qsTr("hamarosan") : ""
            checked: root.cloudView
            onClicked: root.vm.serviceMode = "cloud"
        }
    }

    // ---- saját kulcs ----
    Column {
        visible: !root.cloudView
        width: parent.width
        spacing: 16

        Rectangle {
            visible: root.vm && root.vm.focusField !== ""
            width: parent.width
            height: bannerText.implicitHeight + 20
            radius: Theme.radiusControl
            color: Theme.accentSoft
            border.width: 1
            border.color: Theme.accentLine
            TIcon { x: 12; anchors.verticalCenter: parent.verticalCenter; name: "info"; size: 15; color: Theme.accent }
            TLabel {
                id: bannerText
                x: 37; y: 10
                width: parent.width - 49
                text: root.vm ? root.vm.focusBannerText : ""
                font.pixelSize: Theme.fontSmall
                cssLineHeight: 1.45
                wrapMode: Text.Wrap
            }
        }

        SettingsProviderCard {
            objectName: "sttCard"
            width: parent.width
            z: 2                            // a lenyíló lista a következő kártya fölé kerüljön
            card: root.vm ? root.vm.stt : null
            title: qsTr("Átírás")
            subtitle: qsTr("beszédből szöveg (STT)")
            demoDropdown: root.demoDropdown
        }
        SettingsProviderCard {
            objectName: "llmCard"
            width: parent.width
            z: 1
            card: root.vm ? root.vm.llm : null
            title: qsTr("Összefoglaló")
            subtitle: qsTr("nyelvi modell (LLM)")
        }
        TLabel {
            width: parent.width
            text: qsTr("A kulcsokat a Tanara a belső adatok mappájában, csak neked olvasható fájlban tárolja; a beállítás-fájlba nem kerülnek.")
            muted: true
            font.pixelSize: Theme.fontCaption
            cssLineHeight: 1.45
            wrapMode: Text.Wrap
        }
    }

    // ---- Tanara Cloud ----
    SettingsCloudPanel {
        visible: root.cloudView && root.availability === "live"
        width: parent.width
        vm: root.vm
        onLogoutRequested: root.logoutRequested()
    }
    SettingsWaitlistPanel {
        visible: root.cloudView && root.availability === "teaser"
        width: parent.width
        vm: root.vm
    }
}
