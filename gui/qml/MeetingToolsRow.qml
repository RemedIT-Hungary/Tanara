import QtQuick

// Per-tab tools row below the header: 46 px, 1 px line at the bottom, 0 20 0 24 inner margin;
// the header never moves, this row swaps its content per tab. For the Transcript tab it is the
// TranscriptToolbar (which lives in the editor); the summary tabs' is loaded here from the
// `toolsRow` component (Main.qml: summaryTools). Not shown when empty.
//   MeetingToolsRow { SummaryToolsRow { ... } }
// "Empty" also means every child is hidden (an old, source-less summary: SummaryToolsRow is
// visible: false, so no empty 46 px strip remains). The row itself therefore does not hide
// (a hidden parent would hide its children, and the condition could never become true again):
// zero height, clipped.
Item {
    id: root

    default property alias content: holder.data
    readonly property bool hasContent: holder.visibleChildren.length > 0

    implicitHeight: hasContent ? 46 : 0
    clip: true

    Item {
        id: holder
        anchors { fill: parent; leftMargin: Theme.space5; rightMargin: 20; bottomMargin: 1 }
    }
    TDivider { visible: root.hasContent; anchors { left: parent.left; right: parent.right; bottom: parent.bottom } }
}
