import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// Modális megerősítő párbeszédablak fátyollal (az M10 minta): 440 px, sugár 10, cím 18/600.
// A tartalom egy 14 px-es térközű oszlopba kerül (Layout.fillWidth-et adj a gyerekeknek);
// a gombok az `actions`-be, jobbra igazítva (a megerősítő gomb legyen az utolsó).
//   TDialog { id: dlg; title: qsTr("Újra-átírod a megbeszélést?")
//       TLabel { Layout.fillWidth: true; wrapMode: Text.Wrap; text: qsTr("…23 kézi javítás elvész…") }
//       TCheckBox { Layout.fillWidth: true; checked: true; text: qsTr("A mostani átirat maradjon meg másolatként") }
//       actions: [ TButton { text: qsTr("Mégse"); onClicked: dlg.reject() },
//                  TButton { text: qsTr("Újra-átírás"); variant: "danger"; onClicked: dlg.accept() } ]
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
