import QtQuick

// Elevated surface: raised background + 1 px border + shadow. The background of menus, popovers
// and dialogs; also usable on its own as a "floating" panel.
Rectangle {
    id: root
    property bool elevated: true
    color: Theme.raised
    border.color: Theme.border
    border.width: 1
    radius: Theme.radiusPopup

    TShadow { visible: root.elevated; radius: root.radius }
}
