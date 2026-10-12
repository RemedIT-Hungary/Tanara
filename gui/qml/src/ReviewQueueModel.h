#pragma once
//
// ReviewQueueModel — az Átnézendő „A" koncepciójának (Fókusz-sor) munkalistája (S3). A
// TranscriptEditorViewModel.reviewQueue adja; a fókusz-nézet (S4) és a nyitó-javaslat /
// mintázat-ajánlat (S5) ebből dolgozik.
//
// Elv (koncepció-dokumentum 2.2–2.3): a munkalista egy FAGYASZTOTT, IDŐRENDI pillanatkép, amely
// a belépéskor (start) vagy a kör-záráskor (startNextRound) készül a mostani kétes sorokból (a
// döntést kérő ReviewView-k sorai + minden „uncertain" sor; a már eldöntött — megerősített,
// javított, nem azonosítható — sorok nélkül). A háttér-elemzés (rebuildReview → syncFromReview)
// SOSEM rendezi át:
//  - a döntött sor a HELYÉN marad (`decided` / `ignored`), a kurzor a következő nyitott sorra lép;
//  - az új kétes sorok egy külső „beérkező" halmazba kerülnek (incomingCount, „+N új vár"),
//    és csak kérésre (admitIncoming) vagy kör-záráskor (startNextRound) jönnek a lista VÉGÉRE;
//  - egy sorban álló sor javaslatának változása csak a soron jelzés (suggestionUpdated);
//  - egy sorban álló sor, ami a háttérben már nem kétes, `alreadyDecided` lesz (nem tűnik el).
// Sorrendet csak a later() (a sor a lista végére) és a bringToFront() (S5 „Egyenként") változtat.
//
// Az állapot a sor jelzőiből származik (nem tárolt döntés): ignored → `ignored`; confirmed /
// manuallyCorrected → `decided`; Később-jel → `later`; már nem kétes → `alreadyDecided`;
// különben `pending`. Így a visszavonás magától visszaállítja a sort (és a kurzor rá ugrik).
//
#include "TranscriptEditorViewModel.h"

#include <QAbstractListModel>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace tanara_qml {

class ReviewQueueModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("A TranscriptEditorViewModel.reviewQueue adja")
    // Fut-e munkalista (start után, stop / új munkamenet előtt).
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    // A kör sorszáma (1-től).
    Q_PROPERTY(int round READ round NOTIFY roundChanged)
    Q_PROPERTY(int count READ count NOTIFY countsChanged)
    // A kurzor (sor-index a listában; -1 = nincs: üres lista vagy kör vége).
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentChanged)
    Q_PROPERTY(QString currentUtterance READ currentUtterance NOTIFY currentChanged)
    // A kurzor sora a szerepek nevével (lásd roleNames); üres map, ha nincs.
    Q_PROPERTY(QVariantMap current READ current NOTIFY currentChanged)
    // Számlálók a haladás-sávhoz („N / M"): decided + ignored = eldöntve.
    Q_PROPERTY(int decidedCount READ decidedCount NOTIFY countsChanged)
    Q_PROPERTY(int ignoredCount READ ignoredCount NOTIFY countsChanged)
    Q_PROPERTY(int laterCount READ laterCount NOTIFY countsChanged)
    Q_PROPERTY(int alreadyDecidedCount READ alreadyDecidedCount NOTIFY countsChanged)
    // Nyitott sorok: pending + az át nem ugrott alreadyDecided.
    Q_PROPERTY(int openCount READ openCount NOTIFY countsChanged)
    // A kör kész: van munkalista, és nincs nyitott sora (a later sorok nem nyitottak).
    Q_PROPERTY(bool roundComplete READ roundComplete NOTIFY countsChanged)
    // A beérkező halmaz („+N új vár"): kétes sorok, amelyek nincsenek a munkalistán; időrendben.
    Q_PROPERTY(int incomingCount READ incomingCount NOTIFY incomingChanged)
    Q_PROPERTY(QStringList incomingIds READ incomingIds NOTIFY incomingChanged)

public:
    enum State { Pending, Decided, Later, Ignored, AlreadyDecided };
    Q_ENUM(State)

    enum Role {
        UtteranceIdRole = Qt::UserRole + 1,
        OrderRole,              // sor-index a listában (= a modell sora)
        StateRole,              // "pending" | "decided" | "later" | "ignored" | "alreadyDecided"
        CurrentRole,            // a kurzor ezen a soron
        ReasonKindRole,         // a csoport-fajta: "sideConflict" | "coreMismatch" | "similarToNewPerson" | "uncertain"
        ReasonTitleRole,        // a csoport címe (ReviewView::title, már fordított)
        ReasonsRole,            // a csoport bizonyítékai (evidence map-ek: kind, polarity, text, value, manual …)
        GroupIdRole,            // a ReviewView id-je (a beemeléskori)
        LikelyKeyRole,          // javasolt beszélő kulcsa (üres: meetingen kívüli személy / új beszélő / nincs)
        LikelyNameRole,         // javasolt beszélő neve (a meetingen kívüli személyé is)
        LikelyNewSideRole,      // a javaslat új beszélő ezen az oldalon: "local" | "remote" | ""
        SuggestionUpdatedRole,  // a javaslat a beemelés óta változott (a fókuszban „javaslat frissült")
        SpeakerKeyRole,         // a sor MOSTANI beszélője
        SpeakerNameRole,
        ColorIndexRole,
        LaneRole,               // a beszélő oszlopa a sínen (-1 = összecsukva)
        OriginalKeyRole,        // a beszélő a beemeléskor (a „jelenlegi" a mintázatban)
        OriginalNameRole,
        StartMsRole,
        EndMsRole,
        TimeLabelRole,          // „12:58"
        LineTextRole,
        DecidedKindRole,        // "corrected" | "confirmed" | "ignored" | ""
        SampleStateRole,        // "sample" | "noSample" | ""
        AdmittedRole,           // a beérkező halmazból került be (admitIncoming / új kör)
        ConsequenceRole,        // beemeléskor „a mostani döntéseid miatt" lett kétes
        RoundRole,              // melyik körben került a listára
    };
    Q_ENUM(Role)

    explicit ReviewQueueModel(TranscriptEditorViewModel* owner);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool active() const { return m_active; }
    int round() const { return m_round; }
    int count() const { return int(m_items.size()); }
    int currentIndex() const { return m_current; }
    QString currentUtterance() const;
    QVariantMap current() const { return get(m_current); }
    int decidedCount() const;
    int ignoredCount() const;
    int laterCount() const;
    int alreadyDecidedCount() const;
    int openCount() const;
    bool roundComplete() const { return m_active && openCount() == 0; }
    int incomingCount() const { return int(m_incoming.size()); }
    QStringList incomingIds() const { return m_incoming; }

    // ---- munkalista ----
    // Új pillanatkép a mostani kétes sorokból (1. kör); a kurzor az első nyitott sorra.
    Q_INVOKABLE void start();
    // Kilépés: a lista kiürül, a szinkron leáll.
    Q_INVOKABLE void stop();
    // Kör-zárás után: új pillanatkép a beérkezőkből és / vagy a „később" sorokból (a még nyitott
    // sorok is átkerülnek), időrendben; a kör-számláló nő, a mintázat-napló törlődik.
    Q_INVOKABLE void startNextRound(bool includeIncoming = true, bool includeLater = true);
    // A beérkezők a lista VÉGÉRE (egymás közt időrendben); a kurzor nem mozdul — ha a kör kész
    // volt, az első beemelt sorra áll. Vissza: a beemelt sorok száma.
    Q_INVOKABLE int admitIncoming();

    // ---- kurzor ----
    Q_INVOKABLE bool next();            // a következő sorra (bármilyen állapotú); a végén nem fordul
    Q_INVOKABLE bool previous();        // az előző sorra (döntöttre is; ott újra dönthető); -1-ről az utolsóra
    Q_INVOKABLE bool advance();         // a következő NYITOTT sorra (körbefordul); nincs: -1 (kör vége)
    Q_INVOKABLE bool jumpTo(int index);
    // „Később": a kurzor (pending) sora a lista VÉGÉRE, `later` állapottal; a kurzor a következő
    // nyitott sorra. alreadyDecided soron: átugrás (a sor a helyén marad, többé nem nyitott);
    // döntött / later soron: next().
    Q_INVOKABLE bool later();
    // S5 „Egyenként": a megadott (nyitott) sorok a kurzor helyére, időrendben; a kurzor az elsőre.
    Q_INVOKABLE int bringToFront(const QStringList& utteranceIds);
    Q_INVOKABLE int indexOf(const QString& utteranceId) const { return m_pos.value(utteranceId, -1); }
    Q_INVOKABLE QVariantMap get(int index) const;

    // ---- összesítők ----
    // Kör-zárás: { round, total, decided, ignored, later, alreadyDecided, open,
    //   sampleDelta: [{ key, name, colorIndex, delta }] (a kör eleje óta, csökkenő),
    //   incoming, incomingIds, incomingInReviewed (a kör sorainak időszakán belül),
    //   incomingCausedByDecisions, causedIds }.
    Q_INVOKABLE QVariantMap roundSummary() const;
    // Mintázat (S5): az utolsó 6 döntésből a leggyakoribb (ok, jelenlegi, javasolt) hármas, ha
    // ≥ 4-szer szerepel: { key, reasonKind, currentKey, currentName, proposedKey, proposedName,
    //   hits, window, sameAhead: [id] (a kurzor során KÍVÜL a nyitott, azonos hármasú sorok,
    //   listasorrendben), currentMatches (a kurzor sora is ilyen) }. Üres map: nincs mintázat.
    Q_INVOKABLE QVariantMap recentPattern() const;
    // „Nem": ez a hármas ebben a körben nem ajánlódik újra.
    Q_INVOKABLE void dismissPattern(const QString& key);
    // Nyitó-javaslatok (S5) a kör elejéről: a ≥ 15 soros, erős (hang ≥ 75 % vagy sáv-ellentmondás
    // kézi beosztással), javaslattal bíró csoportok: [{ groupId, kind, title, subtitle, ids,
    //   count, pendingCount, currentKey, currentName, proposedKey, proposedName, newSide,
    //   voicePct (-1: nincs), manualSide, spotIds (3 időben szórt sor) }].
    Q_INVOKABLE QVariantList openers() const;

    // ---- a nézetmodell hívja ----
    // A start() explicit bemenettel (teszt / beágyazás): a pillanatkép és a nyitó-javaslatok
    // ezekből a csoportokból / sorokból (a sor-jelzők élőben a VM-ből).
    void startFrom(const QVector<TranscriptEditorViewModel::ReviewView>& views,
                   const QVector<tanara::EditorUtterance>& utterances);
    // A háttér-elemzés eredménye (a VM rebuildReview-ja után): csak a beérkező halmaz, a
    // javaslat-frissülések és az alreadyDecided állapot változik; sorrend soha.
    void syncFromReview();
    // Ugyanez explicit bemenettel (teszt / beágyazás).
    void syncFromReview(const QVector<TranscriptEditorViewModel::ReviewView>& views,
                        const QVector<tanara::EditorUtterance>& utterances);
    // A VM döntés-hívásai / visszavonás után: az érintett sorok állapota (sorrend nem változik).
    // Döntés a kurzor során → a kurzor a következő nyitott sorra; visszavont döntés → a kurzor
    // a visszaállt sorra.
    void refreshUtterances(const QVector<int>& utteranceIndices);
    void refreshAll();                  // nevek / színek / oszlopok változtak

    static QString stateName(State s);

    static constexpr int kPatternWindow = 6;
    static constexpr int kPatternMinHits = 4;
    static constexpr int kOpenerMinLines = 15;
    static constexpr double kOpenerMinVoice = 0.75;

signals:
    void activeChanged();
    void roundChanged();
    void countsChanged();
    void currentChanged();
    void incomingChanged();
    void patternChanged();
    // Egy sorban álló sor javaslata a beemelés óta megváltozott (a SuggestionUpdatedRole is igaz).
    void suggestionUpdated(const QString& utteranceId);

private:
    struct Doubt {
        QString groupId;
        QString kind;
        QString title;
        QVariantList evidence;
        QString proposedKey;
        QString proposedName;
        QString newSide;
    };
    struct Item {
        QString id;
        QString originalKey;
        Doubt reason;               // a beemeléskori ok (fagyasztva)
        QString likelyKey;          // a javaslat (frissülhet, amíg a sor nyitott)
        QString likelyName;
        QString likelyNewSide;
        bool suggestionUpdated = false;
        bool later = false;
        bool skipped = false;       // alreadyDecided, átugorva
        bool admitted = false;
        bool consequence = false;
        int round = 1;
        State state = Pending;
        QString sig;                // a döntés-jelzők lenyomata (változás-észleléshez)
    };
    struct Opener {
        Doubt view;
        QString currentKey;
        QStringList ids;
        int voicePct = -1;
        bool manualSide = false;
    };
    struct PatternEntry {
        QString id;
        QString key;                // üres: nem áthelyezés (megerősítés, nem azonosítható, később)
    };

    QHash<QString, Doubt> computeDoubt(const QVector<TranscriptEditorViewModel::ReviewView>& views,
                                       const QVector<tanara::EditorUtterance>& utts) const;
    const tanara::EditorUtterance* utt(const QString& id) const;
    State stateOf(const Item& it) const;
    QString sigOf(const QString& id) const;
    Item makeItem(const QString& id, const Doubt& d, bool admitted) const;
    void setItems(QVector<Item> items);
    void rebuildPos();
    void setCurrent(int index, bool force = false);
    int nextOpen(int from) const;           // from-tól (bezárva) előre, körbe
    bool isOpen(const Item& it) const;
    void updateIncoming();
    void computeOpeners(const QVector<TranscriptEditorViewModel::ReviewView>& views);
    void beginRoundBaseline();
    QSet<QString> grownSpeakers() const;
    bool causedByDecisions(const QString& id, const QSet<QString>& grown) const;
    QString patternKey(const Item& it, const QString& targetKey) const;
    void emitRow(int row);
    void sortByTime(QStringList& ids) const;

    TranscriptEditorViewModel* m_vm;
    bool m_active = false;
    int m_round = 1;
    QVector<Item> m_items;
    QHash<QString, int> m_pos;
    int m_current = -1;
    QHash<QString, Doubt> m_doubt;          // a legutóbbi szinkron kétes sorai
    QStringList m_incoming;
    QSet<QString> m_roundStartDoubt;        // a kör elején kétes sorok (a következmény-jelöléshez)
    QHash<QString, int> m_roundStartSamples;    // beszélő → sessionAdded a kör elején
    QVector<PatternEntry> m_log;            // a kör döntései (a legfrissebb a végén)
    QSet<QString> m_dismissed;
    QVector<Opener> m_openers;
};

} // namespace tanara_qml
