import QtQuick

// 1 px-es elválasztó (vízszintes alapból; vertical: true → függőleges).
Rectangle {
    property bool vertical: false
    implicitWidth: vertical ? 1 : 16
    implicitHeight: vertical ? 16 : 1
    color: Theme.border
}
