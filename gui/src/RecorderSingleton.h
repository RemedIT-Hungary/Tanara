#pragma once
//
// RecorderSingleton — EGYETLEN felvevő-példány gépenként.
//
// Aki felvevőt mutat (az elemző a lebegő felvevőjével, vagy a `tanara --record`
// folyamat), az figyel egy lokális socketen. Minden újabb `tanara --record …` hívás
// (figyelő, tálca-katt, notification-gomb) ELŐBB megpróbálja átadni az argumentumait
// a futó példánynak — ha sikerül, kilép. Így nem nyílik több felvevő, és a tálcáról
// mindig az jön elő, ami épp felvesz.
//
#include <QObject>
#include <QString>
#include <QStringList>

class QLocalServer;

namespace tanara_gui {

class RecorderSingleton : public QObject {
    Q_OBJECT
public:
    // "tanara-recorder-<uid>"; TANARA_HOME mellett "-<mappa-hash>" utótaggal (elszigetelés).
    static QString serverName();

    // Átadja az args-ot egy futó példánynak. true = átadva (a hívó kiléphet).
    static bool forwardToExisting(const QStringList& args, int timeoutMs = 1500);

    explicit RecorderSingleton(QObject* parent = nullptr);
    ~RecorderSingleton() override;

    // false, ha a nevet másik ÉLŐ példány fogja (stale socket-fájlt magától eltakarít).
    bool listen();
    bool isListening() const;
    void close();

signals:
    void requestReceived(QStringList args);   // a továbbított `--record …` argumentumok

private:
    QLocalServer* m_server = nullptr;
};

// A `--record` argumentumok közös értelmezése (main.cpp felvevő-mód + továbbított kérés).
struct RecorderArgs {
    QString title, context;
    QString app;           // --app: az észlelt hívás-app (az automatikus névhez)
    QList<int> deviceIdx;
    bool noStart = false;
    bool stop = false;     // --stop: a futó felvétel leállítása (a tálca-menü küldi)
};
RecorderArgs parseRecorderArgs(const QStringList& args);

} // namespace tanara_gui
