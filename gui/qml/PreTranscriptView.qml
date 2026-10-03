import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// Átirat előtti nézet — a fülek helyett látszik, amíg a megbeszélésnek nincs átirata.
// Három állapot a feldolgozási állapotból (PreTranscriptViewModel.state):
//   "steps"   M03: kontextus → előkészítés → átírás (a hiány megnevezve, a gomb odavisz)
//   "running" M04: szakasz-lista valós haladással, megszakítható
//   "failed"  M05: hibakártya újrapróbálással és javító művelettel
// Képernyőképhez: --qml-prop 'demoState="steps|ready|cloud|mixing|running|uploading|failed"'.
Item {
    id: root

    property string meetingId: ""      // a kijelölt megbeszélés; "" = nincs
    property var player: null          // PlayerController vagy null (itt nem használt)
    property var shell: null           // ShellActions vagy null
    property string demoState: ""

    PreTranscriptViewModel {
        id: vm
        meetingId: root.meetingId
        demoState: root.demoState
    }

    // A Beállítások bezárása után a kapuzás újraolvasása (kulcs került be / ki).
    onVisibleChanged: if (visible) vm.refresh()
    Connections {
        target: Qt.application
        function onStateChanged() { if (Qt.application.state === Qt.ApplicationActive && root.visible) vm.refresh() }
    }

    function openSettings(page) { if (root.shell) root.shell.openSettings(page) }
    function commitContext() { vm.contextNote = contextArea.text }
    function start() {
        commitContext()
        if (root.shell) root.shell.startTranscription(root.meetingId)
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: content.implicitHeight + 48
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        T.ScrollBar.vertical: TScrollBar {}

        Item {
            id: content
            x: 24; y: 24
            width: flick.width - 48
            implicitHeight: vm.state === "steps" ? steps.implicitHeight
                          : vm.state === "running" ? runningBox.implicitHeight
                          : vm.state === "failed" ? failed.implicitHeight : 0

            // ================= M03 — lépések =================
            ColumnLayout {
                id: steps
                visible: vm.state === "steps"
                width: Math.min(parent.width, 760)
                spacing: 0

                TLabel {
                    Layout.fillWidth: true
                    Layout.bottomMargin: 16
                    text: qsTr("Ehhez a felvételhez még nincs átirat. Három lépés:")
                    muted: true
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.Wrap
                }

                PreTranscriptStep {
                    Layout.fillWidth: true
                    number: 1
                    tone: "current"
                    title: qsTr("Miről szólt a megbeszélés?")

                    TLabel {
                        Layout.fillWidth: true
                        text: qsTr("Nevek, szakszavak, témák: ezekből pontosabb átirat lesz. Elhagyható.")
                        muted: true
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.Wrap
                    }
                    TTextArea {
                        id: contextArea
                        Layout.fillWidth: true
                        placeholderText: qsTr("Például: negyedéves egyeztetés a partnerekkel; szóba kerül a számlázás és az új súgó.")
                        Component.onCompleted: text = vm.contextNote
                        onEditingFinished: root.commitContext()
                        onTextChanged: if (activeFocus) saveTimer.restart()
                        Accessible.name: qsTr("Miről szólt a megbeszélés?")
                        Timer { id: saveTimer; interval: 900; onTriggered: root.commitContext() }
                        Connections {
                            target: vm
                            function onContextNoteChanged() {
                                if (!contextArea.activeFocus && contextArea.text !== vm.contextNote)
                                    contextArea.text = vm.contextNote
                            }
                            function onMeetingIdChanged() { contextArea.text = vm.contextNote }
                        }
                    }
                }

                PreTranscriptStep {
                    Layout.fillWidth: true
                    number: 2
                    tone: "next"
                    title: qsTr("Előkészítés")
                    tag: qsTr("opcionális")
                    contentSpacing: 10

                    // --- Résztvevők azonosítása hang alapján ---
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        TSwitch {
                            Layout.alignment: Qt.AlignTop
                            Layout.topMargin: 1
                            enabled: vm.identifyAvailable
                            checked: vm.identifyEnabled
                            onToggled: vm.identifyEnabled = checked
                            Accessible.name: qsTr("Résztvevők azonosítása hang alapján")
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            TLabel {
                                Layout.fillWidth: true
                                text: qsTr("Résztvevők azonosítása hang alapján")
                                font.weight: Theme.weightMedium
                                wrapMode: Text.Wrap
                            }
                            TLabel {
                                Layout.fillWidth: true
                                text: vm.identifyAvailable
                                    ? qsTr("Az ismert személyeket a hanglenyomatuk alapján előre megnevezi. Kihagyható: az átirat névtelen beszélőkkel is elkészül.")
                                    : qsTr("Ezen a gépen nincs telepítve a hangfelismerő modell, ezért ez a lépés kimarad. Az átirat névtelen beszélőkkel készül el, a neveket utólag is megadhatod.")
                                muted: true
                                font.pixelSize: Theme.fontSmall
                                wrapMode: Text.Wrap
                            }
                        }
                    }

                    // --- Lekeverés: az átírás ebből dolgozik, ezért nem kapcsolható ki; itt az
                    //     állapota látszik, és előre elkészíthető. ---
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        Item {
                            implicitWidth: 34; implicitHeight: 20
                            Layout.alignment: Qt.AlignTop
                            Layout.topMargin: 1
                            TIcon {
                                anchors.centerIn: parent
                                visible: vm.mixdownState !== "running"
                                name: vm.mixdownState === "ready" ? "circle-check" : "circle-dot"
                                size: 18
                                color: vm.mixdownState === "ready" ? Theme.successInk : Theme.textMuted
                            }
                            TSpinner {
                                anchors.centerIn: parent
                                visible: vm.mixdownState === "running"
                                size: 18
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            TLabel {
                                Layout.fillWidth: true
                                text: qsTr("Lekeverés készítése")
                                font.weight: Theme.weightMedium
                                wrapMode: Text.Wrap
                            }
                            TLabel {
                                Layout.fillWidth: true
                                text: vm.mixdownState === "ready"
                                        ? qsTr("Kész: a sávokból egy fájl a visszahallgatáshoz. Az átírás is ebből dolgozik.")
                                    : vm.mixdownState === "running"
                                        ? qsTr("Most készül a sávokból egy fájl a visszahallgatáshoz és az átíráshoz.")
                                    : vm.mixdownState === "stale"
                                        ? qsTr("A sávok változtak a legutóbbi lekeverés óta. Az átírás indításakor magától frissül, de előre is elkészítheted.")
                                        : qsTr("A sávokból egy fájl a visszahallgatáshoz. Az átírás is ebből dolgozik, ezért az indításkor magától elkészül; előre is elkészítheted.")
                                muted: true
                                font.pixelSize: Theme.fontSmall
                                wrapMode: Text.Wrap
                            }
                            RowLayout {
                                visible: vm.mixdownState === "running"
                                Layout.fillWidth: true
                                Layout.topMargin: 4
                                spacing: 10
                                TProgressBar {
                                    Layout.fillWidth: true
                                    Layout.maximumWidth: 320
                                    thickness: 6
                                    indeterminate: vm.mixdownPercent < 0
                                    value: vm.mixdownPercent < 0 ? 0 : vm.mixdownPercent / 100
                                }
                                TLabel {
                                    visible: vm.mixdownPercent >= 0
                                    text: vm.mixdownPercent + "%"
                                    mono: true; muted: true
                                    font.pixelSize: Theme.fontCaption
                                }
                                TButton {
                                    visible: vm.mixdownCancellable
                                    text: qsTr("Megszakítás")
                                    variant: "ghost"; size: "small"
                                    onClicked: if (root.shell) root.shell.cancelJob(root.meetingId, JobKinds.Mixdown)
                                }
                                Item { Layout.fillWidth: true }
                            }
                            TButton {
                                visible: vm.mixdownState === "missing" || vm.mixdownState === "stale"
                                Layout.topMargin: 4
                                text: vm.mixdownState === "stale" ? qsTr("Lekeverés frissítése") : qsTr("Lekeverés most")
                                size: "small"
                                iconName: "audio-lines"
                                onClicked: vm.startMixdown()
                            }
                        }
                    }
                }

                PreTranscriptStep {
                    Layout.fillWidth: true
                    number: 3
                    tone: "idle"
                    last: true
                    title: qsTr("Átírás")
                    contentSpacing: 10

                    // Hiányzó beállítás: megnevezve, a gomb odavisz.
                    TBanner {
                        visible: vm.blocker.title !== undefined
                        Layout.fillWidth: true
                        tone: "warn"
                        title: vm.blocker.title || ""
                        text: vm.blocker.text || ""
                        TButton {
                            visible: (vm.blocker.actionLabel || "") !== ""
                            text: vm.blocker.actionLabel || ""
                            size: "small"
                            implicitHeight: 30
                            leftPadding: 12; rightPadding: 12
                            font.weight: Theme.weightSemiBold
                            trailingIconName: "arrow-right"
                            iconSize: 14
                            spacing: 6
                            onClicked: root.openSettings(vm.blocker.actionPage || "")
                        }
                    }

                    // Tanara Cloud: szint (Gyors / Pontos) és nyelv az indító gomb fölött.
                    ColumnLayout {
                        visible: vm.cloudSelected
                        Layout.fillWidth: true
                        spacing: 8
                        Flow {
                            Layout.fillWidth: true
                            spacing: 8
                            TLabel {
                                text: qsTr("Tanara Cloud · az átírás szintje:")
                                font.pixelSize: Theme.fontSmall
                                height: 24
                                verticalAlignment: Text.AlignVCenter
                            }
                            TChip {
                                visible: vm.cloudExpertName === ""
                                text: qsTr("Gyors")
                                tone: vm.cloudTier === "fast" ? "accent" : "neutral"
                                onClicked: vm.cloudTier = "fast"
                            }
                            TChip {
                                visible: vm.cloudExpertName === ""
                                text: qsTr("Pontos")
                                tone: vm.cloudTier === "accurate" ? "accent" : "neutral"
                                onClicked: vm.cloudTier = "accurate"
                            }
                            TChip {
                                visible: vm.cloudExpertName !== ""
                                text: qsTr("Expert: %1").arg(vm.cloudExpertName)
                                removable: true
                                onRemoved: vm.cloudTier = "accurate"
                            }
                            TLabel {
                                text: "· " + vm.cloudLanguageLabel
                                muted: true
                                font.pixelSize: Theme.fontSmall
                                height: 24
                                verticalAlignment: Text.AlignVCenter
                            }
                            TButton {
                                text: qsTr("Nyelv és Expert mód…")
                                variant: "ghost"; size: "small"
                                implicitHeight: 24
                                onClicked: root.openSettings("cloud")
                            }
                        }
                        TBanner {
                            visible: vm.cloudWarning !== ""
                            Layout.fillWidth: true
                            tone: "warn"
                            text: vm.cloudWarning
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        TButton {
                            text: qsTr("Átírás indítása")
                            variant: "primary"
                            enabled: vm.canStart
                            iconName: vm.canStart ? "audio-lines" : "lock"
                            iconSize: 15
                            implicitHeight: 36
                            leftPadding: 16; rightPadding: 16
                            font.weight: Theme.weightSemiBold
                            onClicked: root.start()
                        }
                        TLabel {
                            Layout.fillWidth: true
                            text: vm.canStart ? qsTr("Szolgáltató: %1").arg(vm.providerLabel)
                                              : (vm.blocker.reason || "")
                            muted: true
                            font.pixelSize: Theme.fontSmall
                            wrapMode: Text.Wrap
                        }
                    }

                    // „Hamarosan” sor (csak ha a build tartalmazza a Tanara Cloud előzetesét).
                    RowLayout {
                        visible: vm.cloudTeaser
                        Layout.fillWidth: true
                        spacing: 6
                        TLabel {
                            text: qsTr("Nem akarsz kulcsokkal bajlódni? A Tanara Cloud hamarosan jön.")
                            muted: true
                            font.pixelSize: Theme.fontSmall
                        }
                        TButton {
                            text: qsTr("Érdekel")
                            variant: "ghost"; size: "small"
                            implicitHeight: 24
                            onClicked: root.openSettings("cloud")
                        }
                        Item { Layout.fillWidth: true }
                    }
                }
            }

            // ================= M04 — átírás folyamatban =================
            ColumnLayout {
                id: runningBox
                visible: vm.state === "running"
                width: Math.min(parent.width, 640)
                spacing: 16

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: runCol.implicitHeight + 36
                    radius: Theme.radiusPopup
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.border

                    ColumnLayout {
                        id: runCol
                        anchors { fill: parent; leftMargin: 20; rightMargin: 20; topMargin: 18; bottomMargin: 18 }
                        spacing: 14

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            TLabel {
                                Layout.fillWidth: true
                                text: vm.jobTitle
                                font.pixelSize: Theme.fontHeading
                                font.weight: Theme.weightSemiBold
                                elide: Text.ElideRight
                            }
                            // Becslés csak akkor, ha van korábbi mért futás ugyanezzel a szolgáltatóval.
                            TLabel {
                                visible: vm.etaText !== ""
                                text: vm.etaText
                                muted: true
                                font.pixelSize: Theme.fontSmall
                            }
                        }
                        Repeater {
                            model: vm.stages
                            JobStageRow {
                                required property var modelData
                                Layout.fillWidth: true
                                label: modelData.label
                                stageState: modelData.state
                                percent: modelData.percent
                                detail: modelData.detail
                                labelWidth: Math.min(220, runningBox.width * 0.4)
                                runningNote: modelData.id === "diarize" ? qsTr("az átírással egy menetben") : ""
                            }
                        }
                        TDivider { Layout.fillWidth: true }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12
                            TLabel {
                                Layout.fillWidth: true
                                text: vm.cancelling ? qsTr("Megszakítás folyamatban; a felvétel és a sávok érintetlenek maradnak.")
                                                    : qsTr("Közben nyugodtan dolgozz tovább; szólunk, ha kész.")
                                muted: true
                                font.pixelSize: Theme.fontSmall
                                cssLineHeight: 1.45
                                wrapMode: Text.Wrap
                            }
                            TButton {
                                visible: vm.cancellable
                                enabled: !vm.cancelling
                                text: qsTr("Megszakítás")
                                size: "small"
                                implicitHeight: 30
                                leftPadding: 12; rightPadding: 12
                                onClicked: if (root.shell) root.shell.cancelJob(root.meetingId, JobKinds.Transcribe)
                            }
                        }
                    }
                }
                TLabel {
                    visible: text !== ""
                    Layout.fillWidth: true
                    text: vm.footerLine
                    muted: true
                    font.pixelSize: Theme.fontSmall
                    elide: Text.ElideRight
                }
            }

            // ================= M05 — az átírás nem sikerült =================
            ColumnLayout {
                id: failed
                visible: vm.state === "failed"
                width: Math.min(parent.width, 640)
                spacing: 12

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: failCol.implicitHeight + 36
                    radius: Theme.radiusPopup
                    color: Theme.dangerSoft
                    border.width: 1
                    border.color: Theme.dangerLine

                    ColumnLayout {
                        id: failCol
                        anchors { fill: parent; leftMargin: 20; rightMargin: 20; topMargin: 18; bottomMargin: 18 }
                        spacing: 12

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            TIcon { name: "triangle-alert"; size: 18; color: Theme.dangerInk }
                            TLabel {
                                Layout.fillWidth: true
                                text: qsTr("Az átírás nem sikerült")
                                font.pixelSize: Theme.fontHeading
                                font.weight: Theme.weightSemiBold
                            }
                        }
                        TLabel {
                            Layout.fillWidth: true
                            text: vm.errorMessage + " " + qsTr("A felvétel és a sávok érintetlenek.")
                            cssLineHeight: 1.5
                            wrapMode: Text.Wrap
                        }
                        Rectangle {
                            visible: vm.errorDetail !== ""
                            Layout.fillWidth: true
                            implicitHeight: detailText.implicitHeight + 16
                            radius: 5
                            color: Theme.raised
                            border.width: 1
                            border.color: Theme.border
                            TextEdit {
                                id: detailText
                                anchors { fill: parent; leftMargin: 10; rightMargin: 10; topMargin: 8; bottomMargin: 8 }
                                text: vm.errorDetail
                                readOnly: true
                                selectByMouse: true
                                wrapMode: TextEdit.Wrap
                                color: Theme.textMuted
                                selectionColor: Theme.accent
                                selectedTextColor: Theme.textOnAccent
                                font.family: Theme.fontMono
                                font.pixelSize: Theme.fontCaption
                                Accessible.name: qsTr("Technikai részletek")
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: 2
                            spacing: 8
                            TButton {
                                text: qsTr("Újrapróbálás")
                                variant: "primary"
                                size: "small"
                                implicitHeight: 32
                                leftPadding: 12; rightPadding: 12
                                iconName: "rotate-ccw"
                                iconSize: 14
                                spacing: 7
                                onClicked: if (root.shell) root.shell.startTranscription(root.meetingId)
                            }
                            TButton {
                                visible: vm.fixActionLabel !== ""
                                text: vm.fixActionLabel
                                size: "small"
                                implicitHeight: 32
                                leftPadding: 12; rightPadding: 12
                                onClicked: root.openSettings(vm.fixActionPage)
                            }
                            Item { Layout.fillWidth: true }
                        }
                    }
                }
                TButton {
                    text: qsTr("Kontextus és előkészítés módosítása")
                    variant: "ghost"; size: "small"
                    muted: true
                    iconName: "chevron-left"
                    onClicked: vm.clearError()
                }
            }
        }
    }
}
