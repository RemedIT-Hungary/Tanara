import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// Pre-transcript view - shown instead of the tabs while a meeting has no transcript.
// Three states from the processing state (PreTranscriptViewModel.state):
//   "steps"   context -> preparation -> transcription (the missing part is named, the button leads there)
//   "running" section list with real progress, cancelable
//   "failed"  error card with retry and a fix action
// For screenshots: --qml-prop 'demoState="steps|ready|mixing|running|uploading|failed|note"'.
Item {
    id: root

    property string meetingId: ""      // the selected meeting; "" = none
    property var player: null          // PlayerController or null (unused here)
    property var shell: null           // ShellActions or null
    property string demoState: ""

    PreTranscriptViewModel {
        id: vm
        meetingId: root.meetingId
        demoState: root.demoState
        // Tag steps go to the shell toast with "Undo" (shared undo stack).
        onToast: (text, undoable) => { if (root.shell) root.shell.toast(text, undoable ? "tags" : "") }
    }

    // After Settings closes, re-read the gating (a key was added / removed).
    onVisibleChanged: if (visible) vm.refresh()
    Connections {
        target: Qt.application
        function onStateChanged() { if (Qt.application.state === Qt.ApplicationActive && root.visible) vm.refresh() }
    }

    // For a missing transcription provider, Settings opens with the STT card highlighted.
    function openSettings(page) {
        if (!root.shell) return
        if (page === "providers") root.shell.openSettings(page, "stt")
        else root.shell.openSettings(page)
    }
    function commitContext() { noteEditor.commit() }
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

            // ================= steps =================
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

                    // ---- Tags: here before the transcript, not in the header ----
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        TLabel {
                            text: qsTr("Címkék")
                            font.pixelSize: Theme.fontSmall
                            font.weight: Theme.weightSemiBold
                        }
                        TagField {
                            id: tagField
                            objectName: "preTranscriptTagField"
                            Layout.fillWidth: true
                            tags: vm.tags.tags
                            onAddRequested: (name, isNew) => vm.tags.add(name)
                            onRemoveRequested: (id) => vm.tags.remove(id)
                        }
                        // "Suggested: [+ chip] similar title: "..." - Detected call: ..."
                        Row {
                            visible: vm.tagSuggestions.length > 0
                            Layout.fillWidth: true
                            spacing: 6
                            clip: true

                            TLabel {
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("Javasolt:")
                                muted: true
                                font.pixelSize: Theme.fontCaption
                            }
                            Repeater {
                                model: vm.tagSuggestions
                                TagChip {
                                    required property var modelData
                                    anchors.verticalCenter: parent.verticalCenter
                                    kind: modelData.isNew ? "llmNew" : "suggested"
                                    text: modelData.name
                                    toolTipText: qsTr("Kattintás: hozzáadás · ×: nem illik ide")
                                    onClicked: vm.acceptTagSuggestion(modelData.index)
                                    onRemoveRequested: vm.rejectTagSuggestion(modelData.index)
                                }
                            }
                            TLabel {
                                anchors.verticalCenter: parent.verticalCenter
                                text: [vm.tagSuggestionReason,
                                       vm.note.detectedCallApp !== "" ? qsTr("Észlelt hívás: %1").arg(vm.note.detectedCallApp) : ""]
                                      .filter(t => t !== "").join(" · ")
                                muted: true
                                font.pixelSize: Theme.fontCaption
                            }
                        }
                    }

                    TLabel {
                        Layout.topMargin: 6
                        text: qsTr("Megjegyzés")
                        font.pixelSize: Theme.fontSmall
                        font.weight: Theme.weightSemiBold
                    }
                    MeetingNoteEditor {
                        id: noteEditor
                        Layout.fillWidth: true
                        // The detected call sits in the suggestion row if that is visible.
                        showDetectedCall: vm.tagSuggestions.length === 0
                        model: vm.note
                        accessibleName: qsTr("Miről szólt a megbeszélés?")
                        helperText: qsTr("Témák, nevek, szakszavak és az ismert félrehallások: az átíró ebből pontosabb átiratot készít, és az összefoglaló is megkapja. Később, az Összefoglaló fülön is módosítható. Elhagyható.")
                    }
                    // Whisper's context budget (only with the whisper-compat transcriber): above 70 % a
                    // neutral hint, over the budget a warning + "What does the model get?" preview.
                    TLabel {
                        objectName: "contextBudgetHint"
                        visible: vm.contextBudgetState === "hint"
                        Layout.fillWidth: true
                        text: qsTr("A kiválasztott átíró (Whisper) legfeljebb ~%1 tokent fogad a kontextusból (becslés: %2 token).")
                              .arg(vm.contextTokenBudget).arg(vm.contextTokenEstimate)
                        muted: true
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.Wrap
                    }
                    TBanner {
                        id: budgetBanner
                        objectName: "contextBudgetWarning"
                        property bool previewOpen: false
                        visible: vm.contextBudgetState === "over"
                        Layout.fillWidth: true
                        tone: "warn"
                        text: qsTr("A kiválasztott átíró (Whisper) legfeljebb ~224 tokent fogad; a kontextus eleje csonkolva lesz (becslés: %1 token).")
                              .arg(vm.contextTokenEstimate)
                        TButton {
                            objectName: "contextPreviewButton"
                            text: qsTr("Mit kap a modell?")
                            size: "small"
                            variant: "ghost"
                            onClicked: budgetBanner.previewOpen = !budgetBanner.previewOpen
                        }
                    }
                    TLabel {
                        objectName: "contextPreview"
                        visible: budgetBanner.visible && budgetBanner.previewOpen
                        Layout.fillWidth: true
                        textFormat: Text.StyledText
                        text: vm.contextPreview
                        muted: true
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.Wrap
                        Accessible.name: qsTr("A modellnek küldött kontextus; az áthúzott rész kimarad")
                    }
                }

                PreTranscriptStep {
                    Layout.fillWidth: true
                    number: 2
                    tone: "next"
                    title: qsTr("Előkészítés")
                    tag: qsTr("opcionális")
                    contentSpacing: 10

                    // --- Identify participants by voice ---
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

                    // --- Mixdown: transcription works from it, so it cannot be turned off; its
                    //     state shows here and it can be prepared in advance. ---
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

                    // Missing setting: named, the button leads there.
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

                    // Tanara Cloud: coming soon (while no own provider is set up).
                    CloudComingSoon {
                        visible: vm.cloudSoon
                        compact: true
                        Layout.fillWidth: true
                    }
                }
            }

            // ================= transcription in progress =================
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
                            // An estimate only if there is an earlier measured run with the same
                            // provider.
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

            // ================= transcription failed =================
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
