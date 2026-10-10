#pragma once
//
// CandidateListModel — jelöltek bizonyítékkal (handoff-v3 E1 „JAVASOLT", E3 „NEM Ő? VALÓJÁBAN…").
// Egy sorra (utteranceId) a SpeakerEditor::lineCandidates, egy egész beszélőre (speakerKey, üres
// utteranceId mellett) a speakerCandidates rangsora. Soronkénti módban a mostani beszélő a lista
// végén áll („most ő", halványan, az ellene szóló bizonyítékkal); a másik sávon beszélő jelölt
// halvány, de választható. A háttér-elemzés végén magától frissül.
//
//   CandidateListModel { editor: editorVm; utteranceId: "u12"; limit: 3; query: search.text }
//
#include "TranscriptEditorViewModel.h"

#include <QAbstractListModel>
#include <QPointer>
#include <QVariantList>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace tanara_qml {

class CandidateListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(tanara_qml::TranscriptEditorViewModel* editor READ editor WRITE setEditor NOTIFY editorChanged)
    Q_PROPERTY(QString utteranceId READ utteranceId WRITE setUtteranceId NOTIFY queryChanged)
    Q_PROPERTY(QString speakerKey READ speakerKey WRITE setSpeakerKey NOTIFY queryChanged)
    // Szűrés névre (ékezet- és kisbetű-független). Nem üres keresőnél nincs darabszám-korlát.
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    // Legfeljebb ennyi jelölt (a mostani beszélőn kívül); 0 = mind.
    Q_PROPERTY(int limit READ limit WRITE setLimit NOTIFY queryChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        SpeakerKeyRole = Qt::UserRole + 1,  // üres: a meetingen kívüli ismert személy
        NameRole,
        PersonNameRole,
        ColorIndexRole,                     // -1: a meetingen kívüli személy (semleges avatar)
        SubTextRole,                        // „86 sor itt" / „most ő" / „12 megbeszélés"
        EvidenceRole,                       // [evidence map]
        OtherSideRole,
        CurrentRole,                        // a sor mostani beszélője
        DimmedRole,                         // halvány (másik sáv / mostani beszélő)
        KeyHintRole,                        // "1".."3" (gyorsbillentyű) vagy ""
        ScoreRole,
    };
    Q_ENUM(Role)

    explicit CandidateListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    TranscriptEditorViewModel* editor() const { return m_editor; }
    void setEditor(TranscriptEditorViewModel* editor);
    QString utteranceId() const { return m_utteranceId; }
    void setUtteranceId(const QString& id);
    QString speakerKey() const { return m_speakerKey; }
    void setSpeakerKey(const QString& key);
    QString query() const { return m_query; }
    void setQuery(const QString& query);
    int limit() const { return m_limit; }
    void setLimit(int limit);
    int count() const { return int(m_items.size()); }

    // A sor adatai (QML: választás billentyűvel / Enterrel).
    Q_INVOKABLE QVariantMap get(int row) const;
    // A `keyHint` szerinti sor (1–3); -1 = nincs ilyen.
    Q_INVOKABLE int rowForKey(int key) const;
    Q_INVOKABLE void refresh();

signals:
    void editorChanged();
    void queryChanged();
    void countChanged();

private:
    struct Item {
        QString speakerKey;
        QString name;
        QString personName;
        int colorIndex = -1;
        QString subText;
        QVariantList evidence;
        bool otherSide = false;
        bool current = false;
        QString keyHint;
        double score = 0.0;
    };
    void scheduleRefresh();

    QPointer<TranscriptEditorViewModel> m_editor;
    QString m_utteranceId;
    QString m_speakerKey;
    QString m_query;
    int m_limit = 0;
    QVector<Item> m_items;
    bool m_refreshQueued = false;
};

} // namespace tanara_qml
