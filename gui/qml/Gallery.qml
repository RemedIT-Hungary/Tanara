import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// Vezérlő-galéria: a design-rendszer minden eleme minden állapotában, mindkét témában.
//   interaktívan:   tanara --gallery [--theme dark]
//   képernyőkép:    tanara --qml-shot ki.png --qml-page Gallery --theme dark --size 1280x2600
// overlay: "" | "dialog" | "menu" | "popover" — induláskor megnyit egy VALÓDI felugrót
// (pl. --qml-prop overlay=dialog), a fátyol / árnyék / pozicionálás ellenőrzéséhez.
// A mintaszövegek fejlesztői tartalom → szándékosan nincsenek qsTr()-ben.
Item {
    id: root

    property string overlay: ""

    readonly property var iconNames: [
        "file-text", "sparkles", "user-check", "search", "users", "settings", "fingerprint",
        "ellipsis", "undo-2", "redo-2", "plus", "panel-left", "chevron-down", "check",
        "wand-sparkles", "play", "pause", "volume-2", "mic", "monitor-speaker", "speaker",
        "audio-lines", "triangle-alert", "lock", "rotate-ccw", "list-tree", "list-filter",
        "copy", "folder-open", "grip-vertical", "trash-2", "inbox", "radar", "arrow-right",
        "minus", "square", "x", "chevrons-left-right", "pencil", "chevron-right", "chevron-up",
        "chevron-left", "info", "circle-alert", "circle-check", "loader-circle", "refresh-cw",
        "user", "user-plus", "volume-x", "mic-off", "external-link", "clock", "sun", "moon",
        "circle-dot", "ellipsis-vertical"
    ]
    readonly property var speakerNames: [
        "Kovács Lilla", "Fehér Ádám", "Szabó Áron", "Varga Nóra", "Tóth Bence", "Molnár Eszter",
        "Németh Dóra", "Balogh Kata", "Horváth Gábor", "Távoli 1", "Távoli 2"
    ]

    component Section: ColumnLayout {
        property string title: ""
        default property alias content: flow.data
        property alias flowSpacing: flow.spacing
        Layout.fillWidth: true
        spacing: 12
        TSectionLabel { text: parent.title }
        Flow {
            id: flow
            Layout.fillWidth: true
            spacing: 12
        }
    }
    component Caption: TLabel {
        muted: true
        mono: true
        font.pixelSize: Theme.fontMicro
    }
    component Swatch: Column {
        property string name: ""
        property color swatchColor: "transparent"
        spacing: 4
        Rectangle {
            width: 76; height: 40
            radius: Theme.radiusControl
            color: parent.swatchColor
            border.width: 1
            border.color: Theme.border
        }
        Caption { text: parent.name }
    }
    // Egy vezérlő + alatta az állapot neve.
    component Shown: Column {
        property string caption: ""
        default property alias content: holder.data
        spacing: 5
        Item {
            id: holder
            implicitWidth: childrenRect.width
            implicitHeight: childrenRect.height
            width: implicitWidth
            height: implicitHeight
        }
        Caption { text: parent.caption }
    }
    component ButtonStates: Row {
        id: bs
        property string variant: "secondary"
        property string label: ""
        property string iconName: ""
        spacing: 10
        Shown { caption: parent.variant; TButton { text: bs.label; variant: bs.variant; iconName: bs.iconName } }
        Shown { caption: "hover"; TButton { text: bs.label; variant: bs.variant; iconName: bs.iconName; stateHovered: true } }
        Shown { caption: "pressed"; TButton { text: bs.label; variant: bs.variant; iconName: bs.iconName; down: true } }
        Shown { caption: "focus"; TButton { text: bs.label; variant: bs.variant; iconName: bs.iconName; stateFocused: true } }
        Shown { caption: "disabled"; TButton { text: bs.label; variant: bs.variant; iconName: bs.iconName; enabled: false } }
        Shown { caption: "small"; TButton { text: bs.label; variant: bs.variant; iconName: bs.iconName; size: "small" } }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: page.implicitHeight + 64
        boundsBehavior: Flickable.StopAtBounds
        T.ScrollBar.vertical: TScrollBar {}

        ColumnLayout {
            id: page
            x: 28; y: 28
            width: flick.width - 56
            spacing: 28

            RowLayout {
                Layout.fillWidth: true
                TLabel {
                    text: "Tanara · Nyomat / Print — vezérlő-galéria"
                    font.pixelSize: Theme.fontTitle
                    font.weight: Theme.weightSemiBold
                }
                Item { Layout.fillWidth: true }
                Caption { text: (Theme.dark ? "dark" : "light") + " · themeMode: " + App.themeMode }
                TButton {
                    text: Theme.dark ? "Világos" : "Sötét"
                    size: "small"
                    iconName: Theme.dark ? "sun" : "moon"
                    onClicked: App.themeMode = Theme.dark ? "light" : "dark"
                }
            }

            // ---------------------------------------------------------------- színek
            Section {
                title: "Felület és szöveg"
                Swatch { name: "bg"; swatchColor: Theme.bg }
                Swatch { name: "surface"; swatchColor: Theme.surface }
                Swatch { name: "raised"; swatchColor: Theme.raised }
                Swatch { name: "sunken"; swatchColor: Theme.sunken }
                Swatch { name: "border"; swatchColor: Theme.border }
                Swatch { name: "borderStrong"; swatchColor: Theme.borderStrong }
                Swatch { name: "text"; swatchColor: Theme.text }
                Swatch { name: "textMuted"; swatchColor: Theme.textMuted }
                Swatch { name: "accent"; swatchColor: Theme.accent }
                Swatch { name: "accentSoft"; swatchColor: Theme.accentSoft }
                Swatch { name: "accentLine"; swatchColor: Theme.accentLine }
                Swatch { name: "warnSoft"; swatchColor: Theme.warnSoft }
                Swatch { name: "warnLine"; swatchColor: Theme.warnLine }
                Swatch { name: "successSoft"; swatchColor: Theme.successSoft }
                Swatch { name: "danger"; swatchColor: Theme.danger }
                Swatch { name: "dangerSoft"; swatchColor: Theme.dangerSoft }
                Swatch { name: "dangerLine"; swatchColor: Theme.dangerLine }
                Swatch { name: "rec"; swatchColor: Theme.rec }
            }

            Section {
                title: "Beszélők (11) — avatar soft / solid, chip, név"
                flowSpacing: 16
                Repeater {
                    model: root.speakerNames
                    Row {
                        required property int index
                        required property string modelData
                        spacing: 8
                        TAvatar { name: parent.modelData; speakerIndex: parent.index }
                        TAvatar { name: parent.modelData; speakerIndex: parent.index; variant: "solid"; size: 20; anchors.verticalCenter: parent.verticalCenter }
                        TLabel {
                            text: parent.modelData
                            color: Theme.speakerInk(parent.index)
                            font.pixelSize: Theme.fontSmall
                            font.weight: Theme.weightSemiBold
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }
            }

            // ------------------------------------------------------------ tipográfia
            Section {
                title: "Tipográfia"
                ColumnLayout {
                    spacing: 8
                    width: parent.width
                    RowLayout { spacing: 16
                        Caption { text: "title 20/600"; Layout.preferredWidth: 110 }
                        TLabel { text: "Projekt heti egyeztetés"; font.pixelSize: Theme.fontTitle; font.weight: Theme.weightSemiBold }
                    }
                    RowLayout { spacing: 16
                        Caption { text: "heading 16/600"; Layout.preferredWidth: 110 }
                        TLabel { text: "Döntések és teendők"; font.pixelSize: Theme.fontHeading; font.weight: Theme.weightSemiBold }
                    }
                    RowLayout { spacing: 16
                        Caption { text: "body 14/400"; Layout.preferredWidth: 110 }
                        TLabel { text: "Az átirat elkészült, 4 beszélő, 30 perc 34 mp. Árvíztűrő tükörfúrógép." }
                    }
                    RowLayout { spacing: 16
                        Caption { text: "transcript 14/1.55"; Layout.preferredWidth: 110; Layout.alignment: Qt.AlignTop }
                        TLabel {
                            Layout.preferredWidth: 520
                            wrapMode: Text.Wrap
                            cssLineHeight: Theme.transcriptLineHeight
                            text: "Rendben, akkor a szerződést péntekig átnézem, és küldök róla egy rövid összefoglalót. A súgót a többi termékre is kiterjesztik, de a számlázásnál előbb a folyamatot egyszerűsítik."
                        }
                    }
                    RowLayout { spacing: 16
                        Caption { text: "small 13"; Layout.preferredWidth: 110 }
                        TLabel { text: "2026. okt. 1. · 1:16:04 · 6 beszélő"; muted: true; font.pixelSize: Theme.fontSmall }
                    }
                    RowLayout { spacing: 16
                        Caption { text: "caption 12"; Layout.preferredWidth: 110 }
                        TLabel { text: "okt. 2. · 30 p"; muted: true; font.pixelSize: Theme.fontCaption }
                    }
                    RowLayout { spacing: 16
                        Caption { text: "label 12/600"; Layout.preferredWidth: 110 }
                        TSectionLabel { text: "Átirat · 2026. okt. 2." }
                    }
                    RowLayout { spacing: 16
                        Caption { text: "mono 12 / 11"; Layout.preferredWidth: 110 }
                        TLabel { text: "00:13 · 30:34 · 2026-10-02"; mono: true; font.pixelSize: Theme.fontCaption }
                        TLabel { text: "Ctrl+F"; mono: true; muted: true; font.pixelSize: Theme.fontMicro }
                    }
                }
            }

            // ---------------------------------------------------------------- ikonok
            Section {
                title: "Ikonok (Lucide, stroke 1.75) — 16 px, lent 20 px és színezve"
                flowSpacing: 14
                Repeater {
                    model: root.iconNames
                    TIcon { required property string modelData; name: modelData }
                }
            }
            Flow {
                Layout.fillWidth: true
                spacing: 14
                TIcon { name: "mic"; size: 20 }
                TIcon { name: "audio-lines"; size: 20; color: Theme.accent }
                TIcon { name: "triangle-alert"; size: 20; color: Theme.warnInk }
                TIcon { name: "trash-2"; size: 20; color: Theme.dangerInk }
                TIcon { name: "user-check"; size: 20; color: Theme.successInk }
                TIcon { name: "sparkles"; size: 20; color: Theme.textMuted }
                TIcon { name: "fingerprint"; size: 24 }
                TIcon { name: "inbox"; size: 22; color: Theme.textMuted }
                TSpinner {}
            }

            // ---------------------------------------------------------------- gombok
            Section {
                title: "Gombok — 34 px (és 28 px)"
                ColumnLayout {
                    spacing: 12
                    ButtonStates { variant: "primary"; label: "Átírás indítása"; iconName: "audio-lines" }
                    ButtonStates { variant: "secondary"; label: "Azonosítás"; iconName: "fingerprint" }
                    ButtonStates { variant: "ghost"; label: "Mégse" }
                    ButtonStates { variant: "danger"; label: "Újra-átírás" }
                    ButtonStates { variant: "dangerSoft"; label: "Végleges törlés"; iconName: "trash-2" }
                    ButtonStates { variant: "dangerGhost"; label: "Eldobott sávok törlése"; iconName: "trash-2" }
                    ButtonStates { variant: "record"; label: "Új felvétel" }
                }
            }
            Section {
                title: "Ikongombok"
                Shown { caption: "outline"; TIconButton { iconName: "ellipsis"; toolTipText: "További műveletek" } }
                Shown { caption: "hover"; TIconButton { iconName: "ellipsis"; stateHovered: true } }
                Shown { caption: "pressed"; TIconButton { iconName: "ellipsis"; down: true } }
                Shown { caption: "focus"; TIconButton { iconName: "ellipsis"; stateFocused: true } }
                Shown { caption: "disabled"; TIconButton { iconName: "ellipsis"; enabled: false } }
                Shown { caption: "flat"; TIconButton { iconName: "undo-2"; variant: "flat" } }
                Shown { caption: "flat hover"; TIconButton { iconName: "redo-2"; variant: "flat"; stateHovered: true } }
                Shown { caption: "small"; TIconButton { iconName: "search"; size: "small" } }
                Shown { caption: "checked"; TIconButton { iconName: "panel-left"; size: "small"; checkable: true; checked: true } }
                Shown { caption: "solid 32"; TIconButton { iconName: "play"; iconSize: 13; variant: "solid"; implicitWidth: 32; radius: 16 } }
                Shown { caption: "solid hover"; TIconButton { iconName: "pause"; iconSize: 13; variant: "solid"; implicitWidth: 32; radius: 16; stateHovered: true } }
                Shown {
                    caption: "tooltip"
                    TIconButton {
                        iconName: "volume-2"; variant: "flat"
                        TToolTip { visible: true; delay: 0; text: "Hangerő" }
                    }
                }
            }

            // ---------------------------------------------------------------- mezők
            Section {
                title: "Mezők"
                Shown { caption: "placeholder"; TTextField { placeholderText: "Megbeszélés címe" } }
                Shown { caption: "value"; TTextField { text: "Heti projekt-egyeztetés" } }
                Shown { caption: "focus"; TTextField { text: "Heti projekt-egyeztetés"; stateFocused: true } }
                Shown {
                    caption: "error + mono"
                    Column {
                        spacing: 4
                        TTextField { text: "sk-••••••••••••"; mono: true; hasError: true }
                        TLabel { text: "Hiányzik az API-kulcs."; color: Theme.dangerInk; font.pixelSize: Theme.fontCaption }
                    }
                }
                Shown { caption: "disabled"; TTextField { text: "Nem szerkeszthető"; enabled: false } }
                Shown { caption: "search"; TSearchField { width: 252; placeholderText: "Keresés"; hint: "Ctrl+F" } }
                Shown { caption: "search focus"; TSearchField { width: 252; text: "demó"; stateFocused: true } }
                Shown {
                    caption: "textarea"
                    TTextArea {
                        width: 420
                        text: "Ügyféltámogatás átadása az új csapatnak. Érintett rendszerek: jegykezelő, súgóoldalak, számlázás. Résztvevők: Molnár Eszter, Tóth Bence."
                    }
                }
                Shown { caption: "textarea placeholder"; TTextArea { width: 300; placeholderText: "Nevek, szakszavak, témák…" } }
            }

            // ------------------------------------------------- kapcsoló, jelölőnégyzet
            Section {
                title: "Kapcsoló · jelölőnégyzet"
                flowSpacing: 20
                Shown { caption: "off"; TSwitch { text: "Lekeverés készítése" } }
                Shown { caption: "on"; TSwitch { text: "Résztvevők azonosítása hang alapján"; checked: true } }
                Shown { caption: "on hover"; TSwitch { checked: true; stateHovered: true } }
                Shown { caption: "focus"; TSwitch { stateFocused: true } }
                Shown { caption: "disabled"; TSwitch { text: "Letiltva"; checked: true; enabled: false } }
                Shown { caption: "off"; TCheckBox { text: "A súgóoldalak kiterjesztése" } }
                Shown { caption: "on"; TCheckBox { text: "A mostani átirat maradjon meg másolatként"; checked: true } }
                Shown { caption: "hover"; TCheckBox { stateHovered: true } }
                Shown { caption: "focus"; TCheckBox { checked: true; stateFocused: true } }
                Shown { caption: "disabled"; TCheckBox { text: "Letiltva"; checked: true; enabled: false } }
            }

            // ------------------------------------------------- pirula, chip, állapot
            Section {
                title: "Pirulák · chipek · állapotikonok"
                flowSpacing: 16
                Shown { caption: "warn"; TPill { text: "elavult"; tone: "warn" } }
                Shown { caption: "warn"; TPill { text: "bizonytalan"; tone: "warn" } }
                Shown { caption: "success"; TPill { text: "javítva"; tone: "success" } }
                Shown { caption: "danger"; TPill { text: "hiányzik a fájl"; tone: "danger" } }
                Shown { caption: "neutral"; TPill { text: "eldobott · csendes" } }
                Shown { caption: "accent"; TPill { text: "fut"; tone: "accent" } }
                Shown { caption: "removable"; TChip { text: "Nincs összefoglaló"; removable: true } }
                Shown { caption: "person"; TChip { text: "Varga Nóra"; speakerIndex: 3; removable: true } }
                Shown { caption: "person"; TChip { text: "Kovács Lilla"; speakerIndex: 0 } }
                Shown { caption: "dashed add"; TChip { text: "Szűrő"; dashed: true } }
                Shown { caption: "filter off"; TChip { text: "Bizonytalan 12"; checkable: true } }
                Shown { caption: "filter on"; TChip { text: "Bizonytalan 12"; checkable: true; checked: true } }
                Shown { caption: "hover"; TChip { text: "Nincs összefoglaló"; removable: true; stateHovered: true } }
                Shown { caption: "focus"; TChip { text: "Szűrő"; dashed: true; stateFocused: true } }
                Shown { caption: "done"; Row { spacing: 5
                    TStatusIcon { kind: "transcript"; state: "done"; toolTipText: "Átirat: kész" }
                    TStatusIcon { kind: "summary"; state: "done" }
                    TStatusIcon { kind: "identified"; state: "done" } } }
                Shown { caption: "running"; Row { spacing: 5
                    TStatusIcon { kind: "transcript"; state: "running" }
                    TStatusIcon { kind: "summary"; state: "running" }
                    TStatusIcon { kind: "identified"; state: "running" } } }
                Shown { caption: "error"; Row { spacing: 5
                    TStatusIcon { kind: "transcript"; state: "error" }
                    TStatusIcon { kind: "summary"; state: "error" }
                    TStatusIcon { kind: "identified"; state: "error" } } }
                Shown { caption: "stale"; Row { spacing: 5
                    TStatusIcon { kind: "transcript"; state: "stale" }
                    TStatusIcon { kind: "summary"; state: "stale" }
                    TStatusIcon { kind: "identified"; state: "stale" } } }
                Shown { caption: "missing"; Row { spacing: 5
                    TStatusIcon { kind: "transcript"; state: "missing" }
                    TStatusIcon { kind: "summary"; state: "missing" }
                    TStatusIcon { kind: "identified"; state: "missing" } } }
            }

            // ------------------------------------------------- fülek, folyamat
            Section {
                title: "Fülek · folyamatjelző"
                flowSpacing: 32
                TTabBar {
                    width: 360
                    currentIndex: 1
                    TTabButton { text: "Átirat" }
                    TTabButton { text: "Összefoglaló"; pillText: "elavult" }
                    TTabButton { text: "Sávok" }
                }
                TTabBar {
                    width: 260
                    TTabButton { text: "Aktív" }
                    TTabButton { text: "Hover"; stateHovered: true }
                    TTabButton { text: "Fókusz"; stateFocused: true }
                }
                Column {
                    spacing: 6
                    width: 240
                    Row {
                        width: parent.width
                        TLabel { text: "Átírás folyamatban…"; font.pixelSize: Theme.fontSmall; width: parent.width - pct.width }
                        TLabel { id: pct; text: "42%"; mono: true; muted: true; font.pixelSize: Theme.fontSmall }
                    }
                    TProgressBar { width: parent.width; thickness: 6; value: 0.42 }
                    Caption { text: "6 px" }
                }
                Column {
                    spacing: 6
                    width: 200
                    TProgressBar { width: parent.width; value: 0.6 }
                    Caption { text: "4 px" }
                    TProgressBar { width: parent.width; indeterminate: true }
                    Caption { text: "indeterminate" }
                }
            }

            // ------------------------------------------------- bannerek, kártyák
            Section {
                title: "Bannerek"
                TBanner {
                    width: parent.width
                    tone: "warn"
                    text: "Az összefoglaló óta 3 beszélőt javítottál, ezért a felelősök és a résztvevők elavultak lehetnek."
                    TButton { text: "Frissítés"; size: "small"; font.weight: Theme.weightSemiBold }
                    TButton { text: "Rendben így"; size: "small"; variant: "ghost" }
                }
                TBanner {
                    width: parent.width
                    tone: "warn"
                    title: "Nincs beállítva átíró szolgáltató"
                    text: "Add meg a saját kulcsodat, vagy jelentkezz be a Tanara Cloudba."
                    TButton { text: "Szolgáltató beállítása"; size: "small"; trailingIconName: "arrow-right" }
                }
                TBanner {
                    width: parent.width
                    tone: "accent"
                    iconName: "wand-sparkles"
                    text: "Még 14 sor hasonlít erre a hangra. Átrakjam őket Fehér Ádámhoz?"
                    TButton { text: "Átrakom"; size: "small"; variant: "primary" }
                    TButton { text: "Megmutatom"; size: "small"; variant: "ghost" }
                    TButton { text: "Nem"; size: "small"; variant: "ghost" }
                }
                TBanner {
                    width: parent.width
                    tone: "danger"
                    title: "Az átírás nem sikerült"
                    text: "A szolgáltató elutasította a kérést: az API-kulcs érvénytelen vagy lejárt. A felvétel és a sávok érintetlenek."
                    TButton { text: "Újrapróbálás"; size: "small"; variant: "primary" }
                }
            }
            Section {
                title: "Kártyák"
                TCard {
                    width: 300
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 6
                        TLabel { text: "Témánkénti elemzés"; font.pixelSize: 15; font.weight: Theme.weightSemiBold }
                        TLabel { Layout.fillWidth: true; wrapMode: Text.Wrap; muted: true; font.pixelSize: Theme.fontSmall; text: "2 lépés, témánként 1–2 perc." }
                        TButton { text: "Témák javaslása" }
                    }
                }
                TCard {
                    width: 300
                    tone: "accent"
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 6
                        RowLayout {
                            TLabel { text: "Gyors összefoglaló"; font.pixelSize: 15; font.weight: Theme.weightSemiBold }
                            TPill { text: "ajánlott"; tone: "accent" }
                        }
                        TLabel { Layout.fillWidth: true; wrapMode: Text.Wrap; muted: true; font.pixelSize: Theme.fontSmall; text: "Vezetői összefoglaló, döntések, teendők — kb. 1 perc." }
                        TButton { text: "Összefoglaló készítése"; variant: "primary" }
                    }
                }
                TCard {
                    width: 300
                    tone: "danger"
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 6
                        RowLayout {
                            TIcon { name: "circle-alert"; size: 18; color: Theme.dangerInk }
                            TLabel { text: "Az átírás nem sikerült"; font.pixelSize: Theme.fontHeading; font.weight: Theme.weightSemiBold }
                        }
                        TLabel { Layout.fillWidth: true; wrapMode: Text.Wrap; text: "A felvétel és a sávok érintetlenek." }
                        TLabel { text: "HTTP 401 · invalid_api_key"; mono: true; muted: true; font.pixelSize: Theme.fontCaption }
                    }
                }
            }

            // ------------------------------------------------- menü, popover, dialog
            Section {
                title: "Menü · popover · párbeszédablak (helyben kirajzolva; valódi felugró: overlay property)"
                flowSpacing: 40

                TSurface {
                    width: 250
                    height: menuCol.implicitHeight + 8
                    Column {
                        id: menuCol
                        x: 4; y: 4
                        width: parent.width - 8
                        TMenuItem { width: parent.width; text: "Átnevezés"; iconName: "pencil"; shortcutText: "F2" }
                        TMenuItem { width: parent.width; text: "Megnyitás mappában"; iconName: "folder-open"; stateHovered: true }
                        TMenuItem { width: parent.width; text: "Újra-átírás…"; iconName: "rotate-ccw" }
                        TMenuItem { width: parent.width; text: "Letiltott elem"; iconName: "lock"; enabled: false }
                        TMenuItem { width: parent.width; text: "Sötét téma"; iconName: "moon"; checked: true }
                        TMenuSeparator { width: parent.width }
                        TMenuItem { width: parent.width; text: "Törlés…"; iconName: "trash-2"; danger: true }
                    }
                }

                TSurface {
                    width: 300
                    height: popCol.implicitHeight + 24
                    ColumnLayout {
                        id: popCol
                        x: 12; y: 12
                        width: parent.width - 24
                        spacing: 8
                        TSearchField { Layout.fillWidth: true; placeholderText: "Név keresése vagy új személy" }
                        TSectionLabel { text: "Másik személy"; font.pixelSize: Theme.fontMicro }
                        Repeater {
                            model: 3
                            RowLayout {
                                required property int index
                                spacing: 10
                                TAvatar { name: root.speakerNames[parent.index + 1]; speakerIndex: parent.index + 1 }
                                TLabel { text: root.speakerNames[parent.index + 1]; Layout.fillWidth: true }
                                TLabel { text: (12 - parent.index * 4) + " megbeszélés"; muted: true; font.pixelSize: Theme.fontCaption }
                            }
                        }
                        TDivider { Layout.fillWidth: true }
                        TLabel {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            muted: true
                            font.pixelSize: Theme.fontCaption
                            text: "A beszélő névtelenül is maradhat; később is elnevezheted."
                        }
                    }
                }

                TSurface {
                    width: 440
                    height: dlgCol.implicitHeight + 44
                    radius: Theme.radiusDialog
                    ColumnLayout {
                        id: dlgCol
                        x: 22; y: 22
                        width: parent.width - 44
                        spacing: 14
                        TLabel { text: "Újra-átírod a megbeszélést?"; font.pixelSize: 18; font.weight: Theme.weightSemiBold }
                        TLabel {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            cssLineHeight: 1.55
                            text: "A mostani átirat és a benne lévő 23 kézi javítás elvész, az összefoglaló elavulttá válik. A hanglenyomatokba tanított javítások megmaradnak."
                        }
                        TCheckBox { Layout.fillWidth: true; checked: true; text: "A mostani átirat maradjon meg másolatként" }
                        RowLayout {
                            Layout.topMargin: 6
                            spacing: 8
                            Item { Layout.fillWidth: true }
                            TButton { text: "Mégse" }
                            TButton { text: "Újra-átírás"; variant: "danger" }
                        }
                    }
                }
            }

            Section {
                title: "Valódi felugrók (kattintásra)"
                TButton {
                    id: dialogButton
                    text: "Párbeszédablak…"
                    onClicked: demoDialog.open()
                }
                TButton {
                    id: menuButton
                    text: "Menü"
                    trailingIconName: "chevron-down"
                    onClicked: demoMenu.open()
                    TMenu {
                        id: demoMenu
                        y: menuButton.height + 4
                        width: 250
                        TMenuItem { text: "Átnevezés"; iconName: "pencil" }
                        TMenuItem { text: "Megnyitás mappában"; iconName: "folder-open" }
                        TMenuItem { text: "Újra-átírás…"; iconName: "rotate-ccw" }
                        TMenuSeparator {}
                        TMenuItem { text: "Törlés…"; iconName: "trash-2"; danger: true }
                    }
                }
                TButton {
                    id: popoverButton
                    text: "Popover"
                    trailingIconName: "chevron-down"
                    onClicked: demoPopover.open()
                    TPopover {
                        id: demoPopover
                        y: popoverButton.height + 6
                        width: 300
                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 8
                            TSearchField { Layout.fillWidth: true; placeholderText: "Név keresése vagy új személy" }
                            TLabel { Layout.fillWidth: true; wrapMode: Text.Wrap; muted: true; font.pixelSize: Theme.fontCaption
                                     text: "A beszélő névtelenül is maradhat; később is elnevezheted." }
                        }
                    }
                }
            }

            Section {
                title: "Egyéb: elválasztó, szaggatott keret, helykitöltő"
                TDivider { width: 200 }
                TDashedRect { width: 120; height: 34 }
                TDashedRect { width: 24; height: 24; radius: 12; color: Theme.accent }
                TPlaceholder { width: 260; height: 70; label: "Valami.qml"; note: "Helykitöltő a héj-vázban" }
            }
        }
    }

    TDialog {
        id: demoDialog
        title: "Újra-átírod a megbeszélést?"
        TLabel {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            cssLineHeight: 1.55
            text: "A mostani átirat és a benne lévő 23 kézi javítás elvész, az összefoglaló elavulttá válik. A hanglenyomatokba tanított javítások megmaradnak."
        }
        TCheckBox { Layout.fillWidth: true; checked: true; text: "A mostani átirat maradjon meg másolatként" }
        actions: [
            TButton { text: "Mégse"; onClicked: demoDialog.reject() },
            TButton { text: "Újra-átírás"; variant: "danger"; onClicked: demoDialog.accept() }
        ]
    }

    Component.onCompleted: {
        if (overlay === "dialog") {
            demoDialog.open()
        } else if (overlay === "menu" || overlay === "popover") {
            // Az elrendezés elkészülte után görgetünk a gombokhoz, és csak utána nyitunk.
            Qt.callLater(() => {
                flick.contentY = Math.max(0, flick.contentHeight - flick.height)
                if (overlay === "menu") demoMenu.open(); else demoPopover.open()
            })
        }
    }
}
