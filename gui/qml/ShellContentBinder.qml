import QtQuick

// A szerződés (CONTRACT.md) három bemenetének átadása egy tartalom-komponensnek:
// meetingId / player / shell. Kötésekkel megy (nem közvetlen property-értékadással), hogy a
// Main.qml akkor is betöltődjön, amíg egy komponens még helykitöltő, és nem deklarálja a
// property-ket — a kész komponenseknél pontosan úgy viselkedik, mint az értékadás.
//   TranscriptTab { id: transcriptTab }
//   ShellContentBinder { target: transcriptTab; meetingId: …; player: …; shell: … }
QtObject {
    id: root

    property Item target: null
    property string meetingId: ""
    property var player: null
    property var shell: null

    property list<QtObject> bindings: [
        Binding {
            target: root.target
            property: "meetingId"
            value: root.meetingId
            when: root.target !== null && root.target.meetingId !== undefined
        },
        Binding {
            target: root.target
            property: "player"
            value: root.player
            when: root.target !== null && root.target.player !== undefined
        },
        Binding {
            target: root.target
            property: "shell"
            value: root.shell
            when: root.target !== null && root.target.shell !== undefined
        }
    ]
}
