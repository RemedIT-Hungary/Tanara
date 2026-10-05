import QtQuick
import QtQuick.Shapes

// A galéria „Címkék” szakasza: a handoff T01 / T01b lapja (design/handoff-tags/screenshots).
// C01: minden chip-fajta + „Nem téveszthető össze” sor · C02: a beviteli mező négy állapota (a
// lista helyben kirajzolva, felugró nélkül) · C03–C04: a fejléc címkesorának hét állapota.
// A mintaszövegek fejlesztői tartalom (mint a Gallery.qml-ben) → szándékosan nincsenek qsTr()-ben.
//   Gallery { section: "tags" }   — csak ez a szakasz
Column {
    id: root

    spacing: 28

    component SectionTitle: TLabel {
        muted: true
        font.pixelSize: Theme.fontCaption
        font.weight: Theme.weightSemiBold
        font.capitalization: Font.AllUppercase
        font.letterSpacing: Theme.labelSpacing
    }
    component Note: TLabel {
        muted: true
        font.pixelSize: Theme.fontCaption
    }
    component Caption: TLabel {
        muted: true
        font.pixelSize: Theme.fontSmall
        cssLineHeight: 1.45
        wrapMode: Text.Wrap
    }
    component VRule: Rectangle { width: 1; height: 18; color: Theme.border }
    component DashedLine: Shape {
        height: 1
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: Theme.border
            strokeWidth: 1
            strokeStyle: ShapePath.DashLine
            dashPattern: [3, 3]
            startX: 0; startY: 0.5
            PathLine { x: root.width; y: 0.5 }
        }
    }
    // Egy beviteli-mező állapot: a mező (aktív kinézettel) és alatta a lista, ahogy a felugró mutatná.
    component InputDemo: Column {
        id: demo
        property string typed: ""
        property string caption: ""
        width: (root.width - 3 * 20) / 4
        spacing: 4
        TagInput {
            id: demoInput
            width: 220
            standalone: true
            stateFocused: true
            text: demo.typed
        }
        TSurface {
            width: 300
            height: demoList.implicitHeight + 8
            TagInputList {
                id: demoList
                x: 4; y: 4
                width: parent.width - 8
                model: demoInput.effectiveModel
            }
        }
        Caption { width: parent.width; topPadding: 6; text: demo.caption }
    }
    component RowDemo: Item {
        property string caption: ""
        property string demo: ""
        property int focusedTag: -1
        width: root.width
        height: Math.max(rowCaption.implicitHeight, 26) + 17
        DashedLine { width: parent.width }
        Caption {
            id: rowCaption
            y: 9
            width: 260
            text: parent.caption
            cssLineHeight: 1.4
        }
        TagRow {
            x: 280
            y: 9 + Math.max(0, (rowCaption.implicitHeight - 26) / 2)
            width: parent.width - 280
            demoState: parent.demo
            demoFocusedTag: parent.focusedTag
        }
    }

    // ------------------------------------------------------------------ C01
    Column {
        width: root.width
        spacing: 14
        SectionTitle { text: "C01 · Címke-chip" }
        Row {
            spacing: 20
            Repeater {
                model: [
                    { kind: "applied", text: "Nordvik", hover: false, cap: "Felrakott címke. Kattintás: szűrés a könyvtárban erre a címkére." },
                    { kind: "applied", text: "Nordvik", hover: true, cap: "Rámutatva vagy fókuszban: × eltávolít (Delete)." },
                    { kind: "suggested", text: "Partnerek", hover: false, cap: "Javasolt meglévő címke. Kattintás: hozzáadás, ×: elutasítás." },
                    { kind: "llmNew", text: "Ügyfélsiker", hover: false, cap: "A nyelvi modell ötlete, új névvel: elfogadáskor a készlet bővül." },
                    { kind: "applied", text: "Gyűjteménykezelési munkacsoport 2026", hover: false, cap: "Hosszú név: legfeljebb 180 px, a teljes név tooltipben." },
                    { kind: "overflow", text: "+3", hover: false, cap: "Ami nem fér egy sorba. Kattintásra lista." }
                ]
                Column {
                    required property var modelData
                    width: (root.width - 5 * 20) / 6
                    spacing: 10
                    Item {
                        width: parent.width
                        height: 30
                        TagChip {
                            anchors.verticalCenter: parent.verticalCenter
                            kind: parent.parent.modelData.kind
                            text: parent.parent.modelData.text
                            removable: kind !== "overflow" && (kind !== "applied" || parent.parent.modelData.hover)
                            stateHovered: hovered || parent.parent.modelData.hover
                        }
                    }
                    Caption { width: parent.width; text: parent.modelData.cap }
                }
            }
        }
        Rectangle {
            width: root.width
            height: 46
            radius: Theme.radiusPopup
            color: Theme.surface
            border.width: 1
            border.color: Theme.border
            Row {
                x: 14
                anchors.verticalCenter: parent.verticalCenter
                spacing: 14
                Note { anchors.verticalCenter: parent.verticalCenter; text: "Nem téveszthető össze:"; font.pixelSize: Theme.fontSmall }
                TChip { anchors.verticalCenter: parent.verticalCenter; text: "Kovács Lilla"; speakerIndex: 0 }
                Note { anchors.verticalCenter: parent.verticalCenter; text: "személy: kerek, színes" }
                VRule { anchors.verticalCenter: parent.verticalCenter }
                TPill { anchors.verticalCenter: parent.verticalCenter; text: "elavult"; tone: "warn" }
                Note { anchors.verticalCenter: parent.verticalCenter; text: "állapot: kis, kerek" }
                VRule { anchors.verticalCenter: parent.verticalCenter }
                TagChip { anchors.verticalCenter: parent.verticalCenter; text: "Nordvik" }
                Note { anchors.verticalCenter: parent.verticalCenter; text: "címke: szögletes, semleges, „#”" }
            }
        }
    }

    // ------------------------------------------------------------------ C02
    Column {
        width: root.width
        spacing: 14
        Column {
            spacing: 21
            Rectangle { width: root.width; height: 1; color: Theme.border }
            Row {
                spacing: 10
                SectionTitle { text: "C02 · Címke-beviteli mező" }
                Note { text: "Enter hozzáad · Backspace üres mezőben az utolsót törli · ↑↓ a listában · Esc bezár" }
            }
        }
        Row {
            spacing: 20
            InputDemo { typed: ""; caption: "Üres mező: a legutóbb használt címkék." }
            InputDemo { typed: "mu"; caption: "Gépelés közben: ékezet és kis-nagybetű nem számít, az egyezés kiemelve." }
            InputDemo { typed: "Pilot 2027"; caption: "Nincs találat: Enter új címkét hoz létre." }
            InputDemo { typed: "Museum Plus"; caption: "Nagyon hasonló név: a meglévő van kijelölve, az új létrehozása is lehetséges." }
        }
    }

    // ------------------------------------------------------------------ C03–C04
    Column {
        width: root.width
        spacing: 12
        Column {
            spacing: 21
            Rectangle { width: root.width; height: 1; color: Theme.border }
            Row {
                spacing: 10
                SectionTitle { text: "C03–C04 · A megbeszélés címkesora" }
                Note { text: "a fejlécben, az adatsor alatt; mindig 26 px magas, ezért a késve érkező javaslat nem tolja el a felületet" }
            }
        }
        RowDemo { caption: "Nincs címke"; demo: "none" }
        RowDemo { caption: "1–3 címke"; demo: "few" }
        RowDemo { caption: "Sok címke: egy sor, a többi „+N”"; demo: "many" }
        RowDemo { caption: "Javaslat készül: a hely fenn van tartva"; demo: "computing" }
        RowDemo { caption: "Javaslat a hasonló megbeszélések alapján, indoklással"; demo: "similar" }
        RowDemo { caption: "Együtt járó címkék, egy címke hozzáadása után"; demo: "cooccur"; focusedTag: 0 }
        RowDemo { caption: "A nyelvi modell javaslata, az összefoglalás után"; demo: "llm" }
    }
}
