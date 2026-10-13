import QtQuick
import QtQuick.Layouts

// The Summary tab's shared status bars, stacked (only what currently applies):
//   - running job (summary / topic suggestion / final wrap-up) with cancel,
//   - remaining error (human message + technical line, retry, fix action; for a context error
//     with LM Studio "Load with larger context": reload + retry),
//   - missing setup (named; the button leads to the matching Settings page).
// showJob: the empty state draws its own run card, there the running bar can be switched off.
ColumnLayout {
    id: root

    required property var vm           // SummaryViewModel
    property string meetingId: ""
    property var shell: null
    property bool showJob: true
    property bool showBlocker: true
    signal retryRequested()

    readonly property bool jobVisible: showJob && vm.jobRunning
    readonly property bool errorVisible: vm.errorMessage !== "" && !vm.jobRunning
    readonly property bool blockerVisible: showBlocker && vm.blocker.title !== undefined

    visible: jobVisible || errorVisible || blockerVisible
    spacing: 10

    // For a missing language model Settings opens with the LLM card highlighted.
    function openSettings(page) {
        if (!root.shell) return
        if (page === "providers") root.shell.openSettings(page, "llm")
        else root.shell.openSettings(page)
    }

    // Running job: title, the running phase ("Making notes: part 3 / 6" -- real bar; "Merging" / a
    // single step -- indeterminate), cancel; below it, if it reuses partial notes of an earlier run,
    // that too.
    Rectangle {
        visible: root.jobVisible
        Layout.fillWidth: true
        implicitHeight: Math.max(44, jobCol.implicitHeight + 16)
        radius: Theme.radiusControl
        color: Theme.accentSoft
        border.width: 1
        border.color: Theme.accentLine
        ColumnLayout {
            id: jobCol
            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter
                      leftMargin: 14; rightMargin: 8 }
            spacing: 2
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                TSpinner { size: 16 }
                TLabel {
                    text: root.vm.jobTitle !== "" ? root.vm.jobTitle + "…" : qsTr("Folyamatban…")
                    font.weight: Theme.weightSemiBold
                }
                TLabel {
                    visible: root.vm.jobStage !== ""
                    Layout.maximumWidth: root.width * 0.35
                    text: root.vm.jobStageLabel
                    muted: true
                    font.pixelSize: Theme.fontSmall
                    elide: Text.ElideRight
                }
                TProgressBar {
                    Layout.fillWidth: true
                    indeterminate: root.vm.jobPercent < 0
                    value: root.vm.jobPercent < 0 ? 0 : root.vm.jobPercent / 100
                    trackColor: Theme.bg
                }
                TButton {
                    text: root.vm.jobCancelling ? qsTr("Megszakítás…") : qsTr("Megszakítás")
                    enabled: !root.vm.jobCancelling
                    variant: "ghost"; size: "small"
                    onClicked: if (root.shell) root.shell.cancelJob(root.meetingId, root.vm.jobKind)
                }
            }
            TLabel {
                visible: root.vm.jobReusedNote !== ""
                Layout.fillWidth: true
                Layout.leftMargin: 28
                Layout.bottomMargin: 4
                text: root.vm.jobReusedNote
                muted: true
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.Wrap
            }
        }
    }

    TBanner {
        visible: root.errorVisible
        Layout.fillWidth: true
        tone: "danger"
        iconName: "triangle-alert"
        title: qsTr("Az összefoglaló legutóbb nem készült el")
        text: root.vm.errorMessage + (root.vm.errorDetail !== "" ? "\n" + root.vm.errorDetail : "")
              + (root.vm.errorKeptParts
                 ? "\n" + qsTr("A már elkészült részek jegyzetei megmaradtak; a folytatás csak a hiányzó részeket és az összefésülést futtatja.")
                 : "")
        TButton {
            visible: root.vm.fixReloadContext > 0
            text: qsTr("Betöltés nagyobb kontextussal")
            size: "small"
            onClicked: {
                if (root.shell) root.shell.requestLlmContext(root.vm.fixReloadContext)
                root.retryRequested()
            }
        }
        TButton {
            text: root.vm.errorKeptParts ? qsTr("Folytatás") : qsTr("Újra")
            size: "small"
            iconName: "rotate-ccw"
            iconSize: 13
            onClicked: root.retryRequested()
        }
        TButton {
            visible: root.vm.fixActionLabel !== ""
            text: root.vm.fixActionLabel
            size: "small"
            onClicked: root.openSettings(root.vm.fixActionPage)
        }
        TButton {
            text: qsTr("Rendben")
            variant: "ghost"; size: "small"
            onClicked: root.vm.clearError()
        }
    }

    TBanner {
        visible: root.blockerVisible
        Layout.fillWidth: true
        tone: "warn"
        title: root.vm.blocker.title || ""
        text: root.vm.blocker.text || ""
        TButton {
            visible: (root.vm.blocker.actionLabel || "") !== ""
            text: root.vm.blocker.actionLabel || ""
            size: "small"
            implicitHeight: 30
            leftPadding: 12; rightPadding: 12
            font.weight: Theme.weightSemiBold
            trailingIconName: "arrow-right"
            iconSize: 14
            spacing: 6
            onClicked: root.openSettings(root.vm.blocker.actionPage || "")
        }
    }
}
