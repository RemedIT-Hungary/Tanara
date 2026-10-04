#pragma once
//
// TranscriptEditorViewModel — az „Átirat" fül (átirat-szerkesztő, 3a) nézetmodellje: EGY
// megnyitott meeting szerkesztő-munkamenete a QML felé. A core tanara::SpeakerEditor-ára
// épül (az végzi a műveleteket, az undo-t, a hang-elemzést); ez az osztály a megjelenítés
// állapotát adja hozzá: lista-modell, sávok (összecsukási szabály), áttekintő, kijelölés,
// „Bizonytalan" szűrő, keresés, lejátszás-követés, javaslat, hang-elemzés állapota.
//
// Honnan jön a munkamenet:
//  - van AppController (éles):  meetingId → AppController::speakerEditor(id)
//  - nincs (App.demo, --qml-shot, --demo):  beépített KITALÁLT meeting (TranscriptDemoSession)
//  - teszt:  setEditor() + setPeopleProvider() közvetlenül
//
#include "TranscriptListModel.h"

#include "tanara/edit/SpeakerEditTypes.h"
#include "tanara/edit/SpeakerEditor.h"

#include <QColor>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QQmlParserStatus>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <memory>

namespace tanara {
class AppController;
class SpeakerEditor;
}

namespace tanara_qml {

class TranscriptDemoSession;

class TranscriptEditorViewModel : public QObject, public QQmlParserStatus {
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    QML_ELEMENT

    Q_PROPERTY(QString meetingId READ meetingId WRITE setMeetingId NOTIFY meetingIdChanged)
    Q_PROPERTY(QString demoVariant READ demoVariant WRITE setDemoVariant NOTIFY demoVariantChanged)
    // Igaz: a munkamenet a beépített kitalált meeting (nincs AppController).
    Q_PROPERTY(bool demo READ demo NOTIFY sessionChanged)
    Q_PROPERTY(bool hasTranscript READ hasTranscript NOTIFY sessionChanged)
    // A meetingnek VAN átirata, de régi formátumú (nincs megszólalás-lista), ezért itt nem
    // szerkeszthető — újra-átírás után jelenik meg.
    Q_PROPERTY(bool legacyTranscript READ legacyTranscript NOTIFY sessionChanged)
    // Régi formátumú átiratnál a transcript.md szövege (csak olvasásra; üres, ha nincs fájl).
    Q_PROPERTY(QString legacyText READ legacyText NOTIFY sessionChanged)
    Q_PROPERTY(tanara_qml::TranscriptListModel* rows READ rows CONSTANT)

    Q_PROPERTY(int utteranceCount READ utteranceCount NOTIFY sessionChanged)
    Q_PROPERTY(int speakerCount READ speakerCount NOTIFY speakersChanged)
    // A sín látható oszlopai: [{ key, name, personName, colorIndex, hasVoiceprint, anonymous,
    // added, isSelf, utteranceCount, pct, voiceprint }] — saját magam elöl, utána első megjelenés
    // szerint. `voiceprint`: "has" | "none" (elnevezett, de nincs lenyomata) | "anonymous".
    Q_PROPERTY(QVariantList lanes READ lanes NOTIFY speakersChanged)
    // Minden beszélő (az összecsukottak is), ugyanilyen elemekkel + `lane` (-1 = összecsukva).
    Q_PROPERTY(QVariantList speakers READ speakers NOTIFY speakersChanged)
    Q_PROPERTY(int collapsedCount READ collapsedCount NOTIFY speakersChanged)
    Q_PROPERTY(bool lanesExpanded READ lanesExpanded WRITE setLanesExpanded NOTIFY speakersChanged)
    // Áttekintő: [{ key, name, colorIndex (-1 = „Egyéb"), pct, voiceprint ("has" | "none" |
    // "anonymous"; „Egyéb"-nél üres), segments: [x, w, …], marks: [x, w, …] }]
    Q_PROPERTY(QVariantList overview READ overview NOTIFY overviewChanged)
    Q_PROPERTY(int durationMs READ durationMs NOTIFY overviewChanged)
    // A lejátszó hossza (ha hosszabb az utolsó megszólalásnál, az áttekintő ehhez igazodik).
    Q_PROPERTY(int timelineMs READ timelineMs WRITE setTimelineMs NOTIFY overviewChanged)

    Q_PROPERTY(bool railVisible READ railVisible WRITE setRailVisible NOTIFY railVisibleChanged)
    Q_PROPERTY(bool uncertainOnly READ uncertainOnly WRITE setUncertainOnly NOTIFY uncertainOnlyChanged)
    Q_PROPERTY(int uncertainCount READ uncertainCount NOTIFY uncertainCountChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectionChanged)
    // Az egyetlen kijelölt sor (vagy a kijelölés „horgonya") a listában; -1 = nincs.
    Q_PROPERTY(int currentRow READ currentRow NOTIFY selectionChanged)

    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoStateChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoStateChanged)
    Q_PROPERTY(QString undoText READ undoText NOTIFY undoStateChanged)
    Q_PROPERTY(QString redoText READ redoText NOTIFY undoStateChanged)

    Q_PROPERTY(bool suggestionActive READ suggestionActive NOTIFY suggestionChanged)
    Q_PROPERTY(int suggestionCount READ suggestionCount NOTIFY suggestionChanged)
    Q_PROPERTY(QString suggestionTargetName READ suggestionTargetName NOTIFY suggestionChanged)
    // „Megmutatom": a javasolt sorok kiemelve a sínen és az áttekintőn.
    Q_PROPERTY(bool suggestionShown READ suggestionShown WRITE setSuggestionShown NOTIFY suggestionChanged)

    // A legutóbbi átsorolás értesítése (az alsó sáv): mi történt, és mi folytatható belőle.
    // Minden új átsorolás lecseréli; visszavonás / újra / más szerkesztés megszünteti.
    Q_PROPERTY(bool changeActive READ changeActive NOTIFY changeChanged)
    Q_PROPERTY(int changeSerial READ changeSerial NOTIFY changeChanged)     // sávonként nő (időzítő)
    Q_PROPERTY(QString changeText READ changeText NOTIFY changeChanged)
    // Az utoljára áthelyezett sor a listában (maradjon látható); -1 = nincs ilyen.
    Q_PROPERTY(int changeRow READ changeRow NOTIFY changeChanged)
    // „<Forrás> mind a N sora": a forrás-beszélő megmaradt sorai (0 = nincs ilyen folytatás).
    Q_PROPERTY(int changeRestCount READ changeRestCount NOTIFY changeChanged)
    Q_PROPERTY(QString changeRestText READ changeRestText NOTIFY changeChanged)
    Q_PROPERTY(QString changeSourceKey READ changeSourceKey NOTIFY changeChanged)
    Q_PROPERTY(QString changeTargetKey READ changeTargetKey NOTIFY changeChanged)
    // Hanglenyomat-ajánlat a sávon: egy TELJES beszélő most kapott nevet, a személynek még
    // nincs lenyomata, és itt van hozzá elég anyag. Soronkénti áthelyezés után sosem igaz.
    Q_PROPERTY(bool changeVoiceprintOffer READ changeVoiceprintOffer NOTIFY changeChanged)
    // A sávról most készült lenyomat: a sáv „Visszavonás"-a ezt a lenyomatot törli.
    Q_PROPERTY(bool changeVoiceprintCreated READ changeVoiceprintCreated NOTIFY changeChanged)
    // Hamis: a sáv már csak tájékoztat (a most készült lenyomat visszavonása után).
    Q_PROPERTY(bool changeUndoable READ changeUndoable NOTIFY changeChanged)

    // Hang-elemzés (megszólalás-embeddingek): a bizonytalanság és a javaslat alapja.
    Q_PROPERTY(bool voiceAvailable READ voiceAvailable NOTIFY voiceStateChanged)
    Q_PROPERTY(bool embeddingRunning READ embeddingRunning NOTIFY voiceStateChanged)
    Q_PROPERTY(qreal embeddingProgress READ embeddingProgress NOTIFY voiceStateChanged)
    // Nem üres: miért nincs hang-elemzés (egyszer, visszafogottan megjelenítendő).
    Q_PROPERTY(QString voiceNote READ voiceNote NOTIFY voiceStateChanged)

    Q_PROPERTY(QString searchQuery READ searchQuery WRITE setSearchQuery NOTIFY searchChanged)
    Q_PROPERTY(int searchMatchCount READ searchMatchCount NOTIFY searchChanged)
    Q_PROPERTY(int searchCurrent READ searchCurrent NOTIFY searchChanged)   // 0-alapú; -1 = nincs
    Q_PROPERTY(QColor highlightColor READ highlightColor WRITE setHighlightColor NOTIFY highlightColorChanged)
    Q_PROPERTY(QColor highlightCurrentColor READ highlightCurrentColor WRITE setHighlightCurrentColor NOTIFY highlightColorChanged)

    // Az épp lejátszott megszólalás sora; -1 = nincs (vagy a szűrő elrejti).
    Q_PROPERTY(int playingRow READ playingRow NOTIFY playingRowChanged)

public:
    struct SpeakerView {
        QString name;
        int colorIndex = 0;
        int lane = -1;
    };

    explicit TranscriptEditorViewModel(QObject* parent = nullptr);
    ~TranscriptEditorViewModel() override;

    // QML-ből példányosítva a munkamenet a property-k beállítása UTÁN oldódik fel.
    void classBegin() override { m_deferred = true; }
    void componentComplete() override;
    // C++-ból (teszt): a munkamenet feloldása a mostani meetingId / demoVariant szerint.
    void resolveSession();

    // ---- munkamenet -------------------------------------------------------
    QString meetingId() const { return m_meetingId; }
    void setMeetingId(const QString& id);
    QString demoVariant() const { return m_demoVariant; }
    void setDemoVariant(const QString& variant);
    bool demo() const { return m_demoSession != nullptr; }
    bool hasTranscript() const { return !m_utts.isEmpty(); }
    bool legacyTranscript() const { return m_legacyTranscript && m_utts.isEmpty(); }
    QString legacyText() const { return legacyTranscript() ? m_legacyText : QString(); }
    TranscriptListModel* rows() const { return m_rows; }

    // Tesztekhez / beágyazáshoz: a szerkesztő és a személylista közvetlen megadása. A
    // megadott szerkesztő tulajdonosa a hívó marad. nullptr → üres munkamenet.
    void setEditor(tanara::SpeakerEditor* editor);
    tanara::SpeakerEditor* editor() const { return m_editor; }
    void setPeopleProvider(std::function<QVector<tanara::PersonInfo>()> provider);
    // AppController megadása (alapból az App-singletoné). A meetingId ezután ezen át oldódik fel.
    void setController(tanara::AppController* controller);
    // A sín-láthatóság (meetingenként) ebbe a JSON-fájlba kerül; üres → csak memóriában.
    void setUiStatePath(const QString& path);

    // ---- olvasó-állapot ---------------------------------------------------
    int utteranceCount() const { return int(m_utts.size()); }
    int speakerCount() const { return int(m_speakers.size()); }
    QVariantList lanes() const { return m_lanes; }
    QVariantList speakers() const { return m_speakerList; }
    int collapsedCount() const { return m_collapsedShown; }
    bool lanesExpanded() const { return m_lanesExpanded; }
    void setLanesExpanded(bool expanded);
    QVariantList overview() const { return m_overview; }
    int durationMs() const { return int(m_durationMs); }
    int timelineMs() const { return int(m_timelineMs); }
    void setTimelineMs(int ms);

    bool railVisible() const { return m_railVisible; }
    void setRailVisible(bool visible);
    bool uncertainOnly() const { return m_uncertainOnly; }
    void setUncertainOnly(bool on);
    int uncertainCount() const { return m_uncertainCount; }
    int selectedCount() const { return int(m_selected.size()); }
    int currentRow() const;

    bool canUndo() const;
    bool canRedo() const;
    QString undoText() const;
    QString redoText() const;

    bool suggestionActive() const { return m_suggestionAnchor >= 0; }
    int suggestionCount() const { return int(m_suggested.size()); }
    QString suggestionTargetName() const { return m_suggestionTargetName; }
    bool suggestionShown() const { return m_suggestionShown; }
    void setSuggestionShown(bool shown);

    bool changeActive() const { return m_change.active; }
    int changeSerial() const { return m_changeSerial; }
    QString changeText() const { return m_change.text; }
    int changeRow() const;
    int changeRestCount() const { return m_change.restCount; }
    QString changeRestText() const { return m_change.restText; }
    QString changeSourceKey() const { return m_change.restCount > 0 ? m_change.sourceKey : QString(); }
    QString changeTargetKey() const { return m_change.targetKey; }
    bool changeVoiceprintOffer() const { return m_change.active && m_change.voiceprint == Change::Offer; }
    bool changeVoiceprintCreated() const { return m_change.active && m_change.voiceprint == Change::Created; }
    bool changeUndoable() const { return m_change.active && m_change.voiceprint != Change::Removed; }

    bool voiceAvailable() const;
    bool embeddingRunning() const;
    qreal embeddingProgress() const { return m_embeddingProgress; }
    QString voiceNote() const { return m_voiceNote; }

    QString searchQuery() const { return m_searchQuery; }
    void setSearchQuery(const QString& query);
    int searchMatchCount() const { return int(m_matches.size()); }
    int searchCurrent() const { return m_searchCurrent; }
    QColor highlightColor() const { return m_highlight; }
    void setHighlightColor(const QColor& c);
    QColor highlightCurrentColor() const { return m_highlightCurrent; }
    void setHighlightCurrentColor(const QColor& c);

    int playingRow() const { return m_playingRow; }

    // ---- a lista-modell olvassa -------------------------------------------
    const QVector<tanara::EditorUtterance>& utterances() const { return m_utts; }
    SpeakerView speakerView(const QString& key) const { return m_views.value(key); }
    bool isSelected(int utterance) const { return m_selected.contains(utterance); }
    bool isSuggested(int utterance) const { return m_suggestionShown && m_suggested.contains(utterance); }
    // A „Bizonytalan" szűrőben frissen javított sor: a szűrő újbóli alkalmazásáig látható marad
    // (ne tűnjön el a kurzor alól, és ne csússzon más sor a következő kattintás alá).
    bool isSticky(int utterance) const { return m_sticky.contains(utterance); }
    int suggestionAnchor() const { return m_suggestionAnchor; }
    QString richText(int utterance) const;
    static QString timeLabel(qint64 ms);

    // ---- pozíció / lejátszás ---------------------------------------------
    // A lejátszó állása; active = van mit kiemelni (lejátszik, vagy megállt egy soron).
    Q_INVOKABLE void setPlaybackPosition(int ms, bool active);
    Q_INVOKABLE int rowForTime(int ms) const;               // a legközelebbi LÁTHATÓ sor
    Q_INVOKABLE int utteranceForTime(int ms) const;
    Q_INVOKABLE qreal rowStartFraction(int row) const;      // 0..1 az idővonalon
    Q_INVOKABLE qreal rowEndFraction(int row) const;
    Q_INVOKABLE int timeAtFraction(qreal fraction) const;
    Q_INVOKABLE int rowStartMs(int row) const;
    Q_INVOKABLE int rowEndMs(int row) const;
    // A sor megszólalása a soronkénti panelhez: { utteranceId, speakerKey, timeLabel, startMs,
    // endMs }; elválasztónál / érvénytelen sornál üres.
    Q_INVOKABLE QVariantMap rowInfo(int row) const;

    // ---- kijelölés --------------------------------------------------------
    // Kattintás egy soron: sima → csak ez (újra rákattintva megszűnik); toggle (Ctrl) →
    // hozzáad / elvesz; range (Shift) → a horgonytól idáig.
    Q_INVOKABLE void selectRow(int row, bool toggle = false, bool range = false);
    Q_INVOKABLE void selectOnly(int row);
    Q_INVOKABLE void selectRows(int fromRow, int toRow);
    Q_INVOKABLE void clearSelection();
    // Fel / le: a kijelölés léptetése; vissza: az új sor (-1 = nem mozdult).
    Q_INVOKABLE int stepSelection(int delta);
    Q_INVOKABLE bool isRowSelected(int row) const;

    // ---- műveletek (mind egy visszavonási lépés) --------------------------
    // Kattintás a sor egy másik oszlopába. Ha a sor egy több soros kijelölés része, az
    // egész kijelölés megy.
    Q_INVOKABLE bool moveRowToLane(int row, int lane);
    // Húzás: a [fromRow, toRow] sorok a `lane` oszlop beszélőjéhez.
    Q_INVOKABLE bool moveRowsToLane(int fromRow, int toRow, int lane);
    Q_INVOKABLE bool moveSelectionToLane(int lane);             // 1–9 billentyű, chip
    Q_INVOKABLE bool moveSelectionToSpeaker(const QString& speakerKey);
    Q_INVOKABLE bool moveSelectionToPerson(const QString& personName);
    Q_INVOKABLE bool moveSelectionToNewParticipant();           // névtelen új résztvevő
    // EGY sor (a névre kattintva, „Csak ez a sor"): a megszólalás azonosítójával, hogy a
    // panel nyitva tartása alatt változó sorszám (szűrő) ne vihessen el másik sort.
    Q_INVOKABLE bool moveUtteranceToSpeaker(const QString& utteranceId, const QString& speakerKey);
    Q_INVOKABLE bool moveUtteranceToPerson(const QString& utteranceId, const QString& personName);
    Q_INVOKABLE bool moveUtteranceToNewParticipant(const QString& utteranceId);
    // „<Forrás> mind a N sora": a legutóbbi soronkénti áthelyezés forrásának MEGMARADT sorai
    // is a célhoz kerülnek (a két beszélő összevonása) — egy további visszavonási lépés.
    Q_INVOKABLE bool moveRestOfSource();
    // A sáv „Visszavonás" gombja: az átsorolás visszavonása — vagy, ha a sávról épp
    // hanglenyomat készült, PONTOSAN annak a lenyomatnak a törlése (az átsorolás marad).
    Q_INVOKABLE void undoChange();
    // A sáv „Hanglenyomat készítése" gombja (csak changeVoiceprintOffer mellett).
    Q_INVOKABLE bool createVoiceprintFromChange();
    Q_INVOKABLE void dismissChange();                           // a sáv bezárása (a javaslat is megszűnik)
    Q_INVOKABLE QString addParticipant(const QString& personName = QString());
    Q_INVOKABLE bool removeParticipant(const QString& speakerKey);
    Q_INVOKABLE bool reassignSpeaker(const QString& speakerKey, const QString& personName,
                                     bool fixVoiceprints);
    Q_INVOKABLE bool revertSpeakerToAnonymous(const QString& speakerKey, bool fixVoiceprints);
    Q_INVOKABLE bool mergeSpeakers(const QString& fromKey, const QString& intoKey);
    Q_INVOKABLE bool confirmRow(int row);                       // „Jó így"
    Q_INVOKABLE bool acceptSuggestion();
    Q_INVOKABLE void dismissSuggestion();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

    // ---- másolás (vágólap) -------------------------------------------------
    // A sorok szövege a beszélő nevével és az időbélyeggel; beszélőváltáskor új fejsor.
    Q_INVOKABLE QString textOfRows(const QVariantList& rows) const;
    Q_INVOKABLE QString selectionText() const;
    // Vágólapra tesz; vissza: hány megszólalás került rá (0 = semmi).
    Q_INVOKABLE int copyRow(int row);
    Q_INVOKABLE int copySelection();
    Q_INVOKABLE int copyAll();
    Q_INVOKABLE void selectAll();

    // ---- „Következő bizonytalan" -------------------------------------------
    // A `fromRow` utáni (direction < 0: előtti) első bizonytalan sorra lép — körbefordul —,
    // kijelöli és odagörget. Vissza: a sor; -1 = nincs bizonytalan sor.
    Q_INVOKABLE int stepUncertain(int fromRow, int direction = 1);

    // ---- „Meghallgatás": reprezentatív minta egy beszélőtől ----------------
    // { ok, startMs, endMs } — a beszélő egy hosszabb, nem bizonytalan megszólalása
    // (legfeljebb ~12 mp-re vágva).
    Q_INVOKABLE QVariantMap speakerSample(const QString& speakerKey) const;

    // ---- beszélő / hanglenyomat -------------------------------------------
    Q_INVOKABLE QVariantMap speakerInfo(const QString& speakerKey) const;
    // A meeting beszélői a keresőszövegre szűrve (ékezet- és kisbetű-függetlenül, mint a
    // személylista), `excludeKey` nélkül — a `speakers` elemeivel.
    Q_INVOKABLE QVariantList speakersMatching(const QString& query, const QString& excludeKey) const;
    // A személy beszélő-kulcsa ebben a meetingben; üres, ha nem résztvevő.
    Q_INVOKABLE QString speakerKeyForPerson(const QString& personName) const;
    // Magyar névelő a szám elé: igaz → „az" (1, 5, 50…, 1000…), különben „a".
    Q_INVOKABLE static bool needsAz(int number);
    // { supported, reason, usableLines, usableSec, missingSec, sufficient } — reason: miért
    // nem érhető el a hang-elemzés ("model" = nincs hangmodell, "audio" = nincs lekevert hang).
    Q_INVOKABLE QVariantMap voiceprintMaterial(const QString& speakerKey) const;
    // { ok, message, printId, usedLines, usedSec, missingSec } — csak kifejezett műveletre készül.
    Q_INVOKABLE QVariantMap createVoiceprint(const QString& speakerKey);
    // A most készített lenyomat visszavonása: pontosan az a lenyomat törlődik.
    Q_INVOKABLE bool removeVoiceprint(const QString& printId);

    // ---- keresés ----------------------------------------------------------
    // A következő / előző találatra lép; vissza: a sora (-1 = nincs találat).
    Q_INVOKABLE int searchStep(int direction);

    // ---- személyek (a PersonListModel olvassa) ---------------------------
    QVector<tanara::PersonInfo> people() const;
    bool isMeetingPerson(const QString& name) const;

    // ---- demó-állapotok (képernyőképhez) ---------------------------------
    // "selection" | "suggestion" | "suggestionShown" | "filter" | "search" | "searchEmpty" |
    // "rail" | "changeLine" | "changeSelection" | "changeSpeaker" | "changeFilter" |
    // "changeVoiceprint" | "changeVoiceprintDone" | "voiceprintHas" | "voiceprintNone" |
    // "voiceprintDone" | "voiceprintShort".
    // Ha a hang-elemzés még fut, a végén alkalmazódik.
    Q_INVOKABLE void applyDemoState(const QString& state);

signals:
    void meetingIdChanged();
    void demoVariantChanged();
    void sessionChanged();          // másik meeting / új átirat: minden újraolvasandó
    void speakersChanged();
    void overviewChanged();
    void railVisibleChanged();
    void uncertainOnlyChanged();
    void uncertainCountChanged();
    void selectionChanged();
    void undoStateChanged();
    void suggestionChanged();
    void changeChanged();
    void voiceStateChanged();
    void searchChanged();
    void highlightColorChanged();
    void playingRowChanged();
    void peopleChanged();
    // A QML görgessen erre a sorra (keresés, „Megmutatom", javaslat, demó).
    void revealRequested(int row);
    // Rövid, nem modális visszajelzés (a QML a shell.toast-nak adja, ha van).
    void notice(const QString& text);

private:
    void attach(tanara::SpeakerEditor* editor);
    void detach();
    void reloadAll();
    void startEmbeddingIfNeeded();
    void onUtterancesChanged(const QStringList& ids);
    void onSpeakersChanged();
    void onSuggestionChanged();
    void rebuildSpeakers(bool recomputeCollapsed);
    void rebuildOverview();
    void scheduleOverview();
    void updateVoiceNote();
    void updateSearch();
    void updatePlayingRow();
    void setSelection(const QSet<int>& selection, int anchor);
    QStringList selectedIds() const;
    QString textOfUtterances(QVector<int> utterances) const;
    int copyUtterances(const QVector<int>& utterances);
    QStringList idsOfRows(int fromRow, int toRow) const;
    QString laneKey(int lane) const;
    void afterMove(const QString& targetKey);
    enum class MoveTarget { Speaker, Person, NewParticipant };
    // A soronkénti áthelyezések közös útja: művelet + oszlop-kibontás + értesítő sáv.
    // Vissza: a cél-beszélő kulcsa (üres = nem történt semmi).
    QString moveLines(const QStringList& ids, MoveTarget kind, const QString& value);
    void publishChange(const QString& text, const QString& sourceKey, const QString& targetKey,
                       int lastUtterance, bool offerVoiceprint = false);
    void publishWholeSpeakerChange(const QString& fromName, int lines, const QString& targetKey);
    bool canOfferVoiceprint(const QString& speakerKey) const;
    QString voiceprintMessage(const QString& name, const tanara::VoiceprintResult& result) const;
    void clearChange();
    bool loadRailState() const;
    void saveRailState() const;
    void applyPendingDemoState();

    bool m_deferred = false;
    bool m_legacyTranscript = false;
    QString m_legacyText;
    QString m_meetingId;
    QString m_demoVariant;
    QPointer<tanara::AppController> m_controller;
    bool m_controllerInjected = false;
    QPointer<tanara::SpeakerEditor> m_editor;
    bool m_editorInjected = false;
    std::unique_ptr<TranscriptDemoSession> m_demoSession;
    std::function<QVector<tanara::PersonInfo>()> m_peopleProvider;
    QString m_uiStatePath;
    bool m_uiStatePathSet = false;

    TranscriptListModel* m_rows = nullptr;
    QVector<tanara::EditorUtterance> m_utts;
    QHash<QString, int> m_uttIndex;
    QVector<tanara::EditorSpeaker> m_speakers;
    QHash<QString, SpeakerView> m_views;
    QStringList m_laneKeys;             // a látható oszlopok kulcsai, sorrendben
    QSet<QString> m_collapsed;          // összecsukott (ritkán beszélő) kulcsok
    bool m_lanesExpanded = false;
    int m_collapsedShown = 0;
    QVariantList m_lanes;
    QVariantList m_speakerList;
    QVariantList m_overview;
    qint64 m_durationMs = 0;
    qint64 m_timelineMs = 0;
    QTimer m_overviewTimer;

    bool m_railVisible = false;
    bool m_uncertainOnly = false;
    int m_uncertainCount = 0;
    QSet<int> m_selected;
    int m_anchor = -1;

    QSet<int> m_suggested;
    int m_suggestionAnchor = -1;
    QString m_suggestionTargetName;
    bool m_suggestionShown = false;

    struct Change {
        bool active = false;
        QString text;
        QString sourceKey;
        QString targetKey;
        int utterance = -1;
        int restCount = 0;
        QString restText;
        // A hanglenyomat-ajánlat állapota: nincs / ajánlható / most készült / visszavonva.
        enum Voiceprint { None, Offer, Created, Removed };
        Voiceprint voiceprint = None;
        QString printId;                // a sávról készült lenyomat (a visszavonásához)
    };
    Change m_change;
    int m_changeSerial = 0;
    int m_opDepth = 0;                  // > 0: saját művelet fut (a sávot utána mi írjuk ki)
    QSet<int> m_sticky;

    qreal m_embeddingProgress = 0.0;
    bool m_cancelRequested = false;
    bool m_embeddingFailed = false;
    QString m_voiceNote;

    QString m_searchQuery;
    mutable QVector<QString> m_folded;
    QVector<int> m_matches;
    QSet<int> m_matchSet;
    int m_searchCurrent = -1;
    QColor m_highlight{QStringLiteral("#fcedcd")};
    QColor m_highlightCurrent{QStringLiteral("#eac992")};

    int m_playbackMs = 0;
    bool m_playbackActive = false;
    int m_playingUtt = -1;
    int m_playingRow = -1;

    QString m_pendingDemoState;
};

} // namespace tanara_qml
