import QtQuick
import QtQuick.Layouts

// Tanara Cloud — „hamarosan” (teaser-mód): a várólista-ajánlat az új vizuális nyelven.
// Adatot csak a gomb megnyomásakor küld (CloudAccount::joinWaitlist); a feliratkozás ténye
// azonnal mentődik, nem a piszkozat része.
Rectangle {
    id: root

    property var vm: null
    readonly property var cloud: vm ? vm.cloud : null
    readonly property bool joined: cloud && cloud.joinedEmail !== ""

    implicitHeight: col.implicitHeight + 32
    radius: Theme.radiusPopup
    color: Theme.surface
    border.width: 1
    border.color: Theme.border

    ColumnLayout {
        id: col
        x: 16; y: 16
        width: parent.width - 32
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            TLabel {
                text: qsTr("Tanara Cloud")
                font.pixelSize: 15
                font.weight: Theme.weightSemiBold
            }
            TPill { text: qsTr("hamarosan"); tone: "accent" }
            Item { Layout.fillWidth: true }
        }
        TLabel {
            Layout.fillWidth: true
            text: qsTr("Átírás és összefoglaló saját API-kulcsok nélkül: bejelentkezel, és működik. Havidíj nélkül, csak a használatért fizetsz, a feltöltött egyenleg nem jár le. A saját kulcsos mód ingyenes marad, és továbbra is így működik.")
            font.pixelSize: Theme.fontSmall
            cssLineHeight: 1.45
            wrapMode: Text.Wrap
        }

        // ---- feliratkozott ----
        Rectangle {
            visible: root.joined
            Layout.fillWidth: true
            implicitHeight: joinedCol.implicitHeight + 20
            radius: Theme.radiusControl
            color: Theme.successSoft
            Column {
                id: joinedCol
                x: 12; y: 10
                width: parent.width - 24
                spacing: 4
                Row {
                    spacing: 8
                    TIcon { name: "circle-check"; size: 15; color: Theme.successInk; anchors.verticalCenter: parent.verticalCenter }
                    TLabel {
                        text: qsTr("Feliratkoztál: %1").arg(root.cloud ? root.cloud.joinedEmail : "")
                        color: Theme.successInk
                        font.pixelSize: Theme.fontSmall
                        font.weight: Theme.weightSemiBold
                    }
                }
                TLabel {
                    width: parent.width
                    text: qsTr("Küldtünk egy megerősítő e-mailt. A feliratkozás a benne lévő linkre kattintva él; az indulásról ide írunk.")
                    color: Theme.successInk
                    font.pixelSize: Theme.fontSmall
                    cssLineHeight: 1.45
                    wrapMode: Text.Wrap
                }
            }
        }
        TButton {
            visible: root.joined
            variant: "ghost"; size: "small"
            text: qsTr("Másik címmel iratkozom fel")
            onClicked: root.cloud.resetWaitlist()
        }

        // ---- űrlap ----
        GridLayout {
            visible: !root.joined
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 12
            rowSpacing: 8
            TLabel { Layout.preferredWidth: 130; text: qsTr("E-mail cím"); font.pixelSize: Theme.fontSmall; font.weight: Theme.weightMedium }
            SettingsTextField {
                Layout.fillWidth: true
                implicitHeight: 32
                font.pixelSize: Theme.fontSmall
                placeholderText: qsTr("nev@example.com")
                inputMethodHints: Qt.ImhEmailCharactersOnly
                value: root.cloud ? root.cloud.waitEmail : ""
                hasError: root.cloud && root.cloud.waitError !== ""
                Accessible.name: qsTr("E-mail cím")
                onEdited: (t) => root.cloud.waitEmail = t
            }
            TLabel { Layout.preferredWidth: 130; text: qsTr("Mire használnád?"); font.pixelSize: Theme.fontSmall; font.weight: Theme.weightMedium }
            SettingsCombo {
                Layout.fillWidth: true
                fieldHeight: 32
                options: root.cloud ? root.cloud.useCases : []
                value: root.cloud ? root.cloud.waitUseCase : ""
                onPicked: (v) => root.cloud.waitUseCase = v
            }
            TLabel {
                Layout.preferredWidth: 130
                Layout.alignment: Qt.AlignTop
                text: qsTr("A megbeszélések nyelve")
                font.pixelSize: Theme.fontSmall
                font.weight: Theme.weightMedium
                wrapMode: Text.Wrap
            }
            Row {
                spacing: 14
                Repeater {
                    model: [{ code: "hu", label: qsTr("Magyar") }, { code: "en", label: qsTr("Angol") },
                            { code: "other", label: qsTr("Egyéb") }]
                    TCheckBox {
                        required property var modelData
                        text: modelData.label
                        font.pixelSize: Theme.fontSmall
                        checked: root.cloud && root.cloud.waitLanguages.indexOf(modelData.code) >= 0
                        onToggled: {
                            root.cloud.toggleWaitLanguage(modelData.code)
                            checked = Qt.binding(function() {
                                return root.cloud && root.cloud.waitLanguages.indexOf(modelData.code) >= 0 })
                        }
                    }
                }
            }
        }
        TCheckBox {
            visible: !root.joined
            Layout.fillWidth: true
            text: qsTr("Értesítést kérek a Tanara Cloud indulásáról. Bármikor leiratkozhatok.")
            font.pixelSize: Theme.fontSmall
            checked: root.cloud ? root.cloud.waitConsent : false
            onToggled: {
                root.cloud.waitConsent = checked
                checked = Qt.binding(function() { return root.cloud ? root.cloud.waitConsent : false })
            }
        }
        TLabel {
            visible: !root.joined && root.cloud && root.cloud.waitError !== ""
            Layout.fillWidth: true
            text: root.cloud ? root.cloud.waitError : ""
            color: Theme.dangerInk
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.Wrap
        }
        RowLayout {
            visible: !root.joined
            Layout.fillWidth: true
            spacing: 8
            TButton {
                variant: "primary"
                text: root.cloud && root.cloud.waitBusy ? qsTr("Küldés…") : qsTr("Értesítést kérek")
                enabled: root.cloud && root.cloud.waitCanSubmit
                onClicked: root.cloud.submitWaitlist()
            }
            TButton {
                variant: "ghost"; size: "small"
                text: qsTr("Adatkezelési tájékoztató")
                trailingIconName: "external-link"
                iconSize: 12
                onClicked: root.cloud.openLink(root.cloud.privacyUrl)
            }
            Item { Layout.fillWidth: true }
        }
        TLabel {
            visible: !root.joined
            Layout.fillWidth: true
            text: qsTr("Adatot csak a gombra kattintva küldünk: az e-mail címed, a válaszaid, a platform és a Tanara verziója. Megerősítő e-mailt kapsz; csak a megerősített cím kerül a listára.")
            muted: true
            font.pixelSize: Theme.fontCaption
            cssLineHeight: 1.45
            wrapMode: Text.Wrap
        }
    }
}
