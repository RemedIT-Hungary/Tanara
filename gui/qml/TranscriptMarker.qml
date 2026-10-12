import QtQuick

// Egy sor jelölője a Javítás módban (handoff-v3 E1, 14. döntés), 18 px magas:
//   "confirmed"  pipa (megerősítve, „Jó így"), halvány, felirat nélkül
//   "short"      „rövid" + fül-ikon, körvonalas (a hang ennyiből nem dönt)
//   "uncertain"  „bizonytalan · sáv | hang | címke" + az ok ikonja, warn
//   "corrected"  „javítva", success
//   "newPerson"  „új személy" + user-plus, accent
//   "noisy"      „egymásra beszéltek", semleges
//   "ignored"    „? nem azonosítható", szürke (a hang-elemzés figyelmen kívül hagyja)
//   "sample"     tömör hullám (a döntött sor hangmintának számít), felirat nélkül, tooltip
//   "noSample"   áthúzott hullám (döntött, de nem minta), felirat nélkül, tooltip
//   TranscriptMarker { kind: "uncertain"; reason: "side" }
Rectangle {
    id: root

    property string kind: "uncertain"
    property string reason: ""              // uncertain: "side" | "voice" | "tag"
    property string toolTipText: ""

    readonly property string label: {
        switch (kind) {
        case "confirmed": return ""
        case "short": return qsTr("rövid")
        case "corrected": return qsTr("javítva")
        case "newPerson": return qsTr("új személy")
        case "noisy": return qsTr("egymásra beszéltek")
        case "ignored": return qsTr("nem azonosítható")
        case "sample":
        case "noSample": return ""
        }
        const r = reason === "side" ? qsTr("sáv") : reason === "tag" ? qsTr("címke") : reason === "voice" ? qsTr("hang") : ""
        return r === "" ? qsTr("bizonytalan") : qsTr("bizonytalan · %1").arg(r)
    }
    readonly property string iconName: {
        switch (kind) {
        case "confirmed": return "check"
        case "short": return "ear"
        case "newPerson": return "user-plus"
        case "sample":
        case "noSample": return "audio-lines"
        case "uncertain": return reason === "side" ? "arrow-right-left" : reason === "tag" ? "tag"
                               : reason === "voice" ? "audio-lines" : ""
        }
        return ""
    }
    readonly property bool wave: kind === "sample" || kind === "noSample"
    readonly property color ink: kind === "uncertain" ? Theme.warnInk
                               : kind === "corrected" || kind === "sample" ? Theme.successInk
                               : kind === "newPerson" ? Theme.accent : Theme.textMuted

    implicitHeight: 18
    implicitWidth: row.implicitWidth + (label === "" ? 4 : iconName !== "" || kind === "ignored" ? 12 : 14)
    radius: height / 2
    color: kind === "uncertain" ? Theme.warnSoft
         : kind === "corrected" ? Theme.successSoft
         : kind === "newPerson" ? Theme.accentSoft
         : kind === "noisy" || kind === "ignored" ? Theme.sunken : "transparent"
    border.width: kind === "short" ? 1 : 0
    border.color: Theme.borderStrong

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 4
        // „Nem azonosítható": kör alakú „?".
        Rectangle {
            visible: root.kind === "ignored"
            anchors.verticalCenter: parent.verticalCenter
            width: 12; height: 12; radius: 6
            color: "transparent"
            border.width: 1.25
            border.color: root.ink
            TLabel {
                anchors.centerIn: parent
                anchors.verticalCenterOffset: 0.5
                text: "?"
                color: root.ink
                font.pixelSize: 9
                font.weight: Theme.weightBold
            }
        }
        Item {
            visible: root.iconName !== ""
            anchors.verticalCenter: parent.verticalCenter
            width: icon.width
            height: icon.height
            TIcon {
                id: icon
                name: root.iconName
                size: root.kind === "confirmed" || root.wave ? 13 : 11
                color: root.kind === "noSample" ? Theme.alpha(root.ink, 0.6) : root.ink
            }
            // Áthúzott hullám: „nem minta".
            Rectangle {
                visible: root.kind === "noSample"
                anchors.centerIn: parent
                width: 15; height: 1.5
                rotation: -45
                color: root.ink
            }
        }
        TLabel {
            visible: root.label !== ""
            anchors.verticalCenter: parent.verticalCenter
            text: root.label
            color: root.ink
            font.pixelSize: Theme.fontMicro
            font.weight: Theme.weightSemiBold
        }
    }
    HoverHandler { id: hover }
    TToolTip { visible: hover.hovered && root.toolTipText !== ""; text: root.toolTipText; delay: 300 }
}
