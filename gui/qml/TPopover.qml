import QtQuick
import QtQuick.Templates as T

// Lehorgonyzott felugró panel (személyválasztó, beszélő-popover…): raised, sugár 8, árnyék.
// A tartalom a szokásos módon gyerekként adható meg; szélességet a hívó ad (pl. 300 / 340).
//   TPopover { id: pop; width: 300; y: anchor.height + 6;  ColumnLayout { anchors.fill: parent … } }
T.Popup {
    id: control

    implicitWidth: implicitContentWidth + leftPadding + rightPadding
    implicitHeight: implicitContentHeight + topPadding + bottomPadding
    padding: 12
    margins: 8
    focus: true
    popupType: T.Popup.Item

    background: TSurface { radius: Theme.radiusPopup }

    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationFast; easing.type: Theme.easing } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationFast } }
}
