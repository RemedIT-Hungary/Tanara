import QtQuick

// A speaker's voiceprint, explained: whether there is one, how much usable speech the speaker has
// in this meeting, and one can be created from here. The same block is in the overview fingerprint
// mark's own panel and (compact layout) in the whole-speaker scope of the "Who said it?" panel.
// Rule: a voiceprint is created ONLY on an explicit button press; the one just created can be
// undone here (exactly that one is deleted). If voice analysis is unavailable, the block says so
// once and offers no button.
Item {
    id: root

    property var editor: null               // TranscriptEditorViewModel
    property string speakerKey: ""
    // Compact: the button at the header row's right edge with a short label (at the bottom of the
    // "Who said it?" panel); otherwise its own panel: with the name, the button at the bottom, full label.
    property bool compact: false

    // Filled by reset() / refresh(); updates after actions.
    property var info: ({})
    property var material: ({})
    property string createdPrintId: ""      // the voiceprint just created (here)
    property string resultText: ""
    property bool resultError: false

    // The voiceprint state changed (created / undone): the caller can refresh its own.
    signal changed()

    readonly property bool named: info.anonymous === false
    readonly property bool has: info.hasVoiceprint === true
    readonly property bool supported: material.supported === true
    readonly property bool sufficient: material.sufficient === true
    readonly property bool canCreate: named && supported && sufficient && createdPrintId === ""
    readonly property bool hasAction: canCreate || createdPrintId !== ""
    readonly property real actionWidth: createButton.visible ? createButton.width
                                      : undoButton.visible ? undoButton.width : 0

    implicitHeight: body.height + 22 + (!compact && hasAction ? 36 : 0)
    height: visible ? implicitHeight : 0

    function refresh() {
        info = editor ? editor.speakerInfo(speakerKey) : ({})
        material = editor ? editor.voiceprintMaterial(speakerKey) : ({})
    }
    function reset() {
        createdPrintId = ""
        resultText = ""
        resultError = false
        refresh()
    }
    function create() {
        const result = editor.createVoiceprint(speakerKey)
        resultError = result.ok !== true
        createdPrintId = result.ok === true ? result.printId : ""
        resultText = result.ok === true
                     ? qsTr("Elkészült %n sorból, %1 mp beszédből.", "", result.usedLines).arg(result.usedSec)
                     : (result.message || "")
        refresh()
        changed()
    }
    function undoCreate() {
        editor.removeVoiceprint(createdPrintId)
        createdPrintId = ""
        resultError = false
        resultText = qsTr("A most készült hanglenyomat törölve.")
        refresh()
        changed()
    }

    TDivider { visible: root.compact; width: parent.width }

    Rectangle {
        x: 12; y: root.compact ? 9 : 11
        width: 22; height: 22; radius: 11
        color: root.has ? Theme.successSoft : Theme.sunken
        TIcon {
            anchors.centerIn: parent
            name: "fingerprint"
            size: 14
            color: root.has ? Theme.successInk : Theme.textMuted
        }
    }

    Column {
        id: body
        x: 44; y: 11
        width: root.width - 58
        spacing: 4

        TLabel {
            objectName: "voiceprintName"
            visible: !root.compact
            width: parent.width
            text: root.info.name || ""
            muted: true
            elide: Text.ElideRight
            font.pixelSize: Theme.fontCaption
            font.weight: Theme.weightSemiBold
        }
        Item {
            width: parent.width
            // In the compact layout the button sits in this row: leave room below it.
            height: Math.max(stateLabel.height, root.compact && root.hasAction ? 26 : 0)
            TLabel {
                id: stateLabel
                objectName: "voiceprintState"
                width: parent.width - (root.compact && root.hasAction ? root.actionWidth + 8 : 0)
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSmall
                font.weight: Theme.weightSemiBold
                text: !root.named ? qsTr("Névtelen beszélő")
                    : root.has ? qsTr("Van hanglenyomata") : qsTr("Még nincs hanglenyomata")
            }
        }
        TLabel {
            objectName: "voiceprintDetail"
            width: parent.width
            wrapMode: Text.Wrap
            muted: true
            font.pixelSize: Theme.fontSmall
            cssLineHeight: 1.4
            text: {
                if (!root.named)
                    return qsTr("Hanglenyomat csak elnevezett beszélőhöz készíthető. Előbb add meg, ki ő.")
                if (!root.supported)
                    return root.material.reason === "audio"
                        ? qsTr("A megbeszélés lekevert hangja nem érhető el, ezért itt most nem készíthető hanglenyomat.")
                        : qsTr("Nincs letöltve a hangmodell, ezért itt most nem készíthető hanglenyomat.")
                const lines = root.material.usableLines || 0
                if (root.sufficient)
                    return qsTr("Ebből a megbeszélésből %n hosszabb sora használható fel (%1 mp beszéd).", "", lines)
                               .arg(root.material.usableSec)
                const have = lines > 0
                    ? qsTr("Ebből a megbeszélésből csak %n hosszabb sora használható (%1 mp beszéd).", "", lines)
                          .arg(root.material.usableSec)
                    : qsTr("Ebben a megbeszélésben nincs elég hosszú, jól használható sora.")
                return have + " " + qsTr("Még kb. %1 mp tiszta beszéd kellene: segít, ha több, legalább "
                                         + "3 másodperces sorát rendeled hozzá.").arg(root.material.missingSec)
            }
        }
        TLabel {
            objectName: "voiceprintResult"
            visible: root.resultText !== ""
            width: parent.width
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSmall
            font.weight: Theme.weightMedium
            cssLineHeight: 1.4
            color: root.resultError ? Theme.dangerInk : root.createdPrintId !== "" ? Theme.successInk : Theme.text
            text: root.resultText
        }
    }

    // The action: compact at the header row's right edge, otherwise below the text.
    TButton {
        id: createButton
        objectName: "voiceprintCreate"
        visible: root.canCreate
        x: root.compact ? root.width - width - 12 : body.x
        y: root.compact ? 8 : body.y + body.height + 8
        size: "small"
        variant: root.has || root.compact ? "secondary" : "primary"
        iconName: root.compact ? "" : "fingerprint"
        text: root.compact ? (root.has ? qsTr("Új minta") : qsTr("Készítés"))
                           : (root.has ? qsTr("Új minta készítése") : qsTr("Hanglenyomat készítése"))
        toolTipText: qsTr("Hanglenyomat készül a beszélő itteni, hosszabb soraiból — ebből ismeri fel a "
                          + "program a következő megbeszéléseken")
        onClicked: root.create()
    }
    TButton {
        id: undoButton
        objectName: "voiceprintUndo"
        visible: root.createdPrintId !== ""
        x: createButton.x
        y: createButton.y
        size: "small"
        iconName: root.compact ? "" : "undo-2"
        text: qsTr("Visszavonás")
        toolTipText: qsTr("A most készült hanglenyomat törlése")
        onClicked: root.undoCreate()
    }
}
