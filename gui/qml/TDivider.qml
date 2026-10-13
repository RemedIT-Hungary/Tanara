import QtQuick

// 1 px divider (horizontal by default; vertical: true -> vertical).
Rectangle {
    property bool vertical: false
    implicitWidth: vertical ? 1 : 16
    implicitHeight: vertical ? 16 : 1
    color: Theme.border
}
