import QtQuick

// "default" mark on the system's default device (recorder + Settings "Recording").
Rectangle {
    width: defText.implicitWidth + 10
    height: 15
    radius: 6
    color: "transparent"
    border.width: 1
    border.color: Theme.borderStrong
    TLabel {
        id: defText
        anchors.centerIn: parent
        text: qsTr("alapért.")
        muted: true
        font.pixelSize: 10
        font.weight: Theme.weightSemiBold
    }
}
