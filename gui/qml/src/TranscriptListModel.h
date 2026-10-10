#pragma once
//
// TranscriptListModel — az átirat-szerkesztő ListView-jának sorai. Egy sor vagy egy
// MEGSZÓLALÁS, vagy (a „Bizonytalan" szűrőben) egy „··· N biztos sor elrejtve" elválasztó.
//
// Az adat a TranscriptEditorViewModel pillanatképében él (megszólalások, beszélők,
// kijelölés, javaslat, keresés); ez az osztály a LÁTHATÓ sorok leképezését tartja és a
// finom-szemcsés értesítéseket adja ki. Egyetlen sor áthelyezésére SOSEM reseteli a modellt:
// dataChanged megy az érintett sorokra, a szűrőben pedig beszúrás / törlés a különbségre.
//
#include <QAbstractListModel>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace tanara_qml {

class TranscriptEditorViewModel;

class TranscriptListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("A TranscriptEditorViewModel.rows adja")
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        KindRole = Qt::UserRole + 1,    // "utterance" | "gap"
        UtteranceIdRole,
        UtteranceIndexRole,
        StartMsRole,
        EndMsRole,
        TimeLabelRole,                  // "01:16" / "1:02:03"
        TextRole,
        RichTextRole,                   // keresési találat kiemelve (HTML); üres = nincs találat
        SpeakerKeyRole,
        SpeakerNameRole,
        ColorIndexRole,
        LaneRole,                       // a sáv oszlop-indexe; -1 = az összecsukott „+N" csoportban
        HeadRole,                       // beszélőváltás → névsor látszik
        FirstRole,                      // a lista első sora (nincs fölötte forduló-térköz)
        UncertainRole,
        CorrectedRole,
        SelectedRole,
        SuggestedRole,                  // a „hasonló sorok" javaslat része, és épp mutatjuk
        SuggestionAnchorRole,           // a javaslatot kiváltó (kézzel javított) sor
        HiddenCountRole,                // elválasztó: ennyi biztos sor van elrejtve
        NoisyRole,                      // „egymásra beszéltek": nem hangminta (automatikus vagy kézi)
        NoisyOverlapRole,               // az átfedés-szabály szerint zajos (a kézi felülírástól függetlenül)
        LikelySpeakerKeyRole,           // az újraellenőrzés javaslata: a hangra jobban illő beszélő
        LikelySpeakerNameRole,
        // v3 sor-jelölők (handoff-v3 E1, 14. döntés)
        UncertainReasonRole,            // "side" | "voice" | "tag" | "" — a „bizonytalan · …" pirula oka
        ConfirmedRole,                  // „Jó így" (pipa)
        ShortRole,                      // rövidebb, mint amit a hang-elemzés megbízhatóan megítél („rövid")
        SideConflictRole,               // a sor a beszélője oldalával ellentétes sávon szólt
        NewPersonRole,                  // a sor beszélője az imént létrehozott személy („új személy")
    };
    Q_ENUM(Role)

    struct Row {
        bool gap = false;
        int utterance = -1;     // megszólalás-index; elválasztónál az elrejtett futam első indexe
        int hidden = 0;         // elválasztó: az elrejtett sorok száma
        int key() const { return gap ? 2 * utterance : 2 * utterance + 1; }
    };

    explicit TranscriptListModel(TranscriptEditorViewModel* owner);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return int(m_rows.size()); }

    const QVector<Row>& rows() const { return m_rows; }
    // A megszólalás sora a listában; -1, ha épp nem látszik (szűrő).
    int rowOfUtterance(int utteranceIndex) const;
    // A sor megszólalás-indexe; elválasztónál / érvénytelen sornál -1.
    int utteranceOfRow(int row) const;
    // A megszólaláshoz legközelebbi látható megszólalás-sor (szűrőben is ad találatot).
    int nearestRow(int utteranceIndex) const;

    // ---- a nézetmodell hívja ----
    void rebuild();                                     // teljes újraépítés (modell-reset)
    void syncFilter();                                  // szűrő-sorok frissítése különbséggel
    void notifyUtterances(const QVector<int>& utteranceIndices, const QList<int>& roles = {});
    void notifyAll(const QList<int>& roles);

signals:
    void countChanged();

private:
    QVector<Row> computeRows() const;
    bool headAt(int row) const;

    TranscriptEditorViewModel* m_vm;
    QVector<Row> m_rows;
    QVector<int> m_rowOfUtt;    // megszólalás-index → sor (-1 = rejtve)
    void reindex();
};

} // namespace tanara_qml
