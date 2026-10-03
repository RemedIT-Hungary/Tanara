import QtQuick
import QtQuick.Layouts

// A könyvtár-lista egy eleme: cím (14/600, egysoros), keresésnél kivonat a találattal, alatta
// meta („okt. 2. · 30 p”) és a három állapot-ikon (átirat / összefoglaló / azonosítva).
// A LibraryListModel szerepeit kapja `required property`-ként.
Item {
    id: root

    required property int index
    required property string meetingId
    required property string meta
    required property string titleBefore
    required property string titleMatch
    required property string titleAfter
    required property bool hasSnippet
    required property string snippetBefore
    required property string snippetMatch
    required property string snippetAfter
    required property int snippetMs
    required property string transcriptState
    required property string summaryState
    required property string identifyState
    required property string transcriptTip
    required property string summaryTip
    required property string identifyTip

    property bool selected: false
    property bool keyboardFocused: false
    signal activated()
    signal contextMenuRequested(real x, real y)

    implicitHeight: column.implicitHeight + 18
    Accessible.role: Accessible.ListItem
    Accessible.name: titleBefore + titleMatch + titleAfter

    Rectangle {
        anchors.fill: parent
        radius: 5
        color: root.selected ? Theme.accentSoft : "transparent"
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: Theme.stateLayer
            opacity: tap.pressed ? Theme.pressedOpacity
                   : hover.hovered && !root.selected ? Theme.hoverOpacity : 0
        }
        Rectangle {
            visible: root.keyboardFocused && root.selected
            anchors.fill: parent
            radius: parent.radius
            color: "transparent"
            border.width: 1.5
            border.color: Theme.accent
        }
    }

    ColumnLayout {
        id: column
        anchors { left: parent.left; right: parent.right; top: parent.top
                  leftMargin: 10; rightMargin: 10; topMargin: 9 }
        spacing: 4

        LibraryHighlightText {
            Layout.fillWidth: true
            before: root.titleBefore
            match: root.titleMatch
            after: root.titleAfter
            pixelSize: Theme.fontBody
            weight: Theme.weightSemiBold
            color: Theme.text
        }
        LibraryHighlightText {
            visible: root.hasSnippet
            Layout.fillWidth: true
            before: root.snippetBefore
            match: root.snippetMatch
            after: root.snippetAfter
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 2
            spacing: 5
            TLabel {
                Layout.fillWidth: true
                text: root.meta
                muted: true
                font.pixelSize: Theme.fontCaption
                elide: Text.ElideRight
                wrapMode: Text.NoWrap
            }
            TStatusIcon { kind: "transcript"; state: root.transcriptState; toolTipText: root.transcriptTip }
            TStatusIcon { kind: "summary"; state: root.summaryState; toolTipText: root.summaryTip }
            TStatusIcon { kind: "identified"; state: root.identifyState; toolTipText: root.identifyTip }
        }
    }

    HoverHandler { id: hover }
    TapHandler {
        id: tap
        acceptedButtons: Qt.LeftButton
        onTapped: root.activated()
    }
    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: (eventPoint) => root.contextMenuRequested(eventPoint.position.x, eventPoint.position.y)
    }
}
