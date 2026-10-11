#pragma once
//
// MeetingUndoService — EGY megbeszélés közös visszavonási verme (v3 4. döntés: „One shared undo
// stack for tags and speakers”). UI-független QObject; példányt az AppController::meetingUndo ad.
//
// A meglévő két verem érintetlen marad: a SpeakerEditoré (átirat-szerkesztő) és a TagServiceé
// (címkék, globális). Ez a réteg csak azt tartja nyilván, hogy IDŐRENDBEN melyik forrás lépése
// a legfrissebb, és az undo() azt vonja vissza a saját forrásán át. Harmadik forrásként a
// saját lépéseit is vezeti: a résztvevő-műveleteket (jóváhagyás, kihagyás, „Csak én beszéltem”,
// sáv be- / kiemelése, résztvevő felvétele / törlése, leírás), amelyeket az AppController
// rögzít egy visszaállító függvénnyel.
//
// Szabályok:
//  - A forrás-vermek lépéseit sorszám azonosítja (SpeakerEditor::undoSerials,
//    TagService::undoSerials). Új sorszám a forrás tetején → új bejegyzés a közös verem tetején
//    (a szerkesztő redo-ja is ilyen: a visszahozott lépés a legfrissebb). Eltűnt sorszám (a
//    forrásban közvetlenül visszavonták, a verem újratöltődött / túlcsordult) → a bejegyzés is
//    kiesik.
//  - A TagService globális: csak a meetinghez tartozó lépései kerülnek ide (a meeting címkéit
//    érintő, a meetingre szóló elutasítás, ill. a TagService::beginGroup(label, meetingId)
//    csoport). Ha egy ide tartozó címke-lépés fölé a TagService-ben más (pl. a Címkék ablak)
//    lépése került, az nem vonható vissza innen, amíg az ott van: a bejegyzés átugródik.
//  - Egy résztvevő-lépés „elnyelheti” a művelet közben keletkezett szerkesztő-lépést (pl. a
//    jóváhagyás kötései): ilyenkor az egy bejegyzés, és a visszavonása mindkettőt visszacsinálja
//    (a szerkesztőét csak akkor, ha még az van a tetején).
//  - A csatolás előtt már meglévő forrás-lépések nem kerülnek ide.
//
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVector>

#include <functional>
#include <memory>

namespace tanara {

class SpeakerEditor;
class TagService;

class MeetingUndoService : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(QString undoText READ undoText NOTIFY changed)
public:
    enum class Source { Speakers, Tags, Participants };
    Q_ENUM(Source)

    explicit MeetingUndoService(const QString& meetingId, QObject* parent = nullptr);
    ~MeetingUndoService() override;

    QString meetingId() const;

    // A források csatolása (nullptr: leválasztás — a forrás bejegyzései kiesnek). A szerkesztő
    // megszűnésekor magától leválik.
    void attachSpeakerEditor(SpeakerEditor* editor);
    void attachTagService(TagService* tags);
    SpeakerEditor* speakerEditor() const;
    TagService* tagService() const;

    // Saját (résztvevő-) lépés. undo: a visszaállítás (a fő szálon, a közös undo() hívja).
    // absorbEditorSerial: a művelet közben keletkezett szerkesztő-lépés sorszáma (0: nincs) —
    // a bejegyzése beleolvad ebbe a lépésbe.
    void pushStep(const QString& text, std::function<void()> undo, quint64 absorbEditorSerial = 0);

    // A legfrissebb visszavonható bejegyzés (a blokkolt címke-lépések átugorva).
    bool canUndo() const;
    QString undoText() const;
    Source undoSource() const;   // érvénytelen, ha !canUndo()
    // Az összes bejegyzés (a blokkoltakkal együtt) — diagnosztika / tesztek.
    int count() const;
    // A bejegyzések szövegei időrendben (régebbi elöl) — diagnosztika / tesztek.
    QStringList texts() const;

public slots:
    // A legfrissebb visszavonása a saját forrásán át. false: nincs mit.
    bool undo();

signals:
    // canUndo / undoText / a bejegyzések változtak.
    void changed();
    // Egy lépés visszavonva (a „Visszavonva: …” visszajelzéshez).
    void undone(QString text, tanara::MeetingUndoService::Source source);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace tanara
