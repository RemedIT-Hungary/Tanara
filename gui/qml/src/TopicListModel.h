#pragma once
//
// Tanara QML — a témánkénti elemzés (M08) téma-listája: kártyánként cím, leírás, állapot
// (vár / sorban / fut / kész / hibás üzenettel) és a kész elemzés eredménye.
//
// A lemez az igazság (summary.topics.json + summary.analyses.json + a megmaradt hibák), így
// újraindítás után onnan folytatódik, ahol abbamaradt. Minden szerkesztés (hozzáadás,
// átírás, törlés, átrendezés) az AppController::setMeetingTopics-on át azonnal perzisztál.
// A core jeleire (topicsChanged / topicStatusChanged / topicAnalysisReady) SORONKÉNT frissül:
// azonos téma-sorrendnél csak dataChanged megy, teljes reset csak ismeretlen átrendezésnél.
//
// A SummaryViewModel birtokolja (QML-ből: summaryVm.topics); controller nélkül kitalált
// mintaadattal dolgozik, a szerkesztés ilyenkor csak memóriában történik.
//
#include "tanara/Types.h"
#include "tanara/jobs/JobTypes.h"

#include <QAbstractListModel>
#include <QPointer>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace tanara {
class AppController;
}

namespace tanara_qml {

class TopicListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("A TopicListModel-t a SummaryViewModel adja (topics).")

    Q_PROPERTY(int count READ count NOTIFY countsChanged)
    // „5 téma · 2 kész · 1 fut · 1 hibás · 1 vár”
    Q_PROPERTY(QString countsText READ countsText NOTIFY countsChanged)
    // Hány témának nincs kész elemzése és nem is fut / áll sorban (→ „Hiányzók elemzése”).
    Q_PROPERTY(int missingCount READ missingCount NOTIFY countsChanged)
    Q_PROPERTY(int doneCount READ doneCount NOTIFY countsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY countsChanged)   // van futó vagy sorban álló téma

public:
    enum Role {
        TopicIdRole = Qt::UserRole + 1,
        TitleRole,
        SummaryRole,          // a téma rövid leírása
        StateRole,            // waiting | queued | running | done | failed
        ErrorRole,
        ErrorDetailRole,
        HasResultRole,
        ResultTextRole,       // a kész elemzés szövege (markdown)
        ResultDecisionsRole,  // QStringList
        ResultActionsRole,    // [{ text, owner, due }]
    };

    explicit TopicListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_items.size()); }
    QString countsText() const;
    int missingCount() const;
    int doneCount() const;
    bool busy() const;

    // A forrás beállítása: valódi (controller + meeting) vagy demó (controller == nullptr).
    void bind(tanara::AppController* controller, const QString& meetingId);
    void loadDemo();
    void clear();

    // Szerkesztés — mind azonnal perzisztál. Üres címmel nem történik semmi (false).
    Q_INVOKABLE bool addTopic(const QString& title, const QString& summary);
    Q_INVOKABLE bool updateTopic(int row, const QString& title, const QString& summary);
    Q_INVOKABLE void removeTopic(int row);
    Q_INVOKABLE void moveTopic(int from, int to);
    // Egyetlen téma futó / sorban álló elemzésének megszakítása.
    Q_INVOKABLE void cancelTopic(int row);
    Q_INVOKABLE QString topicIdAt(int row) const;

    static QString stateName(tanara::TopicState state);

signals:
    void countsChanged();

private:
    struct Item {
        tanara::SummaryTopic topic;
        tanara::TopicState state = tanara::TopicState::Waiting;
        QString error;
        QString errorDetail;
        bool hasResult = false;
        tanara::TopicAnalysis result;
        bool operator==(const Item& o) const;
    };

    void sync();                 // a lemez-állapot beolvasása, soronkénti különbség-képzéssel
    void persist();              // a jelenlegi sorrend mentése (setMeetingTopics)
    QVector<tanara::SummaryTopic> topicList() const;

    QPointer<tanara::AppController> m_controller;
    QList<QMetaObject::Connection> m_connections;
    QString m_meetingId;
    QVector<Item> m_items;
};

} // namespace tanara_qml
