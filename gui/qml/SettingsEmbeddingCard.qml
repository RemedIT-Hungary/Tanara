import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// „Beágyazás” kártya (C09, T14–T16) a Szolgáltatások lapon: a címkejavaslatok beágyazó
// modellje. Nincs (alap) / Helyi végpont / Tanara Cloud; a helyi végpont mezői (Cím, Modell +
// Lekérés, Haladó: kulcs) és kapcsolat-tesztje; a Cloud tájékoztatója; a modellváltás
// mentés előtti figyelmeztetése; alul a KÖNYVTÁR ELŐKÉSZÍTÉSE állapota.
// Az „alap” semleges választás, nem hiba: nincs figyelmeztető szín.
//   SettingsEmbeddingCard { emb: vm.embedding }
Rectangle {
    id: root

    property var emb: null                 // SettingsEmbeddingModel
    readonly property var card: emb ? emb.card : null
    readonly property string mode: emb ? emb.mode : "none"
    readonly property bool failed: emb !== null && emb.status === "failed"
    readonly property string prep: emb ? emb.prepStatus : ""

    implicitHeight: col.implicitHeight + 28
    radius: Theme.radiusPopup
    color: Theme.surface
    border.width: 1
    border.color: failed ? Theme.dangerLine : Theme.border

    // Kis szöveges link-gomb (Újraelőkészítés).
    component LinkButton: T.AbstractButton {
        id: link
        implicitWidth: linkLabel.implicitWidth + 4
        implicitHeight: 24
        hoverEnabled: true
        activeFocusOnTab: true
        Accessible.name: text
        Keys.onReturnPressed: click()
        Keys.onSpacePressed: click()
        contentItem: TLabel {
            id: linkLabel
            text: link.text
            color: link.enabled ? Theme.accent : Theme.textMuted
            font.pixelSize: Theme.fontSmall
            font.weight: Theme.weightSemiBold
            font.underline: link.hovered && link.enabled
            verticalAlignment: Text.AlignVCenter
        }
        background: Item { TFocusRing { visible: link.visualFocus; targetRadius: 4 } }
    }

    ColumnLayout {
        id: col
        x: 16; y: 14
        width: parent.width - 32
        spacing: 12

        // ---- fej ----
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            TLabel {
                text: qsTr("Beágyazás")
                font.pixelSize: 15
                font.weight: Theme.weightSemiBold
            }
            TLabel {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignBaseline
                text: qsTr("címkejavaslatokhoz (embedding)")
                muted: true
                font.pixelSize: Theme.fontCaption
                elide: Text.ElideRight
            }
            SettingsStatusPill {
                objectName: "embeddingStatus"
                status: root.emb ? root.emb.status : ""
                text: root.emb ? root.emb.statusText : ""
            }
        }

        SettingsSegmented {
            objectName: "embeddingMode"
            options: root.emb ? root.emb.modeOptions : []
            value: root.mode
            onPicked: (v) => root.emb.mode = v
        }
        TLabel {
            Layout.fillWidth: true
            text: root.emb ? root.emb.description : ""
            muted: true
            font.pixelSize: Theme.fontSmall
            cssLineHeight: 1.45
            wrapMode: Text.Wrap
        }

        // ---- helyi végpont: Cím (URL), Modell + Lekérés ----
        SettingsProviderFields {
            visible: root.mode === "local" && root.card !== null && root.card.fields.length > 0
            Layout.fillWidth: true
            card: root.card
            fields: root.card ? root.card.fields : []
        }
        TLabel {
            visible: root.mode === "local" && root.card !== null && root.card.fetchError !== ""
            Layout.fillWidth: true
            text: root.card ? root.card.fetchError : ""
            color: Theme.dangerInk
            font.pixelSize: Theme.fontCaption
            wrapMode: Text.Wrap
        }
        SettingsProviderFields {
            visible: root.mode === "local" && root.card !== null && root.card.advancedOpen
            Layout.fillWidth: true
            card: root.card
            fields: root.card ? root.card.advancedFields : []
        }

        // ---- a kapcsolat-teszt hibája ----
        Rectangle {
            visible: root.mode === "local" && root.card !== null && root.card.testState === "failed"
            Layout.fillWidth: true
            implicitHeight: testErr.implicitHeight + 16
            radius: Theme.radiusControl
            color: Theme.dangerSoft
            border.width: 1
            border.color: Theme.dangerLine
            RowLayout {
                id: testErr
                x: 10; y: 8
                width: parent.width - 20
                spacing: 10
                TLabel {
                    Layout.fillWidth: true
                    text: root.card ? root.card.errorText : ""
                    font.pixelSize: Theme.fontSmall
                    cssLineHeight: 1.4
                    wrapMode: Text.Wrap
                }
                TLabel {
                    text: root.card ? root.card.errorCode : ""
                    mono: true; muted: true
                    font.pixelSize: 11
                }
            }
        }
        TLabel {
            visible: root.mode === "local" && root.card !== null && root.card.warningText !== ""
            Layout.fillWidth: true
            text: root.card ? root.card.warningText : ""
            color: Theme.warnInk
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.Wrap
        }

        // ---- Tanara Cloud: mi megy fel, mennyibe kerül ----
        Rectangle {
            objectName: "embeddingCloudInfo"
            visible: root.mode === "cloud"
            Layout.fillWidth: true
            implicitHeight: cloudText.implicitHeight + 24
            radius: Theme.radiusControl
            color: Theme.accentSoft
            border.width: 1
            border.color: Theme.accentLine
            TIcon { x: 12; y: 14; name: "info"; size: 15; color: Theme.accent }
            TLabel {
                id: cloudText
                x: 37; y: 12
                width: parent.width - 49
                text: root.emb ? root.emb.cloudInfo : ""
                font.pixelSize: Theme.fontSmall
                cssLineHeight: 1.45
                wrapMode: Text.Wrap
            }
        }
        TLabel {
            visible: text !== ""
            Layout.fillWidth: true
            text: root.emb ? root.emb.cloudHint : ""
            muted: true
            font.pixelSize: Theme.fontSmall
            cssLineHeight: 1.45
            wrapMode: Text.Wrap
        }

        // ---- modellváltás: mentés ELŐTT ----
        Rectangle {
            objectName: "embeddingRestartWarning"
            visible: root.emb !== null && root.emb.restartPending
            Layout.fillWidth: true
            implicitHeight: warnText.implicitHeight + 24
            radius: Theme.radiusControl
            color: Theme.warnSoft
            border.width: 1
            border.color: Theme.warnLine
            TIcon { x: 12; y: 14; name: "triangle-alert"; size: 15; color: Theme.warnInk }
            TLabel {
                id: warnText
                x: 37; y: 12
                width: parent.width - 49
                text: root.emb ? root.emb.restartWarning : ""
                font.pixelSize: Theme.fontSmall
                cssLineHeight: 1.45
                wrapMode: Text.Wrap
            }
        }

        // ---- KÖNYVTÁR ELŐKÉSZÍTÉSE ----
        ColumnLayout {
            objectName: "embeddingPrep"
            visible: root.emb !== null && root.emb.prepVisible
            Layout.fillWidth: true
            spacing: 10

            TDivider { Layout.fillWidth: true }
            TSectionLabel { text: qsTr("Könyvtár előkészítése") }

            // Még nincs elmentve a választás.
            TLabel {
                visible: root.prep === "pending"
                Layout.fillWidth: true
                text: qsTr("Mentés után indul: a meglévő átiratokat egyszer előkészítjük, utána az újakat magától.")
                muted: true
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.Wrap
            }

            // Fut: számláló, hátralévő idő, 6 px-es sáv, Megszakítás.
            RowLayout {
                visible: root.prep === "running"
                Layout.fillWidth: true
                spacing: 16
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        TLabel {
                            Layout.fillWidth: true
                            text: root.emb ? root.emb.countText : ""
                            font.pixelSize: Theme.fontSmall
                            elide: Text.ElideRight
                        }
                        TLabel {
                            text: root.emb ? root.emb.etaText : ""
                            muted: true
                            font.pixelSize: Theme.fontSmall
                        }
                    }
                    TProgressBar {
                        Layout.fillWidth: true
                        thickness: 6
                        value: root.emb ? root.emb.progress : 0
                    }
                }
                TButton {
                    objectName: "embeddingCancel"
                    size: "small"
                    text: qsTr("Megszakítás")
                    onClicked: root.emb.cancelPreparation()
                }
            }
            TLabel {
                visible: root.prep === "running"
                Layout.fillWidth: true
                text: qsTr("Közben dolgozhatsz; a már előkészített megbeszéléseknél a javaslatok azonnal jobbak.")
                muted: true
                font.pixelSize: Theme.fontCaption
                cssLineHeight: 1.4
                wrapMode: Text.Wrap
            }

            // Megállt: hiba + Folytatás.
            Rectangle {
                visible: root.prep === "error"
                Layout.fillWidth: true
                implicitHeight: errRow.implicitHeight + 16
                radius: Theme.radiusControl
                color: Theme.dangerSoft
                border.width: 1
                border.color: Theme.dangerLine
                RowLayout {
                    id: errRow
                    x: 10; y: 8
                    width: parent.width - 20
                    spacing: 10
                    TLabel {
                        Layout.fillWidth: true
                        text: root.emb ? root.emb.errorText : ""
                        color: Theme.dangerInk
                        font.pixelSize: Theme.fontSmall
                        cssLineHeight: 1.4
                        wrapMode: Text.Wrap
                    }
                    TButton {
                        objectName: "embeddingResume"
                        size: "small"
                        text: qsTr("Folytatás")
                        // Mentetlen modellváltásnál a régi beállítással folytatna: előbb mentés.
                        enabled: root.emb !== null && !root.emb.restartPending
                        onClicked: root.emb.resumePreparation()
                    }
                }
            }

            // Naprakész.
            RowLayout {
                visible: root.prep === "done"
                Layout.fillWidth: true
                spacing: 8
                TIcon { name: "circle-check"; size: 15; color: Theme.successInk }
                TLabel {
                    Layout.fillWidth: true
                    text: root.emb ? root.emb.doneText : ""
                    font.pixelSize: Theme.fontSmall
                    elide: Text.ElideRight
                }
                LinkButton {
                    objectName: "embeddingReprepare"
                    text: qsTr("Újraelőkészítés")
                    enabled: root.emb !== null && !root.emb.restartPending
                    onClicked: root.emb.reprepare()
                }
            }

            // Nem fut (félbehagyva / még el sem indult): Előkészítés.
            RowLayout {
                visible: root.prep === "idle"
                Layout.fillWidth: true
                spacing: 8
                TLabel {
                    Layout.fillWidth: true
                    text: root.emb ? qsTr("%1 előkészítve").arg(root.emb.countText) : ""
                    font.pixelSize: Theme.fontSmall
                    elide: Text.ElideRight
                }
                TButton {
                    objectName: "embeddingStart"
                    size: "small"
                    text: qsTr("Előkészítés")
                    enabled: root.emb !== null && !root.emb.restartPending
                    onClicked: root.emb.startPreparation()
                }
            }
        }

        // ---- műveletek (helyi végpont) ----
        RowLayout {
            visible: root.mode === "local" && root.card !== null && root.card.testable
            Layout.fillWidth: true
            spacing: 8
            TButton {
                objectName: "embeddingTest"
                implicitHeight: 30
                leftPadding: 12; rightPadding: 12
                font.pixelSize: Theme.fontSmall
                iconName: "plug"
                iconSize: 13
                text: qsTr("Kapcsolat tesztelése")
                enabled: root.card !== null && root.card.testState !== "testing"
                onClicked: root.card.test()
            }
            T.AbstractButton {
                id: advToggle
                visible: root.card !== null && root.card.advancedFields.length > 0
                implicitWidth: advRow.implicitWidth + 12
                implicitHeight: 30
                hoverEnabled: true
                activeFocusOnTab: true
                Accessible.name: qsTr("Haladó")
                onClicked: root.card.advancedOpen = !root.card.advancedOpen
                Keys.onReturnPressed: click()
                Keys.onSpacePressed: click()
                background: Item { TFocusRing { visible: advToggle.visualFocus } }
                contentItem: Item {
                    Row {
                        id: advRow
                        x: 6
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 6
                        TIcon {
                            name: root.card && root.card.advancedOpen ? "chevron-down" : "chevron-right"
                            size: 13
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        TLabel {
                            text: qsTr("Haladó")
                            font.pixelSize: Theme.fontSmall
                            font.weight: Theme.weightMedium
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }
            }
            Item { Layout.fillWidth: true }
        }
    }
}
