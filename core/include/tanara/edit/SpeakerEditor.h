#pragma once
//
// SpeakerEditor — EGY megnyitott meeting beszélő-szerkesztő munkamenete (K4 átirat-szerkesztő
// backendje). UI-független QObject: feloldott olvasó-modell (megszólalások + beszélők),
// műveletek (mind PONTOSAN egy visszavonási lépés, azonnal perzisztálva), undo/redo,
// hang-alapú bizonytalanság + „hasonló sorok" javaslat, kézi hanglenyomat, és az
// összefoglaló elavult-jelzője.
//
// Elvek (a termékgazdával egyeztetve):
//  - Az átsorolás egész megszólalásra szól; soron belüli vágás nincs.
//  - A nyers diarizáció érintetlen; a javítás overlay (transcript.speakers.json).
//  - A kézi átsorolás NEM tanít hanglenyomatot — az csak kifejezett műveletre készül.
//  - A beszélő-változás az összefoglalót elavultnak jelöli; magától sosem generálódik újra.
//
// Példányt jellemzően az AppController::speakerEditor(meetingId) ad; tesztben közvetlenül,
// explicit store-okkal is létrehozható.
//
#include "tanara/edit/SpeakerEditTypes.h"
#include "tanara/edit/UtteranceEmbeddings.h"

#include <QObject>
#include <QtNumeric>
#include <QStringList>
#include <QVector>
#include <memory>

namespace tanara {

class MeetingStore;
class PeopleStore;
class VoiceprintStore;

class SpeakerEditor : public QObject {
    Q_OBJECT
public:
    // people / voiceprints lehet nullptr (ekkor a személy-lista / lenyomat-műveletek kimaradnak).
    SpeakerEditor(MeetingStore* store, PeopleStore* people, VoiceprintStore* voiceprints,
                  const QString& meetingId, QObject* parent = nullptr);
    ~SpeakerEditor() override;

    QString meetingId() const;
    bool hasTranscript() const;     // van-e transcript.segments.json (különben minden üres)

    // A felhasználó saját neve (EditorSpeaker::isSelf jelöléséhez).
    void setUserSpeakerName(const QString& name);
    // A megszólalás-embedder gyára. Nélküle nincs bizonytalanság / javaslat / kézi lenyomat —
    // minden más működik (kíméletes leépülés modell vagy hang hiányában).
    void setEmbedderFactory(UtteranceEmbedderFactory factory);

    // ---- olvasó-modell ----------------------------------------------------
    int utteranceCount() const;
    QVector<EditorUtterance> utterances() const;            // időrendben
    EditorUtterance utteranceAt(int index) const;
    EditorUtterance utterance(const QString& id) const;     // ismeretlen id → index == -1
    int indexOf(const QString& id) const;

    // A meeting beszélői colorIndex szerint (első megjelenés sorrendje; a kézzel felvettek
    // a végén). Az üres, eltávolított vagy összevont nyers címkék nem szerepelnek.
    QVector<EditorSpeaker> speakers() const;
    EditorSpeaker speaker(const QString& key) const;
    // Egy személy becenevei (a személy-tárból) — a választók becenévre is találnak.
    QStringList personAliases(const QString& personName) const;        // ismeretlen kulcs → üres key

    int uncertainCount() const;
    QStringList uncertainUtteranceIds() const;

    // ---- műveletek (mindegyik egy undo-lépés; false / üres = nem történt semmi) ----
    // Sorok áthelyezése a meeting egy meglévő beszélőjéhez.
    bool moveUtterances(const QStringList& utteranceIds, const QString& speakerKey);
    // …egy személyhez név szerint: ha már beszélője a meetingnek, hozzá; különben új
    // résztvevő jön létre (ismert VAGY vadonatúj személy — utóbbi a névlistába is bekerül).
    // Vissza: a cél-beszélő kulcsa.
    QString moveUtterancesToPerson(const QStringList& utteranceIds, const QString& personName);
    // …egy új NÉVTELEN résztvevőhöz („Új beszélő N"). Vissza: az új beszélő kulcsa.
    QString moveUtterancesToNewParticipant(const QStringList& utteranceIds);

    // A teljes beszélő másik személyhez kerül (minden sora). Ha a személy már beszélője a
    // meetingnek, a kettő összevonódik. fixVoiceprints: „téves felismerés" — a meeting
    // mintái kikerülnek a KORÁBBI személy hanglenyomatából és a választotthoz adódnak
    // (az undo ezt is visszacsinálja).
    bool reassignSpeaker(const QString& speakerKey, const QString& personName,
                         bool fixVoiceprints = false);
    // A beszélő visszaállítása névtelenre. fixVoiceprints: a meeting mintái kikerülnek a
    // korábbi személy lenyomatából.
    bool revertSpeakerToAnonymous(const QString& speakerKey, bool fixVoiceprints = false);
    // fromKey minden sora intoKey-hez kerül; fromKey megszűnik.
    bool mergeSpeakers(const QString& fromKey, const QString& intoKey);

    // Résztvevő felvétele (még sor nélkül). Üres név → névtelen. Ha a személy már beszélő,
    // annak a kulcsát adja vissza (lépés nélkül).
    QString addParticipant(const QString& personName = QString());
    // Üres beszélő (0 sor) eltávolítása. Nem üresnél false.
    bool removeParticipant(const QString& speakerKey);

    // „Jó így": a sorok megerősítése (többé nem bizonytalanok). asNoisy: „Jó így, de ne
    // használd mintának" — a sor egyben „egymásra beszéltek" jelzést is kap (nem hangminta).
    bool confirmUtterances(const QStringList& utteranceIds, bool asNoisy = false);
    // Az „egymásra beszéltek" jelzés kézi beállítása (true: ne legyen hangminta; false:
    // mintának használható, akkor is, ha az átfedés-szabály zajosnak tartaná).
    bool setUtterancesNoisy(const QStringList& utteranceIds, bool noisy);

    // ---- újraellenőrzés a megerősített sorok alapján ----------------------
    // A megerősített / javított sorokból épített „megbízható" hang-centroidokhoz méri a többi
    // sort (SpeakerAnalysis: computeUncertainRechecked). A kétségesnek talált sorok
    // bizonytalanok maradnak (az overlay-ben perzisztálva), amíg javítás vagy „Jó így" nem
    // jön; egy újabb újraellenőrzés lecseréli a halmazt. Egy undo-lépés (ha változott valami).
    // Az elnevezett beszélőknél a személy tárolt hanglenyomatai is beszállnak a referenciába
    // (SpeakerAnalysis: SpeakerPrior): az ebben a megbeszélésben készültek helyi bizonyítékként,
    // a korábbiak kisebb, vágott súllyal (a megerősített sorok mindig dominálnak).
    // Egy beszélő referenciájának összetétele (a visszajelzés szövegéhez).
    struct SpeakerReference {
        QString speakerKey;
        QString name;               // megjelenített név
        int  lines = 0;             // megerősített / javított sorok (fallbacknél: az összes sora)
        int  localPrints = 0;       // ebben a megbeszélésben készült lenyomatok
        int  priorPrints = 0;       // korábbi (más megbeszélésből való) lenyomatok
        bool priorOnly = false;     // nincs helyi mag: csak a korábbi lenyomatok
        bool fallback = false;      // se mag, se lenyomat: a beszélő összes sora
        bool priorCapped = false;   // a korábbi lenyomatok súlyát a plafon vágta
        bool usesPrints() const { return localPrints + priorPrints > 0; }
    };
    // „Referencia: Dompa 3 sor + 1 itteni lenyomat + 2 korábbi lenyomat, Gábor 5 sor" — csak a
    // magos / lenyomatos beszélők; üres, ha egyik referencia sem használt lenyomatot (akkor
    // nincs újdonság a megszokotthoz képest).
    static QString referenceSummary(const QVector<SpeakerReference>& refs);
    struct RecheckResult {
        int flagged = 0;                    // ennyi sort jelölt kétségesnek
        int speakersWithConfirmedCore = 0;  // ennyi beszélőnél épült centroid a zárolt soraiból
        int confirmedLines = 0;             // ennyi zárolt sor alkotta ezeket a magokat
        bool ran = false;                   // lefutott-e (canRecheck volt-e)
        QVector<SpeakerReference> references;   // beszélőnként a referencia (a magos / lenyomatosak)
        QString referenceSummary() const { return SpeakerEditor::referenceSummary(references); }
    };
    RecheckResult recheckFromConfirmed();
    // Futtatható-e: van embedding, nem fut a hang-elemzés, és legalább egy beszélőnek van
    // legalább kMinSpeakerLines megerősített / javított (embeddelt) sora.
    bool canRecheck() const;
    // Ha nem futtatható: miért (magyar mondat a felhasználónak); különben üres.
    QString recheckBlocker() const;

    // ---- páronkénti átnézés („Átnézés A és B között") ---------------------
    // Csak A és B sorai, csak kettejük hangja alapján (SpeakerAnalysis: computePairRecheck):
    // a referencia a megerősített / javított sorokból (+ a tárolt lenyomatokból, lásd fent)
    // épül; ha kevés a sor és lenyomat sincs, a beszélő összes tiszta sorából — ezt a
    // fallbackA/B jelzi. A kétesnek talált sorok bizonytalanok lesznek, a
    // javaslat (recheckHint) a másik beszélő; ugyanaz a perzisztencia, mint az
    // újraellenőrzésé (javítás / „Jó így" törli). Az A-n és B-n lévő sorok korábbi
    // újraellenőrzés-jelzéseit ez lecseréli; más beszélőkéit nem érinti. Egy undo-lépés (ha
    // változott valami).
    struct PairRecheckResult {
        int    flagged = 0;
        int    refLinesA = 0;
        int    refLinesB = 0;
        bool   fallbackA = false;
        bool   fallbackB = false;
        double centroidSimilarity = qQNaN();    // a két referencia hangjának cosine-ja
        SpeakerReference refA;                  // a két referencia összetétele
        SpeakerReference refB;
        bool   ran = false;
        QString blocker;                        // ha nem futott: miért (magyar mondat)
        QString referenceSummary() const { return SpeakerEditor::referenceSummary({refA, refB}); }
    };
    PairRecheckResult recheckPair(const QString& speakerKeyA, const QString& speakerKeyB);
    // Ha most nem futtatható: miért; különben üres.
    QString pairRecheckBlocker(const QString& speakerKeyA, const QString& speakerKeyB) const;

    // Az ajánlat (lásd PairRecheckOffer). A suggestionChanged jellel együtt változik, és a
    // következő szerkesztés / visszavonás eldobja.
    bool hasPairOffer() const;
    PairRecheckOffer pairOffer() const;
    // „Most nem": ezt a párt ebben a munkamenetben nem ajánlja fel újra.
    void declinePairOffer();
    void dismissPairOffer();

    // ---- javaslat („Még N sor hasonlít erre a hangra") --------------------
    bool hasSuggestion() const;
    SpeakerSuggestion suggestion() const;
    bool acceptSuggestion();        // tömeges áthelyezés, egy undo-lépés
    void dismissSuggestion();

    // ---- undo / redo (a megnyitott munkamenetre, memóriában) --------------
    bool canUndo() const;
    bool canRedo() const;
    QString undoText() const;       // a legfelső lépés rövid leírása
    QString redoText() const;

    // ---- megszólalás-embeddingek (háttérszál) -----------------------------
    bool embeddingsSupported() const;   // van gyár (a modell/hang megléte futáskor derül ki)
    bool embeddingsComplete() const;    // minden elég hosszú sorra megvan (vagy megpróbáltuk)
    bool isEmbeddingRunning() const;
    QString embeddingError() const;     // az utolsó futás hibája (üres = nincs)

    // ---- kézi hanglenyomat ------------------------------------------------
    VoiceprintMaterial voiceprintMaterial(const QString& speakerKey) const;
    // Lenyomat a beszélő ITTENI soraiból (hosszabb sorok, ~15–20 mp). Kevés anyagnál nem
    // készít gyenge lenyomatot: ok=false + missingMs. Nem undo-lépés (a printId-vel törölhető).
    VoiceprintResult createVoiceprint(const QString& speakerKey);
    // Lenyomat KIFEJEZETTEN megadott sorokból (a sor felugrójának „minta ebből a sorból”
    // gombja): a sor a beszélőé, nem bizonytalan, nem „egymásra beszéltek”, legalább 3 mp.
    // Egyetlen hosszú, tiszta sor is elég (a 15 mp-es minimum itt nem él). Nem undo-lépés.
    VoiceprintResult createVoiceprintFromLines(const QString& speakerKey, const QStringList& utteranceIds);
    // A kézi készítés visszavonása: PONTOSAN ez a lenyomat törlődik (a createVoiceprint
    // printId-je). false, ha már nincs ilyen. Ez sem undo-lépés.
    bool removeVoiceprint(const QString& printId);

    // ---- összefoglaló-elavultság / újra-átírás ----------------------------
    SummaryStaleInfo summaryStale() const;
    RetranscribeImpact retranscribeImpact() const;

    // A javítások (overlay, meeting.json) azonnal lemezre kerülnek; a transcript.md
    // újragenerálása viszont rövid késleltetéssel, kötegelve fut (a meeting hosszával nő).
    // Ez most azonnal kiírja, ha van függő frissítés (kilépés, export, teszt). A destruktor
    // is meghívja.
    void flushPendingWrites();

public slots:
    void undo();
    void redo();

    // Embeddingek számítása a háttérben (a cache-ből hiányzókra). embeddingProgress /
    // embeddingFinished jelek; a végén a bizonytalan-jelzők frissülnek.
    void startEmbedding();
    void cancelEmbedding();

    // „Rendben így": az elavult-jelző elengedése újragenerálás nélkül.
    void dismissSummaryStale();
    // Az összefoglaló (újra)elkészült → az elavult-jelző törlődik. (Az AppController hívja.)
    void notifySummaryRegenerated();

    // Külső változás (régi UI átnevezés, auto-azonosítás, személy-átnevezés): a speakerMap
    // és az overlay újraolvasása a lemezről. Az undo-verem megmarad, ha az overlay nem változott.
    void refreshFromDisk();
    // Új átirat készült: teljes újratöltés, az undo-verem és a javaslat eldobva (reloaded jel).
    void reloadTranscript();

signals:
    // A felsorolt megszólalások adata változott (beszélő / jelzők / bizonytalanság) —
    // a lista-modell ezekre ad dataChanged-et.
    void utterancesChanged(QStringList utteranceIds);
    // A beszélő-lista vagy annak adatai (név, számlálók, lenyomat-jelző) változtak.
    void speakersChanged();
    void uncertainCountChanged(int count);
    void undoStateChanged();
    // Javaslat érkezett vagy megszűnt (hasSuggestion() / suggestion()).
    void suggestionChanged();
    void embeddingRunningChanged(bool running);
    void embeddingProgress(int done, int total);
    void embeddingFinished(bool complete);
    void summaryStaleChanged(bool stale, int correctedSpeakers);
    // Minden megváltozott (új átirat / külső overlay-változás) → modell-reset.
    void reloaded();
    // Lefutott egy újraellenőrzés (a nézet ilyenkor a „Bizonytalan" szűrőre válthat).
    void recheckFinished(int flagged, int speakersWithConfirmedCore, int confirmedLines);

    // Mellékhatások a többi komponens felé (az AppController a saját jeleire fordítja).
    void speakerMapChanged(QString meetingId);
    void peopleChanged();
    void voiceprintsChanged();

private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace tanara
