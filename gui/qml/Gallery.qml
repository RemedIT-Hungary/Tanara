import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// Control gallery: every design-system element in every state, in both themes.
//   interactive:  tanara --gallery [--theme dark]
//   screenshot:   tanara --qml-shot out.png --qml-page Gallery --theme dark --size 1280x2600
// overlay: "" | "dialog" | "menu" | "popover" - opens a REAL popup at startup
// (e.g. --qml-prop overlay=dialog), to check scrim / shadow / positioning.
// Meeting-shell elements (EvidenceChip, TimeChip, TranscriptMarker, CandidateRow, EvidenceReason, TagRowOverview,
// StatTiles, PipelineSteps): see the "Evidence chip" ... "Overview" sections.
// Track and learning elements: TrackActivityStrip (with fictional speech frames), ParticipantTrackChips, TagLearnBar,
// BackfillSkipNote, quick "+ #Tag" - see the "Tracks and learning" section.
// section: "" (all) | "tags" - only that section (e.g. --qml-prop 'section="tags"').
// The sample texts are developer content -> intentionally not wrapped in qsTr().
Item {
    id: root

    property string overlay: ""
    property string section: ""

    readonly property var iconNames: [
        "file-text", "sparkles", "user-check", "search", "users", "settings", "fingerprint",
        "ellipsis", "undo-2", "redo-2", "plus", "panel-left", "chevron-down", "check",
        "wand-sparkles", "play", "pause", "volume-2", "mic", "monitor-speaker", "speaker",
        "audio-lines", "triangle-alert", "lock", "rotate-ccw", "list-tree", "list-filter",
        "copy", "folder-open", "grip-vertical", "trash-2", "inbox", "radar", "arrow-right",
        "minus", "square", "x", "chevrons-left-right", "pencil", "chevron-right", "chevron-up",
        "chevron-left", "info", "circle-alert", "circle-check", "loader-circle", "refresh-cw",
        "user", "user-plus", "volume-x", "mic-off", "external-link", "clock", "sun", "moon",
        "circle-dot", "ellipsis-vertical", "hash", "tag"
    ]
    readonly property var speakerNames: [
        DemoText.name("Kovács Lilla"), DemoText.name("Fehér Ádám"), DemoText.name("Szabó Áron"), DemoText.name("Varga Nóra"), DemoText.name("Tóth Bence"), DemoText.name("Molnár Eszter"),
        DemoText.name("Németh Dóra"), DemoText.name("Balogh Kata"), DemoText.name("Horváth Gábor"), DemoText.raw("Távoli 1"), DemoText.raw("Távoli 2")
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
    // One control + the state name below it.
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
        contentHeight: (root.section === "tags" ? tagsOnly.implicitHeight : page.implicitHeight) + 64
        boundsBehavior: Flickable.StopAtBounds
        T.ScrollBar.vertical: TScrollBar {}

        // Only the "Tags" section (the handoff's tag sheet).
        TagGallerySection {
            id: tagsOnly
            visible: root.section === "tags"
            x: 32; y: 28
            width: flick.width - 64
        }

        ColumnLayout {
            id: page
            visible: root.section === ""
            x: 28; y: 28
            width: flick.width - 56
            spacing: 28

            RowLayout {
                Layout.fillWidth: true
                TLabel {
                    text: DemoText.t("Tanara · Nyomat / Print — vezérlő-galéria", "Tanara · Print — control gallery")
                    font.pixelSize: Theme.fontTitle
                    font.weight: Theme.weightSemiBold
                }
                Item { Layout.fillWidth: true }
                Caption { text: (Theme.dark ? "dark" : "light") + " · themeMode: " + App.themeMode }
                TButton {
                    text: Theme.dark ? DemoText.t("Világos", "Light") : DemoText.t("Sötét", "Dark")
                    size: "small"
                    iconName: Theme.dark ? "sun" : "moon"
                    onClicked: App.themeMode = Theme.dark ? "light" : "dark"
                }
            }

            // ---------------------------------------------------------------- colors
            Section {
                title: DemoText.t("Felület és szöveg", "Surfaces and text")
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
                title: DemoText.t("Beszélők (11) — avatar soft / solid, chip, név", "Speakers (11) — avatar soft / solid, chip, name")
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

            // ------------------------------------------------------------ typography
            Section {
                title: DemoText.t("Tipográfia", "Typography")
                ColumnLayout {
                    spacing: 8
                    width: parent.width
                    RowLayout { spacing: 16
                        Caption { text: "title 20/600"; Layout.preferredWidth: 110 }
                        TLabel { text: DemoText.t("Projekt heti egyeztetés", "Weekly project sync"); font.pixelSize: Theme.fontTitle; font.weight: Theme.weightSemiBold }
                    }
                    RowLayout { spacing: 16
                        Caption { text: "heading 16/600"; Layout.preferredWidth: 110 }
                        TLabel { text: DemoText.t("Döntések és teendők", "Decisions and action items"); font.pixelSize: Theme.fontHeading; font.weight: Theme.weightSemiBold }
                    }
                    RowLayout { spacing: 16
                        Caption { text: "body 14/400"; Layout.preferredWidth: 110 }
                        TLabel { text: DemoText.t("Az átirat elkészült, 4 beszélő, 30 perc 34 mp. Árvíztűrő tükörfúrógép.", "The transcript is ready, 4 speakers, 30 min 34 s. The quick brown fox jumps over the lazy dog.") }
                    }
                    RowLayout { spacing: 16
                        Caption { text: "transcript 14/1.55"; Layout.preferredWidth: 110; Layout.alignment: Qt.AlignTop }
                        TLabel {
                            Layout.preferredWidth: 520
                            wrapMode: Text.Wrap
                            cssLineHeight: Theme.transcriptLineHeight
                            text: DemoText.t("Rendben, akkor a szerződést péntekig átnézem, és küldök róla egy rövid összefoglalót. A súgót a többi termékre is kiterjesztik, de a számlázásnál előbb a folyamatot egyszerűsítik.", "Okay, then I'll review the contract by Friday and send a short summary. The help pages will be extended to the other products, but for billing the process gets simplified first.")
                        }
                    }
                    RowLayout { spacing: 16
                        Caption { text: "small 13"; Layout.preferredWidth: 110 }
                        TLabel { text: DemoText.t("2026. okt. 1. · 1:16:04 · 6 beszélő", "Oct 1, 2026 · 1:16:04 · 6 speakers"); muted: true; font.pixelSize: Theme.fontSmall }
                    }
                    RowLayout { spacing: 16
                        Caption { text: "caption 12"; Layout.preferredWidth: 110 }
                        TLabel { text: DemoText.t("okt. 2. · 30 p", "Oct 2 · 30 min"); muted: true; font.pixelSize: Theme.fontCaption }
                    }
                    RowLayout { spacing: 16
                        Caption { text: "label 12/600"; Layout.preferredWidth: 110 }
                        TSectionLabel { text: DemoText.t("Átirat · 2026. okt. 2.", "Transcript · Oct 2, 2026") }
                    }
                    RowLayout { spacing: 16
                        Caption { text: "mono 12 / 11"; Layout.preferredWidth: 110 }
                        TLabel { text: "00:13 · 30:34 · 2026-10-02"; mono: true; font.pixelSize: Theme.fontCaption }
                        TLabel { text: "Ctrl+F"; mono: true; muted: true; font.pixelSize: Theme.fontMicro }
                    }
                }
            }

            // ---------------------------------------------------------------- icons
            Section {
                title: DemoText.t("Ikonok (Lucide, stroke 1.75) — 16 px, lent 20 px és színezve", "Icons (Lucide, stroke 1.75) — 16 px, below 20 px and tinted")
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

            // ---------------------------------------------------------------- buttons
            Section {
                title: DemoText.t("Gombok — 34 px (és 28 px)", "Buttons — 34 px (and 28 px)")
                ColumnLayout {
                    spacing: 12
                    ButtonStates { variant: "primary"; label: DemoText.t("Átírás indítása", "Start transcription"); iconName: "audio-lines" }
                    ButtonStates { variant: "secondary"; label: DemoText.t("Azonosítás", "Identify"); iconName: "fingerprint" }
                    ButtonStates { variant: "ghost"; label: DemoText.t("Mégse", "Cancel") }
                    ButtonStates { variant: "danger"; label: DemoText.t("Újra-átírás", "Re-transcribe") }
                    ButtonStates { variant: "dangerSoft"; label: DemoText.t("Végleges törlés", "Delete permanently"); iconName: "trash-2" }
                    ButtonStates { variant: "dangerGhost"; label: DemoText.t("Eldobott sávok törlése", "Delete discarded tracks"); iconName: "trash-2" }
                    ButtonStates { variant: "record"; label: DemoText.t("Új felvétel", "New recording") }
                }
            }
            Section {
                title: DemoText.t("Ikongombok", "Icon buttons")
                Shown { caption: "outline"; TIconButton { iconName: "ellipsis"; toolTipText: DemoText.t("További műveletek", "More actions") } }
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
                        TToolTip { visible: root.section === ""; delay: 0; text: DemoText.t("Hangerő", "Volume") }
                    }
                }
            }

            // ---------------------------------------------------------------- fields
            Section {
                title: DemoText.t("Mezők", "Fields")
                Shown { caption: "placeholder"; TTextField { placeholderText: DemoText.t("Megbeszélés címe", "Meeting title") } }
                Shown { caption: "value"; TTextField { text: DemoText.t("Heti projekt-egyeztetés", "Weekly project sync") } }
                Shown { caption: "focus"; TTextField { text: DemoText.t("Heti projekt-egyeztetés", "Weekly project sync"); stateFocused: true } }
                Shown {
                    caption: "error + mono"
                    Column {
                        spacing: 4
                        TTextField { text: "sk-••••••••••••"; mono: true; hasError: true }
                        TLabel { text: DemoText.t("Hiányzik az API-kulcs.", "The API key is missing."); color: Theme.dangerInk; font.pixelSize: Theme.fontCaption }
                    }
                }
                Shown { caption: "disabled"; TTextField { text: DemoText.t("Nem szerkeszthető", "Read-only"); enabled: false } }
                Shown { caption: "search"; TSearchField { width: 252; placeholderText: DemoText.t("Keresés", "Search"); hint: "Ctrl+F" } }
                Shown { caption: "search focus"; TSearchField { width: 252; text: DemoText.t("demó", "demo"); stateFocused: true } }
                Shown {
                    caption: "textarea"
                    TTextArea {
                        width: 420
                        text: DemoText.t("Ügyféltámogatás átadása az új csapatnak. Érintett rendszerek: jegykezelő, súgóoldalak, számlázás. Résztvevők: Molnár Eszter, Tóth Bence.", "Support handover to the new team. Systems involved: ticket system, help pages, billing. Participants: Sophie Bennett, Ryan Mitchell.")
                    }
                }
                Shown { caption: "textarea placeholder"; TTextArea { width: 300; placeholderText: DemoText.t("Nevek, szakszavak, témák…", "Names, terms, topics…") } }
            }

            // ------------------------------------------------- switch, checkbox
            Section {
                title: DemoText.t("Kapcsoló · jelölőnégyzet", "Switch · checkbox")
                flowSpacing: 20
                Shown { caption: "off"; TSwitch { text: DemoText.t("Lekeverés készítése", "Create mixdown") } }
                Shown { caption: "on"; TSwitch { text: DemoText.t("Résztvevők azonosítása hang alapján", "Identify participants by voice"); checked: true } }
                Shown { caption: "on hover"; TSwitch { checked: true; stateHovered: true } }
                Shown { caption: "focus"; TSwitch { stateFocused: true } }
                Shown { caption: "disabled"; TSwitch { text: DemoText.t("Letiltva", "Disabled"); checked: true; enabled: false } }
                Shown { caption: "off"; TCheckBox { text: DemoText.t("A súgóoldalak kiterjesztése", "Extending the help pages") } }
                Shown { caption: "on"; TCheckBox { text: DemoText.t("A mostani átirat maradjon meg másolatként", "Keep the current transcript as a copy"); checked: true } }
                Shown { caption: "hover"; TCheckBox { stateHovered: true } }
                Shown { caption: "focus"; TCheckBox { checked: true; stateFocused: true } }
                Shown { caption: "disabled"; TCheckBox { text: DemoText.t("Letiltva", "Disabled"); checked: true; enabled: false } }
            }

            // ------------------------------------------------- pill, chip, status
            Section {
                title: DemoText.t("Pirulák · chipek · állapotikonok", "Pills · chips · status icons")
                flowSpacing: 16
                Shown { caption: "warn"; TPill { text: DemoText.t("elavult", "outdated"); tone: "warn" } }
                Shown { caption: "warn"; TPill { text: DemoText.t("bizonytalan", "uncertain"); tone: "warn" } }
                Shown { caption: "success"; TPill { text: DemoText.t("javítva", "corrected"); tone: "success" } }
                Shown { caption: "danger"; TPill { text: DemoText.t("hiányzik a fájl", "file missing"); tone: "danger" } }
                Shown { caption: "neutral"; TPill { text: DemoText.t("eldobott · csendes", "discarded · silent") } }
                Shown { caption: "accent"; TPill { text: DemoText.t("fut", "running"); tone: "accent" } }
                Shown { caption: "removable"; TChip { text: DemoText.t("Nincs összefoglaló", "No summary"); removable: true } }
                Shown { caption: "person"; TChip { text: DemoText.name("Varga Nóra"); speakerIndex: 3; removable: true } }
                Shown { caption: "person"; TChip { text: DemoText.name("Kovács Lilla"); speakerIndex: 0 } }
                Shown { caption: "dashed add"; TChip { text: DemoText.t("Szűrő", "Filter"); dashed: true } }
                Shown { caption: "filter off"; TChip { text: DemoText.t("Bizonytalan 12", "Uncertain 12"); checkable: true } }
                Shown { caption: "filter on"; TChip { text: DemoText.t("Bizonytalan 12", "Uncertain 12"); checkable: true; checked: true } }
                Shown { caption: "hover"; TChip { text: DemoText.t("Nincs összefoglaló", "No summary"); removable: true; stateHovered: true } }
                Shown { caption: "focus"; TChip { text: DemoText.t("Szűrő", "Filter"); dashed: true; stateFocused: true } }
                Shown { caption: "done"; Row { spacing: 5
                    TStatusIcon { kind: "transcript"; state: "done"; toolTipText: DemoText.t("Átirat: kész", "Transcript: done") }
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

            // ------------------------------------------------- tabs, progress
            Section {
                title: DemoText.t("Fülek · folyamatjelző", "Tabs · progress indicator")
                flowSpacing: 32
                TTabBar {
                    width: 360
                    currentIndex: 1
                    TTabButton { text: DemoText.t("Átirat", "Transcript") }
                    TTabButton { text: DemoText.t("Összefoglaló", "Summary"); pillText: DemoText.t("elavult", "outdated") }
                    TTabButton { text: DemoText.t("Sávok", "Tracks") }
                }
                TTabBar {
                    width: 260
                    TTabButton { text: DemoText.t("Aktív", "Active") }
                    TTabButton { text: "Hover"; stateHovered: true }
                    TTabButton { text: DemoText.t("Fókusz", "Focus"); stateFocused: true }
                }
                Column {
                    spacing: 6
                    width: 240
                    Row {
                        width: parent.width
                        TLabel { text: DemoText.t("Átírás folyamatban…", "Transcription in progress…"); font.pixelSize: Theme.fontSmall; width: parent.width - pct.width }
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

            // ------------------------------------------------- Tanara Cloud: coming soon
            Section {
                objectName: "cloudComingSoonSection"
                title: DemoText.t("Tanara Cloud: hamarosan", "Tanara Cloud: coming soon")
                CloudComingSoon { width: Math.min(parent.width, 480) }
                CloudComingSoon { compact: true; width: Math.min(parent.width, 480) }
            }

            // ------------------------------------------------- banners, cards
            Section {
                title: DemoText.t("Bannerek", "Banners")
                TBanner {
                    width: parent.width
                    tone: "warn"
                    text: DemoText.t("Az összefoglaló óta 3 beszélőt javítottál, ezért a felelősök és a résztvevők elavultak lehetnek.", "You corrected 3 speakers since the summary, so owners and participants may be out of date.")
                    TButton { text: DemoText.t("Frissítés", "Update"); size: "small"; font.weight: Theme.weightSemiBold }
                    TButton { text: DemoText.t("Rendben így", "Keep as is"); size: "small"; variant: "ghost" }
                }
                TBanner {
                    width: parent.width
                    tone: "warn"
                    title: DemoText.t("Nincs beállítva átíró szolgáltató", "No transcription provider is set up")
                    text: DemoText.t("Add meg a saját szolgáltatód adatait a Beállításokban.", "Enter your own provider details in Settings.")
                    TButton { text: DemoText.t("Szolgáltató beállítása", "Set up provider"); size: "small"; trailingIconName: "arrow-right" }
                }
                TBanner {
                    width: parent.width
                    tone: "accent"
                    iconName: "wand-sparkles"
                    text: DemoText.t("Még 14 sor hasonlít erre a hangra. Átrakjam őket Fehér Ádámhoz?", "14 more lines sound like this voice. Move them to Ethan Hughes?")
                    TButton { text: DemoText.t("Átrakom", "Move them"); size: "small"; variant: "primary" }
                    TButton { text: DemoText.t("Megmutatom", "Show me"); size: "small"; variant: "ghost" }
                    TButton { text: DemoText.t("Nem", "No"); size: "small"; variant: "ghost" }
                }
                TBanner {
                    width: parent.width
                    tone: "danger"
                    title: DemoText.t("Az átírás nem sikerült", "Transcription failed")
                    text: DemoText.t("A szolgáltató elutasította a kérést: az API-kulcs érvénytelen vagy lejárt. A felvétel és a sávok érintetlenek.", "The provider rejected the request: the API key is invalid or has expired. The recording and the tracks are untouched.")
                    TButton { text: DemoText.t("Újrapróbálás", "Try again"); size: "small"; variant: "primary" }
                }
            }
            Section {
                title: DemoText.t("Kártyák", "Cards")
                TCard {
                    width: 300
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 6
                        TLabel { text: DemoText.t("Témánkénti elemzés", "Topic-by-topic analysis"); font.pixelSize: 15; font.weight: Theme.weightSemiBold }
                        TLabel { Layout.fillWidth: true; wrapMode: Text.Wrap; muted: true; font.pixelSize: Theme.fontSmall; text: DemoText.t("2 lépés, témánként 1–2 perc.", "2 steps, 1–2 minutes per topic.") }
                        TButton { text: DemoText.t("Témák javaslása", "Suggesting topics") }
                    }
                }
                TCard {
                    width: 300
                    tone: "accent"
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 6
                        RowLayout {
                            TLabel { text: DemoText.t("Gyors összefoglaló", "Quick summary"); font.pixelSize: 15; font.weight: Theme.weightSemiBold }
                            TPill { text: DemoText.t("ajánlott", "recommended"); tone: "accent" }
                        }
                        TLabel { Layout.fillWidth: true; wrapMode: Text.Wrap; muted: true; font.pixelSize: Theme.fontSmall; text: DemoText.t("Vezetői összefoglaló, döntések, teendők — kb. 1 perc.", "Executive summary, decisions, action items — about 1 minute.") }
                        TButton { text: DemoText.t("Összefoglaló készítése", "Creating summary"); variant: "primary" }
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
                            TLabel { text: DemoText.t("Az átírás nem sikerült", "Transcription failed"); font.pixelSize: Theme.fontHeading; font.weight: Theme.weightSemiBold }
                        }
                        TLabel { Layout.fillWidth: true; wrapMode: Text.Wrap; text: DemoText.t("A felvétel és a sávok érintetlenek.", "The recording and the tracks are untouched.") }
                        TLabel { text: "HTTP 401 · invalid_api_key"; mono: true; muted: true; font.pixelSize: Theme.fontCaption }
                    }
                }
            }

            // ------------------------------------------------- Settings controls
            Section {
                id: settingsSection
                title: DemoText.t("Beállítások (Settings*): szegmentált választó, rádió, kapcsoló-sor, léptető, lenyíló, állapot-pirula, navigáció", "Settings (Settings*): segmented picker, radio, switch row, stepper, dropdown, status pill, navigation")
                flowSpacing: 24
                property string seg: "light"
                property bool radio: true
                property bool sw: true
                property int minutes: 3
                property string combo: DemoText.en ? "en" : "hu"

                Column {
                    spacing: 8
                    SettingsSegmented {
                        options: [{ value: "system", label: DemoText.t("Rendszer", "System"), iconName: "monitor" },
                                  { value: "light", label: DemoText.t("Világos", "Light"), iconName: "sun" },
                                  { value: "dark", label: DemoText.t("Sötét", "Dark"), iconName: "moon" }]
                        value: settingsSection.seg
                        onPicked: (v) => settingsSection.seg = v
                    }
                    SettingsSegmented {
                        width: 220
                        stretch: true
                        options: [{ value: "fast", label: DemoText.t("Gyors", "Quick") }, { value: "accurate", label: DemoText.t("Pontos", "Accurate") }]
                        value: "accurate"
                    }
                }
                Column {
                    spacing: 8
                    SettingsRadio { text: DemoText.t("Automatikusan, a felvétel után", "Automatically, after the recording"); checked: !settingsSection.radio; onClicked: settingsSection.radio = false }
                    SettingsRadio { text: DemoText.t("Kézzel, a Sávok fülön", "Manually, on the Tracks tab"); checked: settingsSection.radio; onClicked: settingsSection.radio = true }
                }
                Column {
                    width: 330
                    spacing: 12
                    SettingsSwitchRow {
                        width: parent.width
                        text: DemoText.t("Induljon el a bejelentkezéskor", "Start when I log in")
                        helper: DemoText.t("Magyarázat a kapcsoló alatt, 13 px, halvány.", "Explanation under the switch, 13 px, muted.")
                        checked: settingsSection.sw
                        onToggled: (on) => settingsSection.sw = on
                    }
                    SettingsSwitchRow {
                        width: parent.width
                        large: true
                        text: DemoText.t("Hívásfigyelés", "Call watching")
                        checked: settingsSection.sw
                        onToggled: (on) => settingsSection.sw = on
                    }
                }
                Column {
                    spacing: 8
                    SettingsStepper {
                        width: 132
                        from: 0; to: 60
                        value: settingsSection.minutes
                        textFor: function(v) { return v === 0 ? DemoText.t("kikapcsolva", "off") : v + DemoText.t(" perc", " min") }
                        onMoved: (v) => settingsSection.minutes = v
                    }
                    SettingsCombo {
                        width: 220
                        options: [{ value: "auto", label: DemoText.t("Rendszer nyelve", "System language") }, { value: "hu", label: "Magyar", tag: DemoText.t("ajánlott", "recommended") },
                                  { value: "en", label: "English" }]
                        value: settingsSection.combo
                        onPicked: (v) => settingsSection.combo = v
                    }
                    SettingsCombo {
                        width: 220
                        editable: true
                        mono: true
                        fieldHeight: 32
                        options: DemoText.en ? ["model-a", "model-b"] : ["modell-a", "modell-b"]
                        value: DemoText.t("modell-a", "model-a")
                    }
                }
                Column {
                    spacing: 8
                    SettingsStatusPill { status: "ok"; text: DemoText.t("Kapcsolódva · 210 ms", "Connected · 210 ms") }
                    SettingsStatusPill { status: "failed"; text: DemoText.t("Nem érhető el", "Unreachable") }
                    SettingsStatusPill { status: "testing"; text: DemoText.t("Tesztelés…", "Testing…") }
                }
                Column {
                    width: 188
                    spacing: 2
                    SettingsNavItem { width: parent.width; text: DemoText.t("Általános", "General"); iconName: "settings"; current: true }
                    SettingsNavItem { width: parent.width; text: DemoText.t("Szolgáltatások", "Services"); iconName: "plug"; warn: true }
                }
            }

            // ------------------------------------------------- menu, popover, dialog
            Section {
                title: DemoText.t("Menü · popover · párbeszédablak (helyben kirajzolva; valódi felugró: overlay property)", "Menu · popover · dialog (drawn inline; real popup: overlay property)")
                flowSpacing: 40

                TSurface {
                    width: 250
                    height: menuCol.implicitHeight + 8
                    Column {
                        id: menuCol
                        x: 4; y: 4
                        width: parent.width - 8
                        TMenuItem { width: parent.width; text: DemoText.t("Átnevezés", "Rename"); iconName: "pencil"; shortcutText: "F2" }
                        TMenuItem { width: parent.width; text: DemoText.t("Megnyitás mappában", "Show in folder"); iconName: "folder-open"; stateHovered: true }
                        TMenuItem { width: parent.width; text: DemoText.t("Újra-átírás…", "Re-transcribe…"); iconName: "rotate-ccw" }
                        TMenuItem { width: parent.width; text: DemoText.t("Letiltott elem", "Disabled item"); iconName: "lock"; enabled: false }
                        TMenuItem { width: parent.width; text: DemoText.t("Sötét téma", "Dark theme"); iconName: "moon"; checked: true }
                        TMenuSeparator { width: parent.width }
                        TMenuItem { width: parent.width; text: DemoText.t("Törlés…", "Delete…"); iconName: "trash-2"; danger: true }
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
                        TSearchField { Layout.fillWidth: true; placeholderText: DemoText.t("Név keresése vagy új személy", "Search name or new person") }
                        TSectionLabel { text: DemoText.t("Másik személy", "Another person"); font.pixelSize: Theme.fontMicro }
                        Repeater {
                            model: 3
                            RowLayout {
                                required property int index
                                spacing: 10
                                TAvatar { name: root.speakerNames[parent.index + 1]; speakerIndex: parent.index + 1 }
                                TLabel { text: root.speakerNames[parent.index + 1]; Layout.fillWidth: true }
                                TLabel { text: (12 - parent.index * 4) + DemoText.t(" megbeszélés", " meetings"); muted: true; font.pixelSize: Theme.fontCaption }
                            }
                        }
                        TDivider { Layout.fillWidth: true }
                        TLabel {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            muted: true
                            font.pixelSize: Theme.fontCaption
                            text: DemoText.t("A beszélő névtelenül is maradhat; később is elnevezheted.", "Speakers can stay unnamed; you can name them later.")
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
                        TLabel { text: DemoText.t("Újra-átírod a megbeszélést?", "Re-transcribe the meeting?"); font.pixelSize: 18; font.weight: Theme.weightSemiBold }
                        TLabel {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            cssLineHeight: 1.55
                            text: DemoText.t("A mostani átirat és a benne lévő 23 kézi javítás elvész, az összefoglaló elavulttá válik. A hanglenyomatokba tanított javítások megmaradnak.", "The current transcript and its 23 manual corrections will be lost, and the summary becomes outdated. Corrections taught to the voiceprints are kept.")
                        }
                        TCheckBox { Layout.fillWidth: true; checked: true; text: DemoText.t("A mostani átirat maradjon meg másolatként", "Keep the current transcript as a copy") }
                        RowLayout {
                            Layout.topMargin: 6
                            spacing: 8
                            Item { Layout.fillWidth: true }
                            TButton { text: DemoText.t("Mégse", "Cancel") }
                            TButton { text: DemoText.t("Újra-átírás", "Re-transcribe"); variant: "danger" }
                        }
                    }
                }
            }

            Section {
                title: DemoText.t("Valódi felugrók (kattintásra)", "Real popups (on click)")
                TButton {
                    id: dialogButton
                    text: DemoText.t("Párbeszédablak…", "Dialog…")
                    onClicked: demoDialog.open()
                }
                TButton {
                    id: menuButton
                    text: DemoText.t("Menü", "Menu")
                    trailingIconName: "chevron-down"
                    onClicked: demoMenu.open()
                    TMenu {
                        id: demoMenu
                        y: menuButton.height + 4
                        width: 250
                        TMenuItem { text: DemoText.t("Átnevezés", "Rename"); iconName: "pencil" }
                        TMenuItem { text: DemoText.t("Megnyitás mappában", "Show in folder"); iconName: "folder-open" }
                        TMenuItem { text: DemoText.t("Újra-átírás…", "Re-transcribe…"); iconName: "rotate-ccw" }
                        TMenuSeparator {}
                        TMenuItem { text: DemoText.t("Törlés…", "Delete…"); iconName: "trash-2"; danger: true }
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
                            TSearchField { Layout.fillWidth: true; placeholderText: DemoText.t("Név keresése vagy új személy", "Search name or new person") }
                            TLabel { Layout.fillWidth: true; wrapMode: Text.Wrap; muted: true; font.pixelSize: Theme.fontCaption
                                     text: DemoText.t("A beszélő névtelenül is maradhat; később is elnevezheted.", "Speakers can stay unnamed; you can name them later.") }
                        }
                    }
                }
            }

            Section {
                title: DemoText.t("Címkék (T01: chip, beviteli mező, a fejléc címkesora)", "Tags (T01: chip, input field, the header tag row)")
                TagGallerySection { width: page.width }
            }

            // ------------------------------------------------- meeting shell: evidence, time, marker
            Section {
                title: DemoText.t("Bizonyíték-chip (EvidenceChip): 3 polaritás × fajták", "Evidence chip (EvidenceChip): 3 polarities × kinds")
                flowSpacing: 10
                // support
                Shown { caption: "support · voice"; EvidenceChip { kind: "voice"; polarity: "support"; text: DemoText.t("hang 84%", "voice 84%") } }
                Shown { caption: "support · side mic"; EvidenceChip { kind: "side"; side: "mic"; polarity: "support"; text: DemoText.t("mikrofon-sáv", "mic track") } }
                Shown { caption: "support · side loopback"; EvidenceChip { kind: "side"; side: "loopback"; polarity: "support"; text: DemoText.t("hívás-sáv", "call track") } }
                Shown { caption: "support · calendar"; EvidenceChip { kind: "calendar"; polarity: "support"; text: DemoText.t("meghívott", "invited") } }
                Shown { caption: "support · tag"; EvidenceChip { kind: "tag"; polarity: "support"; text: "#Nordvik" } }
                Shown { caption: "support · lines"; EvidenceChip { kind: "lines"; polarity: "support"; text: DemoText.t("86 sor", "86 lines") } }
                Shown { caption: "support · similarity"; EvidenceChip { kind: "similarity"; polarity: "support"; text: DemoText.t("hasonló hang", "similar voice") } }
                Shown { caption: "support · manual"; EvidenceChip { kind: "manual"; polarity: "support"; text: DemoText.t("kézi beosztás", "manual assignment") } }
                Shown { caption: DemoText.t("support · quiet (V2 sorok)", "support · quiet (V2 lines)"); EvidenceChip { kind: "voice"; polarity: "support"; quiet: true; text: DemoText.t("hang 91%", "voice 91%") } }
                // neutral
                Shown { caption: "neutral · voice"; EvidenceChip { kind: "voice"; polarity: "neutral"; text: DemoText.t("hang 52%", "voice 52%") } }
                Shown { caption: "neutral · side loopback"; EvidenceChip { kind: "side"; side: "loopback"; polarity: "neutral"; text: DemoText.t("hívás-sáv", "call track") } }
                Shown { caption: "neutral · side"; EvidenceChip { kind: "side"; polarity: "neutral"; text: DemoText.t("egyik sáv", "one track") } }
                Shown { caption: "neutral · lines"; EvidenceChip { kind: "lines"; polarity: "neutral"; text: DemoText.t("7 sor", "7 lines") } }
                // contradict
                Shown { caption: "contradict · voice"; EvidenceChip { kind: "voice"; polarity: "contradict"; text: DemoText.t("hang 31%", "voice 31%") } }
                Shown { caption: "contradict · side"; EvidenceChip { kind: "side"; side: "loopback"; polarity: "contradict"; text: DemoText.t("másik sávon", "on the other track") } }
                Shown { caption: "contradict · tag (⚠)"; EvidenceChip { kind: "tag"; polarity: "contradict"; text: DemoText.t("#Nordvik nélkül", "without #Nordvik") } }
            }

            Section {
                title: DemoText.t("Idő-chip (TimeChip): alap · hover · fókusz · aktív · figyelmeztető · szakasz (play)", "Time chip (TimeChip): default · hover · focus · active · warning · section (play)")
                flowSpacing: 14
                Shown { caption: DemoText.t("alap", "default"); TimeChip { text: "12:41" } }
                Shown { caption: "stateHovered"; TimeChip { text: "12:41"; stateHovered: true } }
                Shown { caption: "stateFocused"; TimeChip { text: "12:41"; stateFocused: true } }
                Shown { caption: "active"; TimeChip { text: "12:41"; active: true } }
                Shown { caption: "warn"; TimeChip { text: "12:41"; warn: true } }
                Shown { caption: "iconName play"; TimeChip { text: "31:10–44:05"; iconName: "play" } }
                Shown { caption: "large + play"; TimeChip { text: "31:10–44:05"; iconName: "play"; large: true } }
                Shown { caption: "large + active"; TimeChip { text: "44:05–58:30"; iconName: "play"; large: true; active: true } }
            }

            Section {
                title: DemoText.t("Átirat-jelölő (TranscriptMarker): Javítás mód, soronként", "Transcript marker (TranscriptMarker): Fix mode, per line")
                flowSpacing: 14
                Shown { caption: "confirmed"; TranscriptMarker { kind: "confirmed" } }
                Shown { caption: "short"; TranscriptMarker { kind: "short" } }
                Shown { caption: "uncertain · side"; TranscriptMarker { kind: "uncertain"; reason: "side" } }
                Shown { caption: "uncertain · voice"; TranscriptMarker { kind: "uncertain"; reason: "voice" } }
                Shown { caption: "uncertain · tag"; TranscriptMarker { kind: "uncertain"; reason: "tag" } }
                Shown { caption: "corrected"; TranscriptMarker { kind: "corrected" } }
                Shown { caption: "newPerson"; TranscriptMarker { kind: "newPerson" } }
                Shown { caption: "noisy"; TranscriptMarker { kind: "noisy" } }
            }

            Section {
                title: DemoText.t("Jelölt-sor (CandidateRow) és ok-sor (EvidenceReason)", "Candidate row (CandidateRow) and reason row (EvidenceReason)")
                flowSpacing: 18
                Shown {
                    caption: DemoText.t("CandidateRow · javasolt (erős bizonyíték, 1)", "CandidateRow · suggested (strong evidence, 1)")
                    CandidateRow {
                        width: 380
                        personName: DemoText.name("Fehér Gábor"); colorIndex: 1; subText: DemoText.t("86 sor itt", "86 lines here"); keyHint: "1"
                        strongEvidence: true; highlighted: true
                        evidence: [{ kind: "voice", polarity: "support", text: DemoText.t("hang 84%", "voice 84%") },
                                   { kind: "side", side: "loopback", polarity: "support", text: DemoText.t("hívás-sáv", "call track") }]
                    }
                }
                Shown {
                    caption: DemoText.t("CandidateRow · most ő (halvány) / meetingen kívüli", "CandidateRow · currently them (dimmed) / not in the meeting")
                    Column {
                        spacing: 6
                        CandidateRow {
                            width: 380
                            personName: DemoText.name("Kovács Lilla"); colorIndex: 0; subText: DemoText.t("most ő", "currently them"); keyHint: "2"; dimmed: true
                            evidence: [{ kind: "voice", polarity: "contradict", text: DemoText.t("hang 31%", "voice 31%") }]
                        }
                        CandidateRow {
                            width: 380
                            personName: DemoText.name("Varga Árpád"); colorIndex: -1; subText: DemoText.t("12 megbeszélés", "12 meetings"); keyHint: "3"
                            evidence: [{ kind: "tag", polarity: "support", text: "#Nordvik" },
                                       { kind: "voice", polarity: "neutral", text: DemoText.t("hang 52%", "voice 52%") }]
                        }
                    }
                }
                Shown {
                    caption: DemoText.t("EvidenceReason · mellette szól / ellene szól (javítás-link)", "EvidenceReason · for / against (fix link)")
                    Column {
                        spacing: 4
                        EvidenceReason {
                            width: 420
                            evidence: ({ kind: "voice", polarity: "support", label: DemoText.t("Hang:", "Voice:"),
                                         sentence: DemoText.t("a mintáihoz 84%-ban hasonlít", "84% similar to their samples"), fixLabel: DemoText.t("Minták", "Samples") })
                        }
                        EvidenceReason {
                            width: 420
                            evidence: ({ kind: "side", side: "loopback", polarity: "contradict", label: DemoText.t("Sáv:", "Track:"),
                                         sentence: DemoText.t("a hívás-sávon szól, ő a mikrofonon szokott", "speaks on the call track, they usually use the mic"), fixLabel: DemoText.t("Sáv-beosztás", "Track assignment") })
                        }
                        EvidenceReason {
                            width: 420
                            evidence: ({ kind: "similarity", polarity: "neutral", label: DemoText.t("Hasonló hang:", "Similar voice:"),
                                         sentence: DemoText.t("Fehér Gáboré is közel van", "Daniel Hughes is close too"), fixLabel: DemoText.t("Kettejük átnézése", "Review the two") })
                        }
                    }
                }
            }

            Section {
                title: DemoText.t("Áttekintés: címkemező javaslattal (TagRowOverview) · ADATOK csempék (StatTiles) · FELDOLGOZÁS lépései (PipelineSteps)", "Overview: tag field with suggestions (TagRowOverview) · DATA tiles (StatTiles) · PROCESSING steps (PipelineSteps)")
                flowSpacing: 24
                Shown {
                    caption: DemoText.t("TagRowOverview · felrakott címkék + javaslatok (MeetingTagsModel demoState=\"similar\")", "TagRowOverview · applied tags + suggestions (MeetingTagsModel demoState=\"similar\")")
                    TagRowOverview {
                        width: 520
                        model: MeetingTagsModel { demoState: "similar" }
                    }
                }
                Shown {
                    caption: DemoText.t("TagRowOverview · a résztvevők címkéiből (demoState=\"people\", átírás előtt is)", "TagRowOverview · from the participants' tags (demoState=\"people\", even before transcription)")
                    TagRowOverview {
                        width: 520
                        model: MeetingTagsModel { demoState: "people" }
                    }
                }
                Shown {
                    caption: DemoText.t("TagRowOverview · üres (demoState=\"none\")", "TagRowOverview · empty (demoState=\"none\")")
                    TagRowOverview {
                        width: 340
                        model: MeetingTagsModel { demoState: "none" }
                    }
                }
                Shown {
                    caption: "StatTiles · 2 × 3"
                    StatTiles {
                        width: 340
                        stats: [{ label: DemoText.t("HOSSZ", "LENGTH"), value: "2:11:04", sub: DemoText.t("felvétel", "recording") },
                                { label: DemoText.t("MEGSZÓLALÁS", "UTTERANCES"), value: "1298", sub: DemoText.t("átirat", "transcript") },
                                { label: DemoText.t("BESZÉLŐ", "SPEAKERS"), value: "5", sub: DemoText.t("4 azonosítva", "4 identified") },
                                { label: DemoText.t("SÁV", "TRACKS"), value: "3", sub: DemoText.t("1 eldobott", "1 discarded") },
                                { label: DemoText.t("FÁJLOK", "FILES"), value: "412 MB", sub: DemoText.t("hang", "audio") },
                                { label: DemoText.t("ÁTLAG", "AVERAGE"), value: "84%", sub: DemoText.t("hang-biztonság", "voice confidence") }]
                    }
                }
                Shown {
                    caption: DemoText.t("PipelineSteps · done · running (haladás) · attention · waiting", "PipelineSteps · done · running (progress) · attention · waiting")
                    PipelineSteps {
                        width: 340
                        steps: [{ key: "recording", title: DemoText.t("Felvétel", "Recording"), sub: DemoText.t("2:11:04 · 3 sáv", "2:11:04 · 3 tracks"), state: "done", parallel: false, progress: -1 },
                                { key: "mixdown", title: DemoText.t("Lekeverés", "Mixdown"), sub: DemoText.t("elavult — újra kell készíteni", "outdated — needs to be redone"), state: "attention", parallel: false, progress: -1 },
                                { key: "voice", title: DemoText.t("Hanglenyomat-elemzés", "Voiceprint analysis"), sub: "42%", state: "running", parallel: true, progress: 42 },
                                { key: "transcript", title: DemoText.t("Átirat", "Transcript"), sub: DemoText.t("fut", "running"), state: "running", parallel: true, progress: -2 },
                                { key: "summary", title: DemoText.t("Összefoglaló", "Summary"), sub: DemoText.t("az átirat után", "after the transcript"), state: "waiting", parallel: false, progress: -1 }]
                    }
                }
            }

            Section {
                title: DemoText.t("Személyek-címkék és hangmodellek: TagPeopleList · SettingsVoiceModelCard (állapotonként)", "People tags and voice models: TagPeopleList · SettingsVoiceModelCard (per state)")
                flowSpacing: 24
                Shown {
                    caption: DemoText.t("TagPeopleList · felrakott / csak tanult / előre felvett", "TagPeopleList · applied / learned only / added in advance")
                    TagPeopleList {
                        width: 420
                        people: [{ name: DemoText.name("Kovács Lilla"), monogram: DemoText.t("KL", "EC"), countText: "11 / 14", manual: true },
                                 { name: DemoText.name("Fehér Gábor"), monogram: DemoText.t("FG", "DH"), countText: "9 / 14", manual: false },
                                 { name: DemoText.name("Varga Árpád"), monogram: DemoText.t("VÁ", "JW"), countText: "0 / 14", manual: true }]
                    }
                }
                Shown {
                    caption: DemoText.t("TagPeopleList · üres", "TagPeopleList · empty")
                    TagPeopleList { width: 420; people: []; emptyText: DemoText.t("Még nincs személy ezen a címkén.", "No one has this tag yet.") }
                }
                Shown {
                    caption: DemoText.t("SettingsVoiceModelCard · missing (ajánlott)", "SettingsVoiceModelCard · missing (recommended)")
                    SettingsVoiceModelCard {
                        width: 420
                        name: "CAM++"; recommended: true; blurb: DemoText.t("Gyors, magyar beszédre is jó beszélő-felismerő.", "Fast speaker recognizer, good for Hungarian speech too.")
                        license: "Apache-2.0"; sourceText: "modelscope"; sizeText: "28 MB"; dimText: "192 dim"
                        state_: "missing"
                    }
                }
                Shown {
                    caption: "downloading"
                    SettingsVoiceModelCard {
                        width: 420
                        name: "WeSpeaker"; blurb: DemoText.t("Pontosabb, de lassabb.", "More accurate, but slower.")
                        license: "CC-BY-4.0"; sizeText: "26 MB"; dimText: "256 dim"
                        state_: "downloading"; progress: 0.42; progressText: "11 / 26 MB"
                    }
                }
                Shown {
                    caption: DemoText.t("downloaded · bekapcsolva", "downloaded · enabled")
                    SettingsVoiceModelCard {
                        width: 420
                        name: "CAM++"; recommended: true; blurb: DemoText.t("Gyors, magyar beszédre is jó beszélő-felismerő.", "Fast speaker recognizer, good for Hungarian speech too.")
                        license: "Apache-2.0"; sizeText: "28 MB"; dimText: "192 dim"
                        state_: "downloaded"; enabledModel: true; canEnable: true
                    }
                }
                Shown {
                    caption: "error / offline"
                    Column {
                        spacing: 8
                        SettingsVoiceModelCard {
                            width: 420; name: "WeSpeaker"; blurb: DemoText.t("Pontosabb, de lassabb.", "More accurate, but slower."); license: "CC-BY-4.0"
                            sizeText: "26 MB"; state_: "error"; errorText: DemoText.t("A letöltés megszakadt (hálózati hiba).", "The download was interrupted (network error).")
                        }
                        SettingsVoiceModelCard {
                            width: 420; name: "WeSpeaker"; blurb: DemoText.t("Pontosabb, de lassabb.", "More accurate, but slower."); license: "CC-BY-4.0"
                            sizeText: "26 MB"; state_: "offline"
                        }
                    }
                }
            }

            Section {
                title: DemoText.t("Sávok és tanulás (javító kör): TrackActivityStrip · ParticipantTrackChips · TagLearnBar · BackfillSkipNote", "Tracks and learning (fix round): TrackActivityStrip · ParticipantTrackChips · TagLearnBar · BackfillSkipNote")
                flowSpacing: 24
                // Fictional, deterministic speech frames (demo data of TrackActivityModel: mic,
                // call, system, mic2).
                TrackActivityModel { id: galleryActivity }
                TrackActivityModel { id: galleryActivityLoading; demoState: "loading" }
                Shown {
                    caption: DemoText.t("TrackActivityStrip · compact (18 px) · lejátszás-fejjel", "TrackActivityStrip · compact (18 px) · with playhead")
                    TrackActivityStrip {
                        width: 320
                        compact: true
                        activity: galleryActivity; trackId: "mic"; positionMs: 612000
                    }
                }
                Shown {
                    caption: DemoText.t("TrackActivityStrip · nagy (40 px), hívás-sáv a sáv színével", "TrackActivityStrip · large (40 px), call track in the track colour")
                    TrackActivityStrip {
                        width: 420
                        activity: galleryActivity; trackId: "call"; color: Theme.speakerLine(1); positionMs: 1200000
                    }
                }
                Shown {
                    caption: DemoText.t("TrackActivityStrip · néma sáv · dimmed (kimaradt) · számol (lüktet)", "TrackActivityStrip · silent track · dimmed (skipped) · computing (pulses)")
                    Column {
                        spacing: 8
                        TrackActivityStrip { width: 420; compact: true; activity: galleryActivity; trackId: "system" }
                        TrackActivityStrip { width: 420; compact: true; activity: galleryActivity; trackId: "mic2"; dimmed: true }
                        TrackActivityStrip { width: 420; compact: true; activity: galleryActivityLoading; trackId: "mic" }
                    }
                }
                ParticipantTrackBinder { id: galleryBinder }
                Shown {
                    caption: DemoText.t("ParticipantTrackChips · tanult mikrofon (p1) · tanult hívás (p2) · kézzel mindkettő (p3)", "ParticipantTrackChips · learned mic (p1) · learned call (p2) · both by hand (p3)")
                    Column {
                        spacing: 8
                        ParticipantTrackChips { binder: galleryBinder; participantId: "p1" }
                        ParticipantTrackChips { binder: galleryBinder; participantId: "p2" }
                        ParticipantTrackChips { binder: galleryBinder; participantId: "p3" }
                    }
                }
                Shown {
                    caption: DemoText.t("CandidateRow · gyors „+ #Címke” (quickTagLabel) · EvidenceReason · quickLabel", "CandidateRow · quick “+ #Tag” (quickTagLabel) · EvidenceReason · quickLabel")
                    Column {
                        spacing: 8
                        CandidateRow {
                            width: 380
                            personName: DemoText.name("Fehér Gábor"); colorIndex: 1; subText: DemoText.t("86 sor itt", "86 lines here"); keyHint: "1"
                            quickTagLabel: "+ #Nordvik"
                            evidence: [{ kind: "voice", polarity: "support", text: DemoText.t("hang 84%", "voice 84%") }]
                        }
                        EvidenceReason {
                            width: 420
                            quickLabel: DemoText.t("+ #Nordvik ráteszem", "+ apply #Nordvik")
                            evidence: ({ kind: "tag", polarity: "neutral", label: DemoText.t("Címke:", "Tag:"),
                                         sentence: DemoText.t("a #Nordvik még nincs rajta", "doesn't have #Nordvik yet"), fixLabel: DemoText.t("Címkéi", "Tags") })
                        }
                    }
                }
                Shown {
                    caption: DemoText.t("TagLearnBar · kérdés két párral (Igen · Nem · Egyenként…)", "TagLearnBar · question with two pairs (Yes · No · One by one…)")
                    TagLearnBar {
                        width: 640
                        question: ({ text: DemoText.t("Fehér Gábor és Varga Árpád másodszor van #Nordvik megbeszélésen. Rájuk teszem a címkét?", "Daniel Hughes and James Walker are in a #Nordvik meeting for the second time. Add the tag to them?"),
                                     pairs: [{ index: 0, name: DemoText.name("Fehér Gábor"), tagName: "Nordvik", text: DemoText.t("másodszor", "for the second time") },
                                             { index: 1, name: DemoText.name("Varga Árpád"), tagName: "Nordvik", text: DemoText.t("másodszor", "for the second time") }] })
                    }
                }
                Shown {
                    caption: DemoText.t("TagLearnBar · az „Egyenként…” lista nyitva (expanded)", "TagLearnBar · the “One by one…” list open (expanded)")
                    TagLearnBar {
                        width: 640
                        expanded: true
                        question: ({ text: DemoText.t("Fehér Gábor és Varga Árpád másodszor van #Nordvik megbeszélésen. Rájuk teszem a címkét?", "Daniel Hughes and James Walker are in a #Nordvik meeting for the second time. Add the tag to them?"),
                                     pairs: [{ index: 0, name: DemoText.name("Fehér Gábor"), tagName: "Nordvik", text: DemoText.t("másodszor", "for the second time") },
                                             { index: 1, name: DemoText.name("Varga Árpád"), tagName: "Nordvik", text: DemoText.t("másodszor", "for the second time") }] })
                    }
                }
                Shown {
                    caption: DemoText.t("TagLearnBar · embedded (a változás-sáv sorában, saját háttér nélkül)", "TagLearnBar · embedded (in the change bar row, without its own background)")
                    Rectangle {
                        width: 640; height: 52; radius: Theme.radiusPopup; color: Theme.text
                        TagLearnBar {
                            anchors.fill: parent
                            embedded: true
                            question: ({ text: DemoText.t("Szabó Bence új személy. Ráteszem a #Nordvik címkét?", "Noah Turner is a new person. Add the #Nordvik tag?"), pairs: [] })
                        }
                    }
                }
                // C++ helper (not visual): the People detail panel's "N samples cannot be
                // refilled" row.
                BackfillSkipNote { id: gallerySkipNote; person: DemoText.name("Fehér Gábor"); demoState: "samplesModels" }
                Shown {
                    caption: DemoText.t("BackfillSkipNote · text + detail (tooltip) — a Személyek ablak jelzése", "BackfillSkipNote · text + detail (tooltip) — the People window notice")
                    Column {
                        spacing: 4
                        Row {
                            spacing: 6
                            TIcon { anchors.verticalCenter: parent.verticalCenter; name: "triangle-alert"; size: 13; color: Theme.warn }
                            TLabel { text: gallerySkipNote.text; muted: true; font.pixelSize: Theme.fontSmall }
                        }
                        Caption { text: gallerySkipNote.detail }
                    }
                }
            }

            Section {
                title: DemoText.t("Egyéb: elválasztó, szaggatott keret, helykitöltő", "Other: divider, dashed frame, placeholder")
                TDivider { width: 200 }
                TDashedRect { width: 120; height: 34 }
                TDashedRect { width: 24; height: 24; radius: 12; color: Theme.accent }
                TPlaceholder { width: 260; height: 70; label: "Valami.qml"; note: DemoText.t("Helykitöltő a héj-vázban", "Placeholder in the shell frame") }
            }
        }
    }

    TDialog {
        id: demoDialog
        title: DemoText.t("Újra-átírod a megbeszélést?", "Re-transcribe the meeting?")
        TLabel {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            cssLineHeight: 1.55
            text: DemoText.t("A mostani átirat és a benne lévő 23 kézi javítás elvész, az összefoglaló elavulttá válik. A hanglenyomatokba tanított javítások megmaradnak.", "The current transcript and its 23 manual corrections will be lost, and the summary becomes outdated. Corrections taught to the voiceprints are kept.")
        }
        TCheckBox { Layout.fillWidth: true; checked: true; text: DemoText.t("A mostani átirat maradjon meg másolatként", "Keep the current transcript as a copy") }
        actions: [
            TButton { text: DemoText.t("Mégse", "Cancel"); onClicked: demoDialog.reject() },
            TButton { text: DemoText.t("Újra-átírás", "Re-transcribe"); variant: "danger"; onClicked: demoDialog.accept() }
        ]
    }

    Component.onCompleted: {
        if (overlay === "dialog") {
            demoDialog.open()
        } else if (overlay === "menu" || overlay === "popover") {
            // After layout is done we scroll to the buttons, and only then open.
            Qt.callLater(() => {
                flick.contentY = Math.max(0, flick.contentHeight - flick.height)
                if (overlay === "menu") demoMenu.open(); else demoPopover.open()
            })
        }
    }
}
