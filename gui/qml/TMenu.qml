import QtQuick
import QtQuick.Templates as T

// Popup menu (raised, radius 8, shadow). Items: TMenuItem, TMenuSeparator.
//   TMenu { id: more
//       TMenuItem { text: qsTr("Rename"); iconName: "pencil"; onTriggered: ... }
//       TMenuSeparator {}
//       TMenuItem { text: qsTr("Delete..."); iconName: "trash-2"; danger: true }
//   }
//   TIconButton { iconName: "ellipsis"; onClicked: more.popup(this, 0, height + 4) }
T.Menu {
    id: control

    implicitWidth: {
        let w = 180
        for (let i = 0; i < count; ++i) {
            const it = itemAt(i)
            if (it) w = Math.max(w, it.implicitWidth)
        }
        return w + leftPadding + rightPadding
    }
    implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
    padding: 4
    margins: 8
    overlap: 1
    popupType: T.Popup.Item

    delegate: TMenuItem {}

    contentItem: ListView {
        implicitHeight: contentHeight
        model: control.contentModel
        interactive: false
        clip: true
        currentIndex: control.currentIndex
    }
    background: TSurface { radius: Theme.radiusPopup }

    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationFast; easing.type: Theme.easing } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationFast } }
}
