import QtQuick

// A „Megbeszélés” menü (design/handoff-v3, 5. döntés): a menüsor „Megbeszélés” menüje és a
// fejléc „…” gombja ugyanezt nyitja. Átnevezés · Ki volt ott? · Újraellenőrzés · Újra-átírás ·
// Exportálás · Megnyitás mappában · Törlés. A „Résztvevők azonosítása (hang alapján)” a régi
// azonosítás belépője marad, amíg a „Ki volt ott?” párbeszéd mindent ki nem vált.
TMenu {
    id: root

    property var shell: null                 // ShellActions vagy null
    property string meetingId: ""
    property bool hasMeeting: meetingId !== "" || !shell
    property bool hasTranscript: true
    property bool canIdentify: true

    signal renameRequested()
    signal participantsRequested()

    TMenuItem {
        objectName: "meetingRenameItem"
        text: qsTr("Átnevezés")
        iconName: "pencil"
        shortcutText: "F2"
        enabled: root.hasMeeting
        onTriggered: root.renameRequested()
    }
    TMenuItem {
        objectName: "meetingParticipantsItem"
        text: qsTr("Ki volt ott?")
        iconName: "users"
        enabled: root.hasMeeting
        onTriggered: root.participantsRequested()
    }
    TMenuItem {
        objectName: "identifyItem"
        text: qsTr("Résztvevők azonosítása (hang alapján)")
        iconName: "fingerprint"
        enabled: root.hasMeeting && root.hasTranscript && root.canIdentify
        onTriggered: if (root.shell) root.shell.identifyParticipants(root.meetingId)
    }
    TMenuItem {
        objectName: "recheckSpeakersItem"
        text: qsTr("Újraellenőrzés…")
        iconName: "refresh-cw"
        enabled: root.hasMeeting && root.hasTranscript
        onTriggered: if (root.shell) root.shell.recheckSpeakers(root.meetingId)
    }
    TMenuItem {
        text: qsTr("Újra-átírás…")
        iconName: "rotate-ccw"
        enabled: root.hasMeeting && root.hasTranscript
        onTriggered: if (root.shell) root.shell.retranscribe(root.meetingId)
    }
    TMenuItem {
        objectName: "exportArchiveItem"
        text: qsTr("Exportálás archívumba…")
        iconName: "arrow-down-to-line"
        enabled: root.hasMeeting
        onTriggered: if (root.shell) root.shell.exportArchive(root.meetingId)
    }
    TMenuItem {
        text: qsTr("Megnyitás mappában")
        iconName: "folder-open"
        enabled: root.hasMeeting
        onTriggered: if (root.shell) root.shell.revealInFolder(root.meetingId)
    }
    TMenuSeparator {}
    TMenuItem {
        objectName: "deleteMeetingItem"
        text: qsTr("Törlés…")
        iconName: "trash-2"
        danger: true
        enabled: root.hasMeeting
        onTriggered: if (root.shell) root.shell.requestDelete(root.meetingId)
    }
}
