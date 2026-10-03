import QtQuick

// Kiemelt felület: raised háttér + 1 px keret + árnyék. Ez a menük, popoverek és
// párbeszédablakok háttere; önállóan is használható „lebegő” panelnek.
Rectangle {
    id: root
    property bool elevated: true
    color: Theme.raised
    border.color: Theme.border
    border.width: 1
    radius: Theme.radiusPopup

    TShadow { visible: root.elevated; radius: root.radius }
}
