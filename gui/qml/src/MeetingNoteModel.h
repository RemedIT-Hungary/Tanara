#pragma once
//
// Tanara QML — a megbeszélés-megjegyzés (Meeting::contextNote) szerkesztésének közös
// nézetmodellje. Két helyen él: az átirat előtti nézet 1. lépésében (PreTranscriptViewModel.note)
// és az Összefoglaló fülön (SummaryViewModel.note); a QML-oldali párja a MeetingNoteEditor.
//
//  - Késleltetett mentés, mindig a SAJÁT megbeszélésébe: a mező gépelés közbeni tartalmát
//    (draft) a megbeszélés azonosítójával együtt jegyezzük meg; a mentés (commitDraft) és a
//    kijelölés-váltás ezt írja ki oda, ahová írták — akkor is, ha közben másik lett a kijelölt.
//  - Sablon-javaslatok (suggestions): a hasonló című korábbi megbeszélések megjegyzései
//    (tanara::MeetingLibrary::noteSuggestions). Kattintásra a mezőbe kerül (applySuggestion);
//    magától soha semmi nem töltődik ki.
//  - Észlelt hívás (detectedCallApp): a figyelő által felismert app, csak tájékoztatás.
//
// Demóban (App.demo / nincs controller) a tulajdonos nézetmodell tölti a mintaadatot
// (setDemoContent); a mentés ilyenkor csak a memóriában történik.
//
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace tanara {
class AppController;
}

namespace tanara_qml {

class MeetingNoteModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("A PreTranscriptViewModel.note / SummaryViewModel.note adja.")

    Q_PROPERTY(QString meetingId READ meetingId NOTIFY meetingIdChanged)
    // A mentett megjegyzés (a mező gépelés közben a draft-ot hívja, ez csak mentéskor változik).
    Q_PROPERTY(QString note READ note WRITE setNote NOTIFY noteChanged)
    // „Microsoft Teams” — üres, ha nem a figyelő indította a felvételt.
    Q_PROPERTY(QString detectedCallApp READ detectedCallApp NOTIFY changed)
    // [{ meetingId, title, dateText, preview, note, tags }] — legfeljebb 3, legújabb elöl;
    // tags: a forrás-megbeszélés címkéinek nevei (a kártya mini chipjei).
    Q_PROPERTY(QVariantList suggestions READ suggestions NOTIFY suggestionsChanged)

public:
    explicit MeetingNoteModel(QObject* parent = nullptr);

    // A tulajdonos nézetmodell állítja (a teszt injektált controllere vagy az App-é).
    void setController(QObject* injected);
    QString meetingId() const { return m_meetingId; }
    // Váltás előtt a függő piszkozatot a RÉGI megbeszélésbe menti.
    void setMeetingId(const QString& id);

    QString note() const { return m_note; }
    void setNote(const QString& note);
    QString detectedCallApp() const { return m_detectedCallApp; }
    QVariantList suggestions() const { return m_suggestions; }
    bool hasPendingDraft() const { return m_hasDraft; }

    // A mező gépelés közbeni tartalma — a megbeszéléssel EGYÜTT, amelyhez írták.
    Q_INVOKABLE void draft(const QString& text);
    // A függő piszkozat mentése a saját megbeszélésébe (késleltetett mentés, fókuszvesztés).
    Q_INVOKABLE void commitDraft();
    // Egy javaslat (a suggestions[i].note szövege) bemásolása: mode "replace" (csere) |
    // "append" (a meglévő után, új sorba). Az új megjegyzés azonnal mentődik. A szöveget (nem
    // az indexet) kapja, mert a piszkozat mentése újraszámolhatja a listát. Üres → semmi.
    Q_INVOKABLE void applySuggestion(const QString& suggestion, const QString& mode);

    // Újraolvasás a tárolóból (a tulajdonos hívja, pl. meetingUpdated után). Demóban nem nyúl
    // a mintaadathoz.
    void reload();
    // Demó-tartalom (kitalált adat).
    void setDemoContent(const QString& note, const QString& detectedCallApp,
                        const QVariantList& suggestions);

    // A javaslat előnézete: az eleje egy sorban, legfeljebb ~90 karakter.
    static QString previewOf(const QString& note);
    // Kitalált sablon-javaslatok a demó-állapotokhoz (átirat előtti nézet, Összefoglaló fül).
    static QVariantList demoSuggestions();

signals:
    void meetingIdChanged();
    void noteChanged();
    void suggestionsChanged();
    void changed();

private:
    tanara::AppController* app() const;
    bool demo() const;
    void reloadSuggestions();

    QPointer<QObject> m_injected;
    QString m_meetingId;
    QString m_note;
    QString m_detectedCallApp;
    QVariantList m_suggestions;

    QString m_draft;
    QString m_draftMeetingId;
    bool m_hasDraft = false;
};

} // namespace tanara_qml
