import QtQuick
import QtQuick.Templates as T

// A címke-beviteli mező felugró listája (C02): 300 px (kompakt / felvevő: 250 px, 28 px-es
// sorok, legfeljebb 3), raised, sugár 8, árnyék. Fejléc: „LEGUTÓBB HASZNÁLT” (üres mező) vagy
// „HASONLÓ MÁR VAN” (nagyon hasonló név). Sor: # ikon, név a találat kiemelésével (warnSoft),
// darabszám mono; az „Új címke: „…”” / „Mégis új: „…”” sor + ikonnal. Alul billentyű-tipp.
// A fókusz a mezőben marad (focus: false); a billentyűket a TagInput kezeli.
TPopover {
    id: popover

    property var model: null
    property bool compact: false
    signal rowClicked(int index)

    width: compact ? 250 : 300
    padding: 4
    focus: false
    closePolicy: T.Popup.CloseOnPressOutsideParent

    contentItem: TagInputList {
        model: popover.model
        compact: popover.compact
        onRowClicked: (index) => popover.rowClicked(index)
    }
}
