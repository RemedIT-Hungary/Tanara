#pragma once
//
// LibraryListModel — a könyvtár-oldalsáv listamodellje a core MeetingLibrary fölött.
//
//  - Sorok: megbeszélések legújabb elöl; dátum-szekció, cím, meta („okt. 2. · 30 p”), a
//    három állapot-ikon kulcsa (done | running | error | stale | missing) + buboréksúgó,
//    keresésnél a cím- és átirat-találat három darabban (előtte / találat / utána).
//  - Lekérdezés: searchText (késleltetve fut), noTranscript, noSummary, people.
//  - Frissítés: a MeetingLibrary jeleire INKREMENTÁLISAN (egy sor dataChanged / beszúrás /
//    törlés); teljes reset csak akkor, ha a találatlista szerkezete megváltozott.
//  - Controller nélkül (App.demo) a kitalált mintakönyvtárból dolgozik (LibraryDemoData).
//
#include "tanara/library/MeetingLibrary.h"

#include <QAbstractListModel>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace tanara {
class AppController;
}

namespace tanara_qml {

class LibraryListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY queryChanged)
    Q_PROPERTY(bool noTranscript READ noTranscript WRITE setNoTranscript NOTIFY queryChanged)
    Q_PROPERTY(bool noSummary READ noSummary WRITE setNoSummary NOTIFY queryChanged)
    Q_PROPERTY(QStringList people READ people WRITE setPeople NOTIFY queryChanged)
    // Van-e bármilyen szűrés (keresés vagy chip) — ekkor nincs dátum-szekció, van találatszám.
    Q_PROPERTY(bool filtered READ filtered NOTIFY queryChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    // A könyvtár teljes mérete szűrés nélkül (0 → üres könyvtár, M01).
    Q_PROPERTY(int totalCount READ totalCount NOTIFY countChanged)
    Q_PROPERTY(QString resultText READ resultText NOTIFY resultChanged)
    // A „+ Szűrő” menü személyei: [{ name, count, colorIndex }], gyakoriság szerint.
    Q_PROPERTY(QVariantList peopleOptions READ peopleOptions NOTIFY peopleOptionsChanged)
    // Csak demó / képernyőkép: az üres könyvtár (M01) kikényszerítése.
    Q_PROPERTY(bool forceEmpty READ forceEmpty WRITE setForceEmpty NOTIFY forceEmptyChanged)

public:
    enum Role {
        MeetingIdRole = Qt::UserRole + 1,
        TitleRole,
        MetaRole,
        SectionRole,            // szekció-cím („Ma”…); szűrt listában üres
        TranscriptStateRole,
        SummaryStateRole,
        IdentifyStateRole,
        TranscriptTipRole,
        SummaryTipRole,
        IdentifyTipRole,
        TitleBeforeRole,        // a cím három darabban (találat nélkül: csak az „after”)
        TitleMatchRole,
        TitleAfterRole,
        SnippetBeforeRole,
        SnippetMatchRole,
        SnippetAfterRole,
        SnippetMsRole,
        HasSnippetRole,
    };

    explicit LibraryListModel(QObject* parent = nullptr);

    // A controller alapból az App-singletoné; tesztben injektálható.
    void setController(tanara::AppController* controller);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString searchText() const { return m_query.text; }
    void setSearchText(const QString& text);
    bool noTranscript() const { return m_query.noTranscript; }
    void setNoTranscript(bool on);
    bool noSummary() const { return m_query.noSummary; }
    void setNoSummary(bool on);
    QStringList people() const { return m_query.people; }
    void setPeople(const QStringList& people);
    bool filtered() const { return !m_query.isEmpty(); }
    int count() const { return int(m_entries.size()); }
    int totalCount() const { return m_total; }
    QString resultText() const;
    QVariantList peopleOptions() const;
    bool forceEmpty() const { return m_forceEmpty; }
    void setForceEmpty(bool on);

    Q_INVOKABLE int indexOfMeeting(const QString& meetingId) const;
    Q_INVOKABLE QString meetingIdAt(int row) const;
    Q_INVOKABLE QString titleOf(const QString& meetingId) const;
    Q_INVOKABLE void addPerson(const QString& name);
    Q_INVOKABLE void removePerson(const QString& name);
    Q_INVOKABLE void clearFilters();
    // A személy állandó színindexe a chiphez (a peopleOptions sorrendje szerint).
    Q_INVOKABLE int personColorIndex(const QString& name) const;
    // Az átirat-szövegek előtöltése a kereséshez (a keresőmező fókuszakor).
    Q_INVOKABLE void warmUp();
    // A függő (késleltetett) lekérdezés azonnali lefuttatása.
    Q_INVOKABLE void refreshNow();

signals:
    void queryChanged();
    void countChanged();
    void resultChanged();          // lefutott egy lekérdezés (a találat-szöveg változhatott)
    void peopleOptionsChanged();
    void forceEmptyChanged();

private:
    void attach();
    void scheduleRefresh(int delayMs = 0);
    void onMeetingChanged(const QString& meetingId);
    void apply(const tanara::LibraryResult& result);
    tanara::LibraryResult runQuery() const;
    tanara::MeetingLibrary* library() const;

    QPointer<tanara::AppController> m_controller;
    QPointer<tanara::MeetingLibrary> m_library;     // amelyikre épp rá vagyunk kötve
    bool m_controllerInjected = false;
    tanara::LibraryQuery m_query;
    QVector<tanara::LibraryEntry> m_entries;
    int m_total = 0;
    bool m_forceEmpty = false;
    QTimer m_refreshTimer;
    QTimer m_dayTimer;      // a dátum-szekciók (ma / tegnap) napváltáskor is frissüljenek
};

} // namespace tanara_qml
