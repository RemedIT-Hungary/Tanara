import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// Modal confirmation dialog with a scrim (the standard dialog pattern): 440 px, radius 10, title 18/600.
// Content goes into a column with 14 px spacing (give children Layout.fillWidth);
// buttons go into `actions`, right-aligned (the confirming button last).
//   TDialog { id: dlg; title: qsTr("Re-transcribe the meeting?")
//       TLabel { Layout.fillWidth: true; wrapMode: Text.Wrap; text: qsTr("...23 manual fixes are lost...") }
//       TCheckBox { Layout.fillWidth: true; checked: true; text: qsTr("Keep the current transcript as a copy") }
//       actions: [ TButton { text: qsTr("Cancel"); onClicked: dlg.reject() },
//                  TButton { text: qsTr("Re-transcribe"); variant: "danger"; onClicked: dlg.accept() } ]
//   }
T.Dialog {
    id: control

    property alias actions: actionRow.data

    parent: T.Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(440, (parent ? parent.width : 440) - 32)
    implicitHeight: implicitHeaderHeight + implicitContentHeight + implicitFooterHeight
                    + topPadding + bottomPadding
    leftPadding: 22; rightPadding: 22; topPadding: 14; bottomPadding: 14
    modal: true
    focus: true
    popupType: T.Popup.Item

    header: Item {
        implicitHeight: titleLabel.implicitHeight + 22
        Text {
            id: titleLabel
            x: 22; y: 22
            width: parent.width - 44
            text: control.title
            color: Theme.text
            font.family: Theme.fontSans
            font.pixelSize: 18
            font.weight: Theme.weightSemiBold
            wrapMode: Text.Wrap
        }
    }
    contentItem: ColumnLayout { spacing: 14 }
    footer: Item {
        implicitHeight: actionRow.implicitHeight + 6 + 22
        Row {
            id: actionRow
            anchors.right: parent.right
            anchors.rightMargin: 22
            y: 6
            spacing: 8
        }
    }

    background: TSurface { radius: Theme.radiusDialog }
    T.Overlay.modal: Rectangle { color: Theme.scrim }

    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationFast; easing.type: Theme.easing } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationFast } }
}
