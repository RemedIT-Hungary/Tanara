import QtQuick
import QtQuick.Layouts

// Az Összefoglaló fül közös állapot-sávjai, egymás alatt (csak ami épp érvényes):
//   - futó feladat (összefoglaló / téma-javaslat / záró összegzés) megszakítással,
//   - megmaradt hiba (emberi üzenet + technikai sor, újrapróbálás, javító művelet),
//   - hiányzó beállítás (megnevezve; a gomb a Beállítások megfelelő oldalára visz).
// showJob: az M06 üres állapot saját futás-kártyát rajzol, ott a futó sáv kikapcsolható.
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

    // A hiányzó nyelvi modellhez a Beállítások az LLM-kártyát kiemelve nyílik (B04).
    function openSettings(page) {
        if (!root.shell) return
        if (page === "providers") root.shell.openSettings(page, "llm")
        else root.shell.openSettings(page)
    }

    Rectangle {
        visible: root.jobVisible
        Layout.fillWidth: true
        implicitHeight: 44
        radius: Theme.radiusControl
        color: Theme.accentSoft
        border.width: 1
        border.color: Theme.accentLine
        RowLayout {
            anchors { fill: parent; leftMargin: 14; rightMargin: 8 }
            spacing: 12
            TSpinner { size: 16 }
            TLabel {
                text: root.vm.jobTitle !== "" ? root.vm.jobTitle + "…" : qsTr("Folyamatban…")
                font.weight: Theme.weightSemiBold
            }
            TProgressBar {
                Layout.fillWidth: true
                indeterminate: true
                trackColor: Theme.bg
            }
            TButton {
                text: root.vm.jobCancelling ? qsTr("Megszakítás…") : qsTr("Megszakítás")
                enabled: !root.vm.jobCancelling
                variant: "ghost"; size: "small"
                onClicked: if (root.shell) root.shell.cancelJob(root.meetingId, root.vm.jobKind)
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
        TButton {
            text: qsTr("Újra")
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
