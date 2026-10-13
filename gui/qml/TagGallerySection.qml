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
        SectionTitle { text: DemoText.t("C01 · Címke-chip", "C01 · Tag chip") }
        Row {
            spacing: 20
            Repeater {
                model: [
                    { kind: "applied", text: "Nordvik", hover: false, cap: DemoText.t("Felrakott címke. Kattintás: szűrés a könyvtárban erre a címkére.", "Applied tag. Click: filter the library by this tag.") },
                    { kind: "applied", text: "Nordvik", hover: true, cap: DemoText.t("Rámutatva vagy fókuszban: × eltávolít (Delete).", "On hover or focus: × removes (Delete).") },
                    { kind: "suggested", text: DemoText.t("Partnerek", "Partners"), hover: false, cap: DemoText.t("Javasolt meglévő címke. Kattintás: hozzáadás, ×: elutasítás.", "Suggested existing tag. Click: add, ×: dismiss.") },
                    { kind: "llmNew", text: DemoText.t("Ügyfélsiker", "Customer success"), hover: false, cap: DemoText.t("A nyelvi modell ötlete, új névvel: elfogadáskor a készlet bővül.", "The language model's idea, with a new name: accepting it extends the tag set.") },
                    { kind: "applied", text: DemoText.t("Gyűjteménykezelési munkacsoport 2026", "Collections management working group 2026"), hover: false, cap: DemoText.t("Hosszú név: legfeljebb 180 px, a teljes név tooltipben.", "Long name: at most 180 px, the full name in a tooltip.") },
                    { kind: "overflow", text: "+3", hover: false, cap: DemoText.t("Ami nem fér egy sorba. Kattintásra lista.", "Whatever doesn't fit in one row. Click for a list.") }
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
                Note { anchors.verticalCenter: parent.verticalCenter; text: DemoText.t("Nem téveszthető össze:", "Not to be confused with:"); font.pixelSize: Theme.fontSmall }
                TChip { anchors.verticalCenter: parent.verticalCenter; text: DemoText.name("Kovács Lilla"); speakerIndex: 0 }
                Note { anchors.verticalCenter: parent.verticalCenter; text: DemoText.t("személy: kerek, színes", "person: round, coloured") }
                VRule { anchors.verticalCenter: parent.verticalCenter }
                TPill { anchors.verticalCenter: parent.verticalCenter; text: DemoText.t("elavult", "outdated"); tone: "warn" }
                Note { anchors.verticalCenter: parent.verticalCenter; text: DemoText.t("állapot: kis, kerek", "status: small, round") }
                VRule { anchors.verticalCenter: parent.verticalCenter }
                TagChip { anchors.verticalCenter: parent.verticalCenter; text: "Nordvik" }
                Note { anchors.verticalCenter: parent.verticalCenter; text: DemoText.t("címke: szögletes, semleges, „#”", "tag: square, neutral, “#”") }
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
                SectionTitle { text: DemoText.t("C02 · Címke-beviteli mező", "C02 · Tag input field") }
                Note { text: DemoText.t("Enter hozzáad · Backspace üres mezőben az utolsót törli · ↑↓ a listában · Esc bezár", "Enter adds · Backspace in an empty field removes the last one · ↑↓ in the list · Esc closes") }
            }
        }
        Row {
            spacing: 20
            InputDemo { typed: ""; caption: DemoText.t("Üres mező: a legutóbb használt címkék.", "Empty field: the recently used tags.") }
            InputDemo { typed: "mu"; caption: DemoText.t("Gépelés közben: ékezet és kis-nagybetű nem számít, az egyezés kiemelve.", "While typing: accents and case don't matter, the match is highlighted.") }
            InputDemo { typed: "Pilot 2027"; caption: DemoText.t("Nincs találat: Enter új címkét hoz létre.", "No match: Enter creates a new tag.") }
            InputDemo { typed: "Muzeeo"; caption: DemoText.t("Nagyon hasonló név: a meglévő van kijelölve, az új létrehozása is lehetséges.", "Very similar name: the existing one is selected, creating a new one is still possible.") }
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
                SectionTitle { text: DemoText.t("C03–C04 · A megbeszélés címkesora", "C03–C04 · The meeting's tag row") }
                Note { text: DemoText.t("a fejlécben, az adatsor alatt; mindig 26 px magas, ezért a késve érkező javaslat nem tolja el a felületet", "in the header, below the data row; always 26 px tall, so a late suggestion doesn't shift the layout") }
            }
        }
        RowDemo { caption: DemoText.t("Nincs címke", "No tags"); demo: "none" }
        RowDemo { caption: DemoText.t("1–3 címke", "1–3 tags"); demo: "few" }
        RowDemo { caption: DemoText.t("Sok címke: egy sor, a többi „+N”", "Many tags: one row, the rest “+N”"); demo: "many" }
        RowDemo { caption: DemoText.t("Javaslat készül: a hely fenn van tartva", "Suggestion in progress: the space is reserved"); demo: "computing" }
        RowDemo { caption: DemoText.t("Javaslat a hasonló megbeszélések alapján, indoklással", "Suggestion based on similar meetings, with reasons"); demo: "similar" }
        RowDemo { caption: DemoText.t("Együtt járó címkék, egy címke hozzáadása után", "Co-occurring tags, after adding a tag"); demo: "cooccur"; focusedTag: 0 }
        RowDemo { caption: DemoText.t("A nyelvi modell javaslata, az összefoglalás után", "The language model's suggestion, after the summary"); demo: "llm" }
    }
}
