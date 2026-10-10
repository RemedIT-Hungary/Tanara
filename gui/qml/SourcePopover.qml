import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// „Honnan jön ez?” (S1): egy állítás forrás-idézetei, 440 px. Idézetenként avatar, név, idő,
// „Meghallgatom” (a megszólalás lejátszása) és maga az idézet az átirat sorából; lábléc:
// „Ugrás az átiratba” (fül-váltás + a sorra ugrás) és „Nem így hangzott el? · Jelzem”.
//   SourcePopover { id: pop; vm: summaryVm; player: …; shell: …; meetingId: … }
//   pop.openFor("s1", chipItem)
TPopover {
    id: root

    required property var vm                // SummaryViewModel
    property var player: null
    property var shell: null
    property string meetingId: ""
    property var details: ({})              // vm.sourceDetails(id)
    property Item anchorItem: null

    readonly property string statementId: details.statementId || ""
    readonly property var quotes: details.quotes || []

    objectName: "sourcePopover"
    width: 440
    padding: 0

    // A chip alá (ha nem fér, fölé) nyílik, a chip bal széléhez igazítva.
    function openFor(statementId, anchor) {
        details = vm.sourceDetails(statementId)
        if (!details.statementId) return
        anchorItem = anchor
        parent = anchor
        x = Math.min(0, (anchor.Window.window ? anchor.Window.window.width : 1e6)
                        - anchor.mapToItem(null, 0, 0).x - width - 8)
        y = anchor.height + 6
        open()
    }
    function listen(q) {
        if (!root.player) return
        if (q.endMs > q.startMs && root.player.playRange) root.player.playRange(q.startMs, q.endMs)
        else { root.player.seek(q.startMs); root.player.play() }
    }
    function jump() {
        const ms = root.quotes.length > 0 ? root.quotes[0].startMs : -1
        if (root.shell && ms >= 0) root.shell.seekTo(root.meetingId, ms)
        close()
    }
    function flag() {
        if (!root.vm.flagStatement(root.statementId)) return
        root.details = root.vm.sourceDetails(root.statementId)
        if (root.shell) root.shell.toast(qsTr("Jelezted, hogy ez nem így hangzott el. Újrageneráláskor figyelembe veszem."))
    }

    onClosed: if (vm.activeStatementId === statementId) vm.activeStatementId = ""

    // Szöveges link-gomb (accent / halvány), opcionális ikonnal.
    component LinkButton: T.AbstractButton {
        id: link
        property string iconName: ""
        property color ink: Theme.accent
        property real iconSize: 11
        hoverEnabled: true
        activeFocusOnTab: true
        implicitHeight: 22
        implicitWidth: linkRow.implicitWidth
        font.family: Theme.fontSans
        font.pixelSize: Theme.fontCaption
        font.weight: Theme.weightMedium
        Keys.onReturnPressed: click()
        background: Item { TFocusRing { visible: link.visualFocus; targetRadius: 4 } }
        contentItem: Item {
            Row {
                id: linkRow
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4
                TIcon {
                    visible: link.iconName !== ""
                    name: link.iconName
                    size: link.iconSize
                    color: link.ink
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: link.text
                    font.family: link.font.family
                    font.pixelSize: link.font.pixelSize
                    font.weight: link.font.weight
                    font.underline: link.hovered && link.enabled
                    color: link.ink
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 14; Layout.rightMargin: 14
            Layout.topMargin: 12; Layout.bottomMargin: 8
            spacing: 8
            TLabel {
                text: qsTr("Honnan jön ez?")
                font.pixelSize: 15
                font.weight: Theme.weightSemiBold
            }
            TLabel {
                Layout.fillWidth: true
                text: root.quotes.length > 0 ? qsTr("%n helyen hangzott el", "", root.quotes.length) : ""
                muted: true
                font.pixelSize: Theme.fontCaption
                elide: Text.ElideRight
            }
        }

        // Célzott elavulás: a forrás beszélője azóta más.
        RowLayout {
            visible: (root.details.staleBecause || "") !== ""
            Layout.fillWidth: true
            Layout.leftMargin: 14; Layout.rightMargin: 14; Layout.bottomMargin: 8
            spacing: 6
            TIcon { name: "triangle-alert"; size: 13; color: Theme.warnInk }
            TLabel {
                Layout.fillWidth: true
                text: qsTr("A forrásban azóta más a beszélő: %1").arg(root.details.staleBecause || "")
                color: Theme.warnInk
                font.pixelSize: Theme.fontCaption
                wrapMode: Text.Wrap
            }
        }

        Repeater {
            model: root.quotes
            ColumnLayout {
                id: quote
                required property var modelData
                required property int index
                Layout.fillWidth: true
                spacing: 0
                TDivider { Layout.fillWidth: true }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 14; Layout.rightMargin: 14
                    Layout.topMargin: 9; Layout.bottomMargin: 9
                    spacing: 10
                    TAvatar {
                        visible: quote.modelData.colorIndex >= 0
                        Layout.alignment: Qt.AlignTop
                        size: 22
                        name: quote.modelData.name
                        speakerIndex: Math.max(0, quote.modelData.colorIndex)
                        border.width: 1.5
                    }
                    Rectangle {
                        visible: quote.modelData.colorIndex < 0
                        Layout.alignment: Qt.AlignTop
                        implicitWidth: 22; implicitHeight: 22; radius: 11
                        color: Theme.sunken
                        border.width: 1
                        border.color: Theme.borderStrong
                        TIcon { anchors.centerIn: parent; name: "user"; size: 11; color: Theme.textMuted }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            TLabel {
                                visible: text !== ""
                                Layout.maximumWidth: 200
                                text: quote.modelData.name
                                color: quote.modelData.colorIndex >= 0 ? Theme.speakerInk(quote.modelData.colorIndex) : Theme.text
                                font.pixelSize: Theme.fontSmall
                                font.weight: Theme.weightSemiBold
                                elide: Text.ElideRight
                            }
                            TLabel {
                                text: quote.modelData.stamp
                                mono: true; muted: true
                                font.pixelSize: Theme.fontMicro
                            }
                            Item { Layout.fillWidth: true }
                            LinkButton {
                                objectName: "sourceListen"
                                text: qsTr("Meghallgatom")
                                iconName: "play"
                                enabled: root.player !== null
                                onClicked: root.listen(quote.modelData)
                            }
                        }
                        TLabel {
                            Layout.fillWidth: true
                            text: quote.modelData.text !== "" ? "„" + quote.modelData.text + "”"
                                                              : qsTr("Az átirat sora nem található (újra-átírás után a hivatkozás elavulhat).")
                            muted: quote.modelData.text === ""
                            font.pixelSize: Theme.fontSmall
                            cssLineHeight: 1.5
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }
        }

        TDivider { Layout.fillWidth: true }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 14; Layout.rightMargin: 14
            Layout.topMargin: 7; Layout.bottomMargin: 7
            spacing: 12
            LinkButton {
                objectName: "sourceJump"
                text: qsTr("Ugrás az átiratba")
                iconName: "arrow-right"
                iconSize: 12
                font.pixelSize: Theme.fontSmall
                enabled: root.shell !== null && root.quotes.length > 0
                onClicked: root.jump()
            }
            Item { Layout.fillWidth: true }
            TLabel {
                text: root.details.flagged ? "" : qsTr("Nem így hangzott el?")
                visible: text !== ""
                muted: true
                font.pixelSize: Theme.fontSmall
            }
            LinkButton {
                objectName: "sourceFlag"
                text: root.details.flagged ? qsTr("Jelezve") : qsTr("Jelzem")
                iconName: root.details.flagged ? "check" : ""
                iconSize: 12
                ink: root.details.flagged ? Theme.textMuted : Theme.text
                font.pixelSize: Theme.fontSmall
                enabled: !root.details.flagged
                onClicked: root.flag()
            }
        }
    }
}
