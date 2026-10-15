import QtQuick
import QtQuick.Layouts

// Settings > Tanara Cloud (S4): the account and the account-level tier. Which step runs on the
// Cloud is decided on the Processing page ("Hol fusson?"); this page only decides how it runs.
//  - teaser / off: the "coming soon" card, and the tier block disabled with a "hamarosan" note
//    (nothing can be changed or saved);
//  - live: "Fiók" (signed in: e-mail, balance, VAT label, the low-balance warning, "Feltöltés",
//    sign-out; signed out: "Bejelentkezés" with the device code), then "Szint": Egyszerű / Haladó,
//    Gyors / Pontos with the catalog's price, and at Haladó three switches.
// The Pro level (provider and model per step) is not in the client: it is the web account page's.
Column {
    id: root

    property var vm: null
    readonly property bool live: root.vm !== null && root.vm.cloudMode === "live"
    readonly property bool editable: root.vm !== null && root.vm.cloudEditable
    readonly property var account: root.vm ? root.vm.cloudAccount : ({})
    readonly property bool advanced: root.vm !== null && root.vm.cloudLevel === "advanced"

    objectName: "settingsCloudPage"
    spacing: 22

    // A Gyors / Pontos choice: a selectable card with the price on the right.
    component TierChoice: Rectangle {
        id: choice
        property string value: ""
        property string title: ""
        property string detail: ""
        property string price: ""
        readonly property bool selected: root.vm !== null && root.vm.cloudTier === value

        objectName: "cloudTier-" + value
        Layout.fillWidth: true
        Layout.preferredWidth: 1
        implicitHeight: choiceCol.implicitHeight + 24
        radius: Theme.radiusPopup
        color: selected ? Theme.accentSoft : Theme.surface
        border.width: selected ? 1.5 : 1
        border.color: selected ? Theme.accent : Theme.border
        opacity: root.editable ? 1 : 0.55
        activeFocusOnTab: root.editable
        Accessible.role: Accessible.RadioButton
        Accessible.name: choice.title
        Accessible.checked: choice.selected
        Keys.onSpacePressed: if (root.editable) root.vm.cloudTier = choice.value
        Keys.onReturnPressed: if (root.editable) root.vm.cloudTier = choice.value

        TapHandler {
            enabled: root.editable
            onTapped: { choice.forceActiveFocus(); root.vm.cloudTier = choice.value }
        }
        HoverHandler { enabled: root.editable; cursorShape: Qt.PointingHandCursor }
        TFocusRing { visible: choice.activeFocus; targetRadius: Theme.radiusPopup }

        ColumnLayout {
            id: choiceCol
            x: 14; y: 12
            width: parent.width - 28
            spacing: 4
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                SettingsRadio {
                    checked: choice.selected
                    enabled: root.editable
                    focusPolicy: Qt.NoFocus
                    onClicked: root.vm.cloudTier = choice.value
                }
                TLabel { text: choice.title; font.weight: Theme.weightSemiBold }
                Item { Layout.fillWidth: true }
                TLabel {
                    visible: choice.price !== ""
                    text: choice.price
                    mono: true
                    muted: true
                    font.pixelSize: Theme.fontCaption
                }
            }
            TLabel {
                Layout.fillWidth: true
                text: choice.detail
                muted: true
                font.pixelSize: Theme.fontSmall
                cssLineHeight: 1.45
                wrapMode: Text.Wrap
            }
        }
    }

    // ---- not live: the shared "coming soon" card ----
    CloudComingSoon {
        visible: !root.live
        width: parent.width
    }

    // ---- live: the account ----
    Column {
        objectName: "cloudAccountBlock"
        visible: root.live
        width: parent.width
        spacing: 12

        TSectionLabel { text: qsTr("Fiók") }

        TCard {
            width: parent.width
            ColumnLayout {
                anchors.fill: parent
                spacing: 12

                // Signed in: who, the balance, top-up and sign-out.
                RowLayout {
                    objectName: "cloudSignedIn"
                    visible: root.vm !== null && root.vm.cloudSignedIn
                    Layout.fillWidth: true
                    spacing: 12
                    TAvatar { name: root.account.email || ""; size: 36 }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        TLabel { text: qsTr("Bejelentkezve"); font.weight: Theme.weightSemiBold }
                        TLabel {
                            Layout.fillWidth: true
                            text: root.account.email || ""
                            muted: true
                            font.pixelSize: Theme.fontSmall
                            elide: Text.ElideRight
                        }
                    }
                    ColumnLayout {
                        spacing: 2
                        TLabel { Layout.alignment: Qt.AlignRight; text: qsTr("Keret"); muted: true; font.pixelSize: Theme.fontCaption }
                        TLabel {
                            objectName: "cloudBalance"
                            Layout.alignment: Qt.AlignRight
                            text: root.account.valid ? root.account.balance : "—"
                            font.pixelSize: Theme.fontHeading
                            font.weight: Theme.weightSemiBold
                            color: root.account.balanceEmpty ? Theme.dangerInk : root.account.lowBalance ? Theme.warnInk : Theme.text
                        }
                        TLabel {
                            visible: (root.account.vatLabel || "") !== ""
                            Layout.alignment: Qt.AlignRight
                            text: root.account.vatLabel || ""
                            muted: true
                            font.pixelSize: Theme.fontCaption
                        }
                    }
                }
                RowLayout {
                    visible: root.vm !== null && root.vm.cloudSignedIn
                    Layout.fillWidth: true
                    spacing: 8
                    TLabel {
                        Layout.fillWidth: true
                        text: root.account.hoursText || ""
                        muted: true
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.Wrap
                    }
                    TButton {
                        objectName: "cloudTopUpButton"
                        text: qsTr("Feltöltés")
                        variant: root.account.lowBalance || root.account.balanceEmpty ? "primary" : "secondary"
                        size: "small"
                        trailingIconName: "external-link"
                        iconSize: 12
                        onClicked: root.vm.cloudTopUp()
                    }
                    TButton {
                        objectName: "cloudSignOutButton"
                        text: qsTr("Kijelentkezés")
                        variant: "ghost"
                        size: "small"
                        onClicked: root.vm.cloudSignOut()
                    }
                }

                // Signed out: the device-code sign-in.
                RowLayout {
                    objectName: "cloudSignedOut"
                    visible: root.vm !== null && !root.vm.cloudSignedIn && !root.vm.cloudSigningIn
                    Layout.fillWidth: true
                    spacing: 12
                    TLabel {
                        Layout.fillWidth: true
                        text: qsTr("Nem vagy bejelentkezve. A bejelentkezés a böngészőben történik; a Tanara egy kódot mutat, amelyet ott megadsz.")
                        muted: true
                        font.pixelSize: Theme.fontSmall
                        cssLineHeight: 1.45
                        wrapMode: Text.Wrap
                    }
                    TButton {
                        objectName: "cloudSignInButton"
                        text: qsTr("Bejelentkezés")
                        variant: "primary"
                        onClicked: root.vm.cloudSignIn()
                    }
                }
                ColumnLayout {
                    objectName: "cloudSigningIn"
                    visible: root.vm !== null && root.vm.cloudSigningIn
                    Layout.fillWidth: true
                    spacing: 8
                    TLabel {
                        Layout.fillWidth: true
                        text: qsTr("Nyisd meg a bejelentkező oldalt, és add meg ezt a kódot:")
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.Wrap
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        TLabel {
                            text: root.account.signInCode || ""
                            mono: true
                            font.pixelSize: Theme.fontHeading
                            font.weight: Theme.weightSemiBold
                        }
                        Item { Layout.fillWidth: true }
                        TButton {
                            text: qsTr("Bejelentkező oldal")
                            size: "small"
                            trailingIconName: "external-link"
                            iconSize: 12
                            onClicked: root.vm.cloudOpenSignInPage()
                        }
                        TButton {
                            text: qsTr("Mégse")
                            variant: "ghost"
                            size: "small"
                            onClicked: root.vm.cloudCancelSignIn()
                        }
                    }
                }

                TLabel {
                    objectName: "cloudError"
                    visible: (root.account.error || "") !== ""
                    Layout.fillWidth: true
                    text: root.account.error || ""
                    color: Theme.dangerInk
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.Wrap
                }
            }
        }

        TBanner {
            objectName: "cloudLowBalance"
            visible: root.vm !== null && root.vm.cloudSignedIn && (root.account.lowBalance || root.account.balanceEmpty)
            width: parent.width
            tone: root.account.balanceEmpty ? "danger" : "warn"
            text: root.account.balanceEmpty
                  ? qsTr("Elfogyott a keret: a Tanara Cloud addig nem indít új feldolgozást, amíg fel nem töltöd.")
                  : qsTr("Kevés a keret. Egy hosszabb felvétel feldolgozása elakadhat; érdemes feltölteni.")
        }
    }

    Rectangle { width: parent.width; height: 1; color: Theme.border }

    // ---- the account-level tier ----
    Column {
        objectName: "cloudTierBlock"
        width: parent.width
        spacing: 12

        RowLayout {
            width: parent.width
            spacing: 14
            TSectionLabel { text: qsTr("Szint") }
            SettingsSegmented {
                objectName: "cloudLevel"
                options: [{ value: "simple", label: qsTr("Egyszerű"), enabled: root.editable },
                          { value: "advanced", label: qsTr("Haladó"), enabled: root.editable }]
                value: root.vm ? root.vm.cloudLevel : "simple"
                onPicked: (v) => root.vm.cloudLevel = v
            }
            Item { Layout.fillWidth: true }
            TPill {
                visible: !root.editable
                tone: "accent"
                text: qsTr("hamarosan")
            }
        }
        TLabel {
            width: parent.width
            text: root.advanced
                  ? qsTr("Ugyanaz a két szint, plusz néhány kapcsoló arra, hogyan fussanak a lépések.")
                  : qsTr("Két szint közül választasz; a szolgáltatót és a modellt mi választjuk, és frissítjük, ha jobb jön.")
            muted: true
            font.pixelSize: Theme.fontSmall
            cssLineHeight: 1.45
            wrapMode: Text.Wrap
        }

        RowLayout {
            width: parent.width
            spacing: 12
            TierChoice {
                value: "fast"
                title: qsTr("Gyors")
                detail: qsTr("Gyors átírás, rövidebb összefoglaló. Olcsóbb, hosszú felvételekhez is jó.")
                price: root.vm ? (root.vm.cloudTierOptions[0].price || "") : ""
            }
            TierChoice {
                value: "accurate"
                title: qsTr("Pontos")
                detail: qsTr("A legjobb átírás beszélőkkel, részletes összefoglaló. Ajánlott.")
                price: root.vm ? (root.vm.cloudTierOptions[1].price || "") : ""
            }
        }
        TLabel {
            objectName: "cloudTierMixed"
            visible: root.vm !== null && root.vm.cloudTier === "mixed"
            width: parent.width
            text: qsTr("Az átírás és az összefoglaló most eltérő szinten fut; a választás mindkettőt beállítja.")
            muted: true
            font.pixelSize: Theme.fontCaption
            wrapMode: Text.Wrap
        }

        // Haladó: the switches.
        TCard {
            objectName: "cloudAdvancedSwitches"
            visible: root.advanced
            width: parent.width
            opacity: root.editable ? 1 : 0.55
            Column {
                width: parent.width
                spacing: 14
                SettingsSwitchRow {
                    width: parent.width
                    enabled: root.editable
                    text: qsTr("Költségbecslés minden futás előtt")
                    helper: qsTr("Indítás előtt megmutatjuk a várható költséget, és megerősítést kérünk.")
                    checked: root.vm ? root.vm.cloudEstimateBeforeRun : true
                    onToggled: (on) => root.vm.cloudEstimateBeforeRun = on
                }
                SettingsSwitchRow {
                    width: parent.width
                    enabled: root.editable
                    text: qsTr("Hosszú felvételnél Gyors szint")
                    helper: qsTr("%n percnél hosszabb felvétel a Gyors szinten fut, a választott szinttől függetlenül.", "",
                                 root.vm ? root.vm.cloudLongRecordingMinutes : 90)
                    checked: root.vm ? root.vm.cloudFastForLongRecordings : false
                    onToggled: (on) => root.vm.cloudFastForLongRecordings = on
                }
                SettingsSwitchRow {
                    width: parent.width
                    enabled: root.editable
                    text: qsTr("A feltöltött hang törlése a feldolgozás után")
                    helper: qsTr("A Tanara Cloud nem tartja meg a felvételt, amint az átírás elkészült.")
                    checked: root.vm ? root.vm.cloudDeleteUploadedAudio : true
                    onToggled: (on) => root.vm.cloudDeleteUploadedAudio = on
                }
            }
        }
    }

    Rectangle { width: parent.width; height: 1; color: Theme.border }

    // ---- where vs. how ----
    RowLayout {
        width: parent.width
        spacing: 8
        TLabel {
            Layout.fillWidth: true
            text: qsTr("Melyik lépés menjen a Cloudra, azt a Feldolgozás oldalon döntöd el; itt csak az dől el, hogyan fusson.")
            muted: true
            font.pixelSize: Theme.fontSmall
            cssLineHeight: 1.45
            wrapMode: Text.Wrap
        }
        TButton {
            objectName: "cloudToProcessing"
            text: qsTr("Feldolgozás")
            variant: "ghost"
            size: "small"
            trailingIconName: "arrow-right"
            iconSize: 12
            onClicked: root.vm.openPage("processing")
        }
    }
}
