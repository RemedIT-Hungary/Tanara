import QtQuick

// Fülenkénti eszköz-sor a fejléc alatt (design/handoff-v3, 3. döntés): 46 px, alul 1 px vonal,
// 0 20 0 24 belső margó; a fejléc sosem mozdul, ez a sor cseréli a tartalmát fülenként.
// Az Átirat fülé a TranscriptToolbar (a szerkesztőben él); az összefoglaló fülekét az U4
// `toolsRow` komponense tölti ide (Main.qml: summaryTools). Üresen nem látszik.
//   MeetingToolsRow { SummaryToolsRow { … } }
Item {
    id: root

    default property alias content: holder.data
    readonly property bool hasContent: holder.children.length > 0

    visible: hasContent
    implicitHeight: hasContent ? 46 : 0

    Item {
        id: holder
        anchors { fill: parent; leftMargin: Theme.space5; rightMargin: 20; bottomMargin: 1 }
    }
    TDivider { anchors { left: parent.left; right: parent.right; bottom: parent.bottom } }
}
