import QtQuick
import QtQuick.Templates as T

// The Settings window's left sidebar: group rows (SettingsNavItem) with their jump points
// always visible below them, indented, on a thin vertical guide line. The jump point in view is
// marked, only in the current group (the content's scroll position drives `section`). A warning
// dot sits next to a jump point that needs attention; on the group row while the group is not
// the current one. The build's version at the bottom.
//   SettingsSidebar { groups: [{ key, label, icon, children: [{ key, label }] }]; page: "processing"
//                     section: "stt"; warnings: ({ diar: "..." }); onPagePicked: ...; onSectionPicked: ... }
Rectangle {
    id: root

    property var groups: []
    property string page: ""
    property string section: ""
    // Jump point (or group without any) key -> why it needs attention (the dot's tooltip).
    property var warnings: ({})
    signal pagePicked(string key)
    signal sectionPicked(string page, string key)


    color: Theme.surface
    Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.border }

    Flickable {
        anchors.fill: parent
        anchors.rightMargin: 1
        anchors.bottomMargin: 36
        contentHeight: navCol.implicitHeight + 24
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        Column {
            id: navCol
            x: 10; y: 12
            width: parent.width - 20
            spacing: 2
            Repeater {
                model: root.groups
                Column {
                    id: grp
                    required property var modelData
                    readonly property bool currentGroup: root.page === modelData.key
                    width: navCol.width
                    spacing: 1
                    SettingsNavItem {
                        objectName: "nav-" + grp.modelData.key
                        width: parent.width
                        text: grp.modelData.label
                        iconName: grp.modelData.icon
                        current: grp.currentGroup && (grp.modelData.children.length === 0 || root.section === "")
                        // The bindings read root.warnings themselves (a helper function's reads
                        // are not tracked reliably in compiled bindings).
                        warn: {
                            const w = root.warnings
                            return grp.modelData.children.length === 0 ? w[grp.modelData.key] !== undefined
                                 : !grp.currentGroup && grp.modelData.children.some(c => w[c.key] !== undefined)
                        }
                        // Its own warning or its first warned jump point's.
                        warnText: {
                            const w = root.warnings
                            if (w[grp.modelData.key] !== undefined) return w[grp.modelData.key]
                            for (const c of grp.modelData.children)
                                if (w[c.key] !== undefined) return w[c.key]
                            return ""
                        }
                        onClicked: root.pagePicked(grp.modelData.key)
                    }
                    Repeater {
                        model: grp.modelData.children
                        T.AbstractButton {
                            id: child
                            required property var modelData
                            readonly property bool active: grp.currentGroup && root.section === modelData.key
                            readonly property string warnText: root.warnings[modelData.key] ?? ""
                            readonly property bool warned: warnText !== ""
                            objectName: "nav-" + grp.modelData.key + "-" + modelData.key
                            width: grp.width
                            implicitHeight: 30
                            text: modelData.label
                            hoverEnabled: true
                            activeFocusOnTab: true
                            Accessible.role: Accessible.PageTab
                            Accessible.name: child.warnText !== "" ? text + " — " + child.warnText : text
                            Keys.onReturnPressed: click()
                            Keys.onSpacePressed: click()
                            onClicked: root.sectionPicked(grp.modelData.key, modelData.key)
                            background: Rectangle {
                                radius: Theme.radiusControl
                                color: child.active ? Theme.accentSoft
                                     : child.hovered ? Theme.alpha(Theme.stateLayer, Theme.hoverOpacity) : "transparent"
                                Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                                TFocusRing { visible: child.visualFocus }
                            }
                            contentItem: Item {
                                // The guide line, accented at the jump point in view.
                                Rectangle {
                                    x: 17; width: 2; height: parent.height
                                    color: child.active ? Theme.accent : Theme.border
                                }
                                TLabel {
                                    x: 36
                                    width: parent.width - 36 - (dot.visible ? 22 : 10)
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: child.text
                                    font.pixelSize: Theme.fontSmall
                                    color: child.active ? Theme.text : Theme.textMuted
                                    font.weight: child.active ? Theme.weightSemiBold : Theme.weightRegular
                                    elide: Text.ElideRight
                                }
                                Rectangle {
                                    id: dot
                                    visible: child.warned
                                    width: 7; height: 7; radius: 3.5
                                    anchors.right: parent.right
                                    anchors.rightMargin: 10
                                    anchors.verticalCenter: parent.verticalCenter
                                    color: Theme.warn
                                }
                            }
                            TToolTip {
                                visible: child.warnText !== "" && child.hovered
                                text: child.warnText
                            }
                        }
                    }
                    Item { width: 1; height: grp.modelData.children.length > 0 ? 6 : 0 }
                }
            }
        }
    }

    // Version at the bottom of the navigation (a bug report shows which build runs).
    TLabel {
        objectName: "versionLabel"
        x: 20
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 14
        text: "Tanara " + App.version
        mono: true
        muted: true
        font.pixelSize: Theme.fontMicro
    }
}
