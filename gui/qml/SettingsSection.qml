import QtQuick

// One jump point of a Settings page: wraps a block of the page (its children are stacked in a
// column). The window finds it by `key` (objectName "section-<key>"), scrolls to it and calls
// flash(), which outlines the block once.
//   SettingsSection { key: "folders"; width: parent.width; Column { ... } }
Item {
    id: root

    property string key: ""
    property alias spacing: body.spacing
    default property alias content: body.data

    objectName: "section-" + key
    implicitWidth: body.implicitWidth
    implicitHeight: body.implicitHeight

    function flash() { flashAnim.restart() }

    Column {
        id: body
        width: parent.width
        spacing: 16
    }

    // The jump target's frame: fades in, stays a moment, fades out.
    Rectangle {
        id: frame
        objectName: "sectionFlash"
        anchors.fill: parent
        anchors.margins: -10
        radius: Theme.radiusPopup
        color: "transparent"
        border.width: 2
        border.color: Theme.accent
        opacity: 0
        visible: opacity > 0
        SequentialAnimation {
            id: flashAnim
            NumberAnimation { target: frame; property: "opacity"; to: 1; duration: Theme.durationFast }
            PauseAnimation { duration: 600 }
            NumberAnimation { target: frame; property: "opacity"; to: 0; duration: 500; easing.type: Easing.InOutQuad }
        }
    }
}
