import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

// Memó szakaszokkal (S2): balra a tartalomjegyzék (MemoToc, 236 px), jobbra a szakaszok
// időrendben. Szakasz-fejléc: „N. cím” 19/600, alatta az időtartomány-chip (kattintás =
// lejátszás onnan) és a szakasz beszélői; utána a pontok. A kiemelt szakasz (vm.activeSection)
// a görgetést követi, és a térkép „Szakaszok” sávjában accent.
Item {
    id: root

    required property var vm           // SummaryViewModel
    property string meetingId: ""
    property var shell: null
    property var player: null

    property bool tracking: true       // a görgetés követi-e a kiemelt szakaszt

    function scrollToSection(index) {
        const it = sections.itemAt(index)
        if (!it) return
        root.tracking = false
        const y = it.mapToItem(col, 0, 0).y + col.y - 18
        flick.contentY = Math.max(0, Math.min(y, flick.contentHeight - flick.height))
        root.vm.activeSection = index
        Qt.callLater(() => root.tracking = true)
    }
    function playFrom(index, ms) {
        root.vm.activeSection = index
        if (!root.player || ms < 0) return
        root.player.seek(ms)
        root.player.play()
    }
    function trackScroll() {
        if (!root.tracking) return
        let active = -1
        for (let i = 0; i < sections.count; ++i) {
            const it = sections.itemAt(i)
            if (!it) continue
            if (it.mapToItem(col, 0, 0).y + col.y <= flick.contentY + 40) active = i
            else break
        }
        root.vm.activeSection = Math.max(0, active)
    }
    function regenerate() {
        root.vm.note.commitDraft()
        if (root.shell) root.shell.startQuickSummary(root.meetingId)
    }

    // A képernyőkép / visszatérés: a kiemelt szakaszhoz görget.
    Component.onCompleted: if (vm.activeSection > 0) initialScroll.start()
    Timer {
        id: initialScroll
        interval: 60
        onTriggered: root.scrollToSection(root.vm.activeSection)
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        MemoToc {
            Layout.preferredWidth: 236
            Layout.fillHeight: true
            sections: root.vm.memo
            activeIndex: root.vm.activeSection
            onPicked: (index) => root.scrollToSection(index)
        }

        Flickable {
            id: flick
            objectName: "summaryMemoFlick"
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: col.implicitHeight + 18 + 28
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            T.ScrollBar.vertical: TScrollBar {}
            onContentYChanged: root.trackScroll()

            ColumnLayout {
                id: col
                x: 28; y: 18
                width: flick.width - 56
                spacing: 14

                SummaryStatusBanners {
                    Layout.fillWidth: true
                    vm: root.vm
                    meetingId: root.meetingId
                    shell: root.shell
                    showBlocker: false
                    onRetryRequested: root.regenerate()
                }

                TBanner {
                    visible: root.vm.stale
                    Layout.fillWidth: true
                    Layout.maximumWidth: 780
                    tone: "warn"
                    text: root.vm.staleCount > 0
                        ? qsTr("Az összefoglaló óta %n beszélőt javítottál, ezért a memó beszélői elavultak lehetnek.", "", root.vm.staleCount)
                        : qsTr("Az összefoglaló óta változtak a beszélők, ezért a memó beszélői elavultak lehetnek.")
                    TButton {
                        text: qsTr("Frissítés")
                        size: "small"
                        implicitHeight: 30
                        leftPadding: 12; rightPadding: 12
                        font.weight: Theme.weightSemiBold
                        enabled: root.vm.canRun && !root.vm.jobRunning
                        onClicked: root.regenerate()
                    }
                    TButton {
                        text: qsTr("Rendben így")
                        variant: "ghost"; size: "small"
                        muted: true
                        implicitHeight: 30
                        onClicked: root.vm.dismissStale()
                    }
                }

                Repeater {
                    id: sections
                    model: root.vm.memo
                    ColumnLayout {
                        id: section
                        required property var modelData
                        required property int index
                        objectName: "memoSection"
                        Layout.fillWidth: true
                        Layout.maximumWidth: 780
                        spacing: 5

                        TDivider { visible: section.index > 0; Layout.fillWidth: true; Layout.topMargin: 6; Layout.bottomMargin: 14 }
                        TLabel {
                            Layout.fillWidth: true
                            text: (section.index + 1) + ". " + (section.modelData.title !== "" ? section.modelData.title : qsTr("Egyéb"))
                            font.pixelSize: 19
                            font.weight: Theme.weightSemiBold
                            wrapMode: Text.Wrap
                        }
                        RowLayout {
                            visible: section.modelData.sourceRange !== "" || section.modelData.speakersText !== ""
                            Layout.fillWidth: true
                            spacing: 8
                            TimeChip {
                                objectName: "sectionRangeChip"
                                visible: section.modelData.sourceRange !== ""
                                large: true
                                iconName: "play"
                                text: section.modelData.sourceRange
                                toolTipText: qsTr("Lejátszás innen: %1").arg(section.modelData.sourceRange.split("–")[0])
                                onClicked: root.playFrom(section.index, section.modelData.startMs)
                            }
                            TLabel {
                                Layout.fillWidth: true
                                text: section.modelData.speakersText
                                muted: true
                                font.pixelSize: Theme.fontSmall
                                elide: Text.ElideRight
                            }
                        }
                        Repeater {
                            model: section.modelData.points
                            RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.topMargin: 4
                                spacing: 10
                                Rectangle {
                                    Layout.alignment: Qt.AlignTop
                                    Layout.topMargin: 10
                                    implicitWidth: 5; implicitHeight: 5; radius: 2.5
                                    color: Theme.textMuted
                                }
                                TLabel {
                                    Layout.fillWidth: true
                                    text: parent.modelData
                                    font.pixelSize: 15
                                    cssLineHeight: 1.65
                                    wrapMode: Text.Wrap
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
