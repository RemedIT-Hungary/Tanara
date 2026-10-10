import QtQuick

// Egy Átnézendő csoport kártyája (handoff-v3 E2): 28 px-es ikon-csempe · cím 14/600 + mono
// darabszám · alcím + bizonyíték-chipek · döntés: „Mind a N → X" (egy visszavonási lépés),
// „Egyenként" (a csoport sorai a kártya alatt, soronkénti gombokkal), „Kihagyom". A tájékoztató
// csoport (rövid sorok) semleges, egyetlen „Megmutatom" gombbal.
Rectangle {
    id: root

    property string groupId: ""
    property string iconName: "audio-lines"
    property string title: ""
    property string subtitle: ""
    property int lineCount: 0
    property var evidence: []
    property string proposedName: ""
    property bool actionable: true
    property bool canApply: false
    property bool expanded: false
    property bool active: false
    // „az" a magánhangzóval kezdődő számnevek előtt (1, 5, 50…): a hívó adja (vm.needsAz).
    property bool needsAz: false

    signal applyRequested()
    signal expandToggled()
    signal skipRequested()

    implicitHeight: column.implicitHeight + 22
    radius: Theme.radiusPopup
    color: actionable ? Theme.surface : "transparent"
    border.width: 1
    border.color: active ? Theme.accentLine : Theme.border

    Rectangle {
        id: tile
        x: 14; y: 11
        width: 28; height: 28
        radius: Theme.radiusControl
        color: root.actionable ? Theme.accentSoft : Theme.sunken
        TIcon {
            anchors.centerIn: parent
            name: root.iconName
            size: 15
            color: root.actionable ? Theme.accent : Theme.textMuted
        }
    }

    Column {
        id: column
        x: tile.x + tile.width + 12
        y: 11
        width: root.width - x - 14
        spacing: 6

        Row {
            width: parent.width
            spacing: 10
            TLabel {
                id: titleLabel
                objectName: "reviewTitle"
                width: Math.min(implicitWidth, parent.width - countLabel.implicitWidth - 10)
                text: root.title
                elide: Text.ElideRight
                font.pixelSize: Theme.fontBody
                font.weight: Theme.weightSemiBold
            }
            TLabel {
                id: countLabel
                anchors.baseline: titleLabel.baseline
                text: qsTr("%n sor", "", root.lineCount)
                mono: true
                muted: true
                font.pixelSize: Theme.fontCaption
                font.weight: Theme.weightMedium
            }
        }
        Flow {
            width: parent.width
            spacing: 6
            TLabel {
                text: root.subtitle
                visible: text !== ""
                muted: true
                font.pixelSize: Theme.fontSmall - 0.5
                height: 20
                verticalAlignment: Text.AlignVCenter
                width: Math.min(implicitWidth, parent.width)
                elide: Text.ElideRight
            }
            Repeater {
                model: root.evidence
                EvidenceChip {
                    required property var modelData
                    kind: modelData.kind === "manual" ? "side" : modelData.kind
                    polarity: modelData.polarity === "support" ? "neutral" : modelData.polarity
                    side: modelData.side || ""
                    text: modelData.text
                    HoverHandler { id: evHover }
                    TToolTip { visible: evHover.hovered && (modelData.detail || "") !== ""; text: modelData.detail || ""; delay: 300 }
                }
            }
        }
        Row {
            topPadding: 2
            spacing: 6
            TButton {
                focusPolicy: Qt.TabFocus     // kattintásra a fókusz a szerkesztőé marad (B, 1, Ctrl+Z)
                objectName: "reviewApply"
                visible: root.canApply
                size: "small"
                variant: "primary"
                text: (root.needsAz ? qsTr("Mind az %1 → %2") : qsTr("Mind a %1 → %2")).arg(root.lineCount).arg(root.proposedName)
                toolTipText: qsTr("A csoport minden sora ide kerül: %1 — egy lépésben visszavonható (Ctrl+Z)").arg(root.proposedName)
                onClicked: root.applyRequested()
            }
            TButton {
                focusPolicy: Qt.TabFocus     // kattintásra a fókusz a szerkesztőé marad (B, 1, Ctrl+Z)
                objectName: root.actionable ? "reviewOneByOne" : "reviewShow"
                size: "small"
                text: root.actionable ? (root.expanded ? qsTr("Összecsukás") : qsTr("Egyenként"))
                                      : (root.expanded ? qsTr("Elrejtem") : qsTr("Megmutatom"))
                onClicked: root.expandToggled()
            }
            TButton {
                focusPolicy: Qt.TabFocus     // kattintásra a fókusz a szerkesztőé marad (B, 1, Ctrl+Z)
                objectName: "reviewSkip"
                visible: root.actionable
                size: "small"
                variant: "ghost"
                text: qsTr("Kihagyom")
                toolTipText: qsTr("Most nem foglalkozom vele: a csoport eltűnik a listából (a sorok maradnak, ahogy vannak)")
                onClicked: root.skipRequested()
            }
        }
    }
}
