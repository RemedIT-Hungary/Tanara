import QtQuick

// Könyvtár-állapotikon (20×20). kind: "transcript" | "summary" | "identified";
// state: "done" | "running" | "error" | "stale" | "missing". Mindig adj toolTipText-et.
//   TStatusIcon { kind: "summary"; state: "stale"; toolTipText: qsTr("Összefoglaló: elavult") }
Rectangle {
    id: root
    property string kind: "transcript"
    property string state: "missing"
    property string toolTipText: ""
    property real size: 20

    readonly property string iconName: kind === "summary" ? "sparkles"
                                     : kind === "identified" ? "user-check" : "file-text"

    implicitWidth: size
    implicitHeight: size
    radius: 4
    color: state === "done" ? Theme.successSoft
         : state === "running" ? Theme.accentSoft
         : state === "error" ? Theme.dangerSoft
         : state === "stale" ? Theme.warnSoft : "transparent"
    border.width: state === "done" ? 0 : 1
    border.color: state === "running" ? Theme.accent
                : state === "error" ? Theme.dangerLine
                : state === "stale" ? Theme.warnLine : Theme.borderStrong
    Accessible.name: toolTipText

    TIcon {
        anchors.centerIn: parent
        name: root.iconName
        size: Math.round(root.size * 0.6)
        color: root.state === "done" ? Theme.successInk
             : root.state === "running" ? Theme.accent
             : root.state === "error" ? Theme.dangerInk
             : root.state === "stale" ? Theme.warnInk : Theme.textMuted
    }
    HoverHandler { id: hover }
    TToolTip { visible: root.toolTipText !== "" && hover.hovered; text: root.toolTipText }
}
