#pragma once
//
// ReviewGroupsModel — az „Átnézendő" nézet (handoff-v3 E2) sorai: csoport-kártyák, a kinyitott
// („Egyenként") csoport sorai a soronkénti műveletekkel, és az aktív, összecsukott csoport alatti
// „··· N sor ebben a csoportban · Kinyitás" sor. A csoportokat a TranscriptEditorViewModel adja
// (a core SpeakerEditor::reviewGroups() + a csoportokon kívüli, hangra kétes sorok).
//
// A kinyitott csoportban a már eldöntött sor („Jó így", átsorolva) a helyén marad, amíg a csoport
// nyitva van — ne ugorjon el a kurzor alól (mint a régi „Bizonytalan" szűrőben). A modell
// újraszámoláskor nem resetel: a változatlan sorok maradnak, a különbség beszúrás / törlés.
//
#include <QAbstractListModel>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace tanara_qml {

class TranscriptEditorViewModel;

class ReviewGroupsModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("A TranscriptEditorViewModel.reviewGroups adja")
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        RowKindRole = Qt::UserRole + 1,     // "group" | "line" | "more"
        GroupIdRole,
        GroupKindRole,                      // ReviewView::kind
        TitleRole,
        SubtitleRole,
        CountRole,                          // a csoport mostani sorainak száma
        EvidenceRole,                       // [evidence map] (TranscriptEditorViewModel::evidenceMap)
        ProposedKeyRole,
        ProposedNameRole,
        ActionableRole,                     // döntést kér (nem tájékoztató)
        CanApplyRole,                       // van javaslat („Mind a N → X")
        ExpandedRole,
        ActiveRole,                         // az épp kezelt csoport (accentLine keret)
        IconRole,                           // a csoport ikonja
        UtteranceIdRole,
        SpeakerKeyRole,
        SpeakerNameRole,
        ColorIndexRole,
        TimeLabelRole,
        LineTextRole,
        StartMsRole,
        EndMsRole,
        ReasonRole,                         // "side" | "voice" | "tag" | ""
        LikelyKeyRole,                      // a sor javasolt beszélője (a csoport javaslata)
        LikelyNameRole,
        DecidedRole,                        // a sor már eldöntve (a helyén marad, művelet nélkül)
        DecidedTextRole,                    // „javítva" | „jó így"
    };
    Q_ENUM(Role)

    explicit ReviewGroupsModel(TranscriptEditorViewModel* owner);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return int(m_rows.size()); }

    Q_INVOKABLE void setExpanded(const QString& groupId, bool expanded);
    Q_INVOKABLE bool isExpanded(const QString& groupId) const { return m_expanded.contains(groupId); }
    Q_INVOKABLE int rowOfGroup(const QString& groupId) const;
    Q_INVOKABLE int rowOfLine(const QString& utteranceId) const;
    // A `fromRow` utáni (direction < 0: előtti) döntésre váró sor — ha zárt csoportban van, a
    // csoport kinyílik. Körbefordul. Vissza: a sor; -1 = nincs ilyen.
    Q_INVOKABLE int stepLine(int fromRow, int direction = 1);
    Q_INVOKABLE QString utteranceAt(int row) const;
    Q_INVOKABLE QString groupAt(int row) const;

    // ---- a nézetmodell hívja ----
    void sync();                                        // a csoportok változtak
    void refreshUtterances(const QVector<int>& utteranceIndices);
    void refreshAll();                                  // nevek / színek változtak
    void reset();                                       // új munkamenet: kinyitás, ragadás törölve

signals:
    void countChanged();

private:
    enum Kind { Group, Line, More };
    struct Row {
        Kind kind = Group;
        int view = -1;          // index a TranscriptEditorViewModel::reviewViews()-ban
        QString utteranceId;    // Line
        QString key;            // stabil kulcs a különbség-frissítéshez
    };
    QVector<Row> compute();
    void apply(const QVector<Row>& target);
    QString activeId() const;

    TranscriptEditorViewModel* m_vm;
    QVector<Row> m_rows;
    QSet<QString> m_expanded;
    QHash<QString, QStringList> m_shown;    // kinyitott csoport → a megjelenített sorok (ragadósak)
    QString m_active;
};

} // namespace tanara_qml
