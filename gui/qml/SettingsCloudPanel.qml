import QtQuick
import QtQuick.Layouts

// B06 — Tanara Cloud (élő mód): fiók-sor, egyenleg + becsült órák, minőség-szintek,
// „költségbecslés indítás előtt”. Minden adat a CloudAccount-ból jön (SettingsCloudModel);
// a bejelentkezés, a feltöltés, az Expert-modell és az ÁSZF a meglévő Widgets-ablakokat nyitja.
Column {
    id: root

    property var vm: null                          // SettingsViewModel
    readonly property var cloud: vm ? vm.cloud : null
    signal logoutRequested()                       // a megerősítő ablakot a befoglaló nyitja

    spacing: 16

    // ---- fiók ----
    Rectangle {
        width: parent.width
        height: Math.max(60, accountCol.implicitHeight + 24)
        radius: Theme.radiusPopup
        color: Theme.surface
        border.width: 1
        border.color: Theme.border

        Rectangle {
            id: avatar
            x: 14
            anchors.verticalCenter: parent.verticalCenter
            width: 34; height: 34; radius: 17
            color: root.cloud && root.cloud.loggedIn ? Theme.accentSoft : Theme.sunken
            TLabel {
                visible: root.cloud && root.cloud.loggedIn
                anchors.centerIn: parent
                text: root.cloud ? Theme.monogram(root.cloud.email.split("@")[0].replace(/[._-]+/g, " ")) : ""
                color: Theme.accent
                font.pixelSize: Theme.fontCaption
                font.weight: Theme.weightBold
            }
            TIcon {
                visible: !(root.cloud && root.cloud.loggedIn)
                anchors.centerIn: parent
                name: "cloud"
                size: 16
                color: Theme.textMuted
            }
        }
        Column {
            id: accountCol
            x: 60
            width: parent.width - 60 - accountButton.width - 22
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            TLabel {
                width: parent.width
                text: root.cloud && root.cloud.loggedIn ? root.cloud.email : qsTr("Nincs bejelentkezve")
                font.weight: Theme.weightSemiBold
                elide: Text.ElideRight
            }
            TLabel {
                width: parent.width
                text: root.cloud && root.cloud.loggedIn
                    ? qsTr("Bejelentkezve · Tanara Cloud")
                    : qsTr("Egy bejelentkezés az átíráshoz és az összefoglalóhoz. Jelszó nincs: a böngészőben hagyod jóvá.")
                muted: true
                font.pixelSize: Theme.fontCaption
                cssLineHeight: 1.4
                wrapMode: Text.Wrap
            }
        }
        TButton {
            id: accountButton
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            implicitHeight: 30
            font.pixelSize: Theme.fontSmall
            variant: root.cloud && root.cloud.loggedIn ? "ghost" : "primary"
            text: root.cloud && root.cloud.loggedIn ? qsTr("Kijelentkezés") : qsTr("Bejelentkezés")
            onClicked: root.cloud.loggedIn ? root.logoutRequested() : root.cloud.login()
        }
    }

    // ---- állapot-sor (leválasztva, elfogyott, frissítés kell…) ----
    TBanner {
        id: stateBanner
        readonly property var info: root.cloud ? root.cloud.notice : ({})
        visible: info.text !== undefined && info.text !== ""
        width: parent.width
        tone: info.tone === "danger" ? "danger" : info.tone === "warn" ? "warn" : "accent"
        text: info.text || ""
        TButton {
            visible: (stateBanner.info.actionLabel || "") !== ""
            size: "small"
            text: stateBanner.info.actionLabel || ""
            onClicked: root.cloud.noticeAction()
        }
    }
    Repeater {
        model: root.cloud ? root.cloud.notices : []
        TBanner {
            id: noticeBanner
            required property var modelData
            width: root.width
            tone: modelData.level === "critical" ? "danger" : modelData.level === "warning" ? "warn" : "accent"
            text: modelData.text
            TButton {
                visible: noticeBanner.modelData.url !== ""
                size: "small"
                text: qsTr("Részletek")
                onClicked: root.cloud.openLink(noticeBanner.modelData.url)
            }
        }
    }

    // ---- egyenleg + minőség ----
    RowLayout {
        visible: root.cloud && root.cloud.loggedIn
        width: parent.width
        spacing: 10

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredWidth: 100
            Layout.fillHeight: true
            implicitHeight: balanceCol.implicitHeight + 28
            radius: Theme.radiusPopup
            color: "transparent"
            border.width: 1
            border.color: Theme.border
            Column {
                id: balanceCol
                x: 14; y: 14
                width: parent.width - 28
                spacing: 6
                TSectionLabel { text: qsTr("Egyenleg") }
                Row {
                    spacing: 8
                    TLabel {
                        text: root.cloud && root.cloud.balanceText !== "" ? root.cloud.balanceText : "…"
                        mono: true
                        font.pixelSize: 24
                        font.weight: Theme.weightMedium
                        color: root.cloud && root.cloud.balanceState === "empty" ? Theme.dangerInk : Theme.text
                    }
                    TPill {
                        visible: root.cloud && (root.cloud.balanceState === "low" || root.cloud.balanceState === "empty")
                        anchors.verticalCenter: parent.verticalCenter
                        tone: root.cloud && root.cloud.balanceState === "empty" ? "danger" : "warn"
                        text: root.cloud && root.cloud.balanceState === "empty" ? qsTr("elfogyott") : qsTr("kevés")
                    }
                }
                TLabel {
                    width: parent.width
                    text: root.cloud ? [root.cloud.hoursText, root.cloud.vatText].filter(s => s !== "").join(" · ") : ""
                    muted: true
                    font.pixelSize: Theme.fontSmall
                    cssLineHeight: 1.45
                    wrapMode: Text.Wrap
                }
                TLabel {
                    visible: root.cloud && root.cloud.hoursText !== ""
                    width: parent.width
                    text: qsTr("Az összefoglaló költsége ezen felül, a szöveg hosszától függően.")
                    muted: true
                    font.pixelSize: Theme.fontCaption
                    wrapMode: Text.Wrap
                }
                Row {
                    topPadding: 4
                    spacing: 6
                    TButton {
                        implicitHeight: 30
                        font.pixelSize: Theme.fontSmall
                        text: root.cloud ? root.cloud.topupLabel : ""
                        trailingIconName: "external-link"
                        iconSize: 13
                        onClicked: root.cloud.topup()
                    }
                    TIconButton {
                        size: "small"
                        implicitHeight: 30; implicitWidth: 30
                        iconName: "refresh-cw"
                        iconSize: 13
                        toolTipText: qsTr("Egyenleg frissítése")
                        onClicked: root.cloud.refresh()
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredWidth: 100
            Layout.fillHeight: true
            implicitHeight: tierCol.implicitHeight + 28
            radius: Theme.radiusPopup
            color: "transparent"
            border.width: 1
            border.color: Theme.border
            Column {
                id: tierCol
                x: 14; y: 14
                width: parent.width - 28
                spacing: 8
                TSectionLabel { text: qsTr("Minőség") }

                component TierBlock: Column {
                    id: tier
                    property string label: ""
                    property string kind: "stt"
                    property string value: "accurate"
                    property string expert: ""
                    property string info: ""
                    signal picked(string value)
                    width: parent.width
                    spacing: 6
                    RowLayout {
                        width: parent.width
                        TLabel {
                            Layout.fillWidth: true
                            text: tier.label
                            font.pixelSize: Theme.fontSmall
                            font.weight: Theme.weightMedium
                        }
                        TButton {
                            variant: "ghost"
                            size: "small"
                            implicitHeight: 22
                            leftPadding: 6; rightPadding: 6
                            font.pixelSize: Theme.fontCaption
                            muted: true
                            text: qsTr("Expert mód…")
                            toolTipText: qsTr("Konkrét modell választása a katalógusból")
                            onClicked: root.cloud.pickExpert(tier.kind)
                        }
                    }
                    SettingsSegmented {
                        width: parent.width
                        stretch: true
                        options: [{ value: "fast", label: qsTr("Gyors") }, { value: "accurate", label: qsTr("Pontos") }]
                        value: tier.expert !== "" ? "" : tier.value
                        onPicked: (v) => tier.picked(v)
                    }
                    TLabel {
                        visible: tier.expert !== ""
                        width: parent.width
                        text: qsTr("Expert-modell: %1").arg(tier.expert)
                        color: Theme.accent
                        font.pixelSize: Theme.fontCaption
                        font.weight: Theme.weightSemiBold
                        wrapMode: Text.Wrap
                    }
                    TLabel {
                        width: parent.width
                        text: tier.info
                        muted: true
                        font.pixelSize: Theme.fontCaption
                        cssLineHeight: 1.45
                        wrapMode: Text.Wrap
                    }
                }

                TierBlock {
                    label: qsTr("Átírás")
                    kind: "stt"
                    value: root.cloud ? root.cloud.sttTier : "accurate"
                    expert: root.cloud ? root.cloud.sttExpert : ""
                    info: root.cloud ? root.cloud.sttInfo : ""
                    onPicked: (v) => root.cloud.sttTier = v
                }
                RowLayout {
                    width: parent.width
                    spacing: 8
                    z: 2
                    TLabel {
                        text: qsTr("A megbeszélések nyelve")
                        font.pixelSize: Theme.fontCaption
                        muted: true
                    }
                    SettingsCombo {
                        Layout.fillWidth: true
                        fieldHeight: 28
                        options: root.cloud ? root.cloud.meetingLanguages : []
                        value: root.cloud ? root.cloud.meetingLanguage : ""
                        onPicked: (v) => root.cloud.meetingLanguage = v
                    }
                }
                TierBlock {
                    label: qsTr("Összefoglaló")
                    kind: "llm"
                    value: root.cloud ? root.cloud.llmTier : "accurate"
                    expert: root.cloud ? root.cloud.llmExpert : ""
                    info: root.cloud ? root.cloud.llmInfo : ""
                    onPicked: (v) => root.cloud.llmTier = v
                }
            }
        }
    }

    SettingsSwitchRow {
        visible: root.cloud && root.cloud.loggedIn
        width: parent.width
        text: qsTr("Költségbecslés minden feldolgozás előtt")
        helper: qsTr("Indítás előtt megmutatja, mennyibe kerül az átírás vagy az összefoglaló, és megerősítést kér. Kikapcsolva a feldolgozás kérdés nélkül indul.")
        checked: root.cloud ? root.cloud.estimateBeforeRun : true
        onToggled: (on) => root.cloud.estimateBeforeRun = on
    }

    RowLayout {
        visible: root.cloud && root.cloud.loggedIn
        width: parent.width
        spacing: 4
        TLabel {
            Layout.fillWidth: true
            text: root.cloud ? root.cloud.termsText : ""
            color: root.cloud && root.cloud.termsPending ? Theme.warnInk : Theme.textMuted
            font.pixelSize: Theme.fontCaption
            wrapMode: Text.Wrap
        }
        TButton {
            visible: root.cloud && root.cloud.termsPending
            variant: "ghost"; size: "small"
            text: qsTr("ÁSZF megtekintése…")
            onClicked: root.cloud.openTerms()
        }
        TButton {
            variant: "ghost"; size: "small"
            text: qsTr("Napló a weben")
            trailingIconName: "external-link"
            iconSize: 12
            onClicked: root.cloud.openUsage()
        }
        TButton {
            variant: "ghost"; size: "small"
            text: qsTr("Fiókom a weben")
            trailingIconName: "external-link"
            iconSize: 12
            onClicked: root.cloud.openDashboard()
        }
    }

    Rectangle { width: parent.width; height: 1; color: Theme.border }

    TLabel {
        width: parent.width
        topPadding: -4
        text: qsTr("A saját kulcsos beállításaid megmaradnak; bármikor visszaválthatsz. Tanara Cloud módban az átíráshoz a hangfelvétel, az összefoglalóhoz az átirat a szerverünkön át a szolgáltatóhoz megy, és a feldolgozás után töröljük. A felvétel, a hanglenyomatok és minden fájl a gépeden marad.")
        muted: true
        font.pixelSize: Theme.fontSmall
        cssLineHeight: 1.45
        wrapMode: Text.Wrap
    }
}
