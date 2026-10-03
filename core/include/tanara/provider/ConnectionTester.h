#pragma once
//
// ConnectionTester — a Beállítások „Kapcsolat tesztelése” gombjának háttere.
//
// MIT ELLENŐRIZ (és mit nem): egyetlen GET kérést küld a provider leírójában megadott
// próba-végpontra (ConnectionProbe: jellemzően GET <baseUrl>/models), a megadott API-kulccsal.
//  - elérhető-e a cím (DNS, TCP, TLS), és mennyi idő alatt válaszol;
//  - elfogadja-e a szolgáltató a kulcsot (401 / 403 → nem);
//  - API-szerű választ ad-e (JSON modell-lista), és szerepel-e benne a beállított modell.
// NEM indít átírást / összefoglalót, nem költ, és nem bizonyítja, hogy a modell tényleg
// betöltődik vagy hogy a fióknak van kerete — azt csak egy valódi futás mutatja meg.
//
// Headless (core): QtNetwork. A provider-specifikus tudás a leíróban van (ProviderDescriptor::probe).
//
#include "tanara/Types.h"
#include "tanara/provider/ProviderDescriptor.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QStringList>

class QNetworkAccessManager;
class QNetworkReply;

namespace tanara {

struct ConnectionTestResult {
    enum class Status {
        Ok,            // elérhető, a kulcs rendben (a modell-figyelmeztetés ettől még lehet)
        BadConfig,     // hiányzó / hibás cím — kérés sem ment ki
        Unreachable,   // nem jött létre kapcsolat (elutasítva, nincs ilyen gép, időtúllépés, TLS)
        AuthFailed,    // 401 / 403
        NotAnApi,      // a cím válaszol, de nem a várt API (404, nem JSON)
        ServerError,   // 5xx / 429 / egyéb HTTP-hiba
    };
    Status status = Status::BadConfig;
    bool ok() const { return status == Status::Ok; }
    int httpStatus = 0;          // 0 = nem érkezett HTTP-válasz
    qint64 latencyMs = -1;       // a kérés elküldésétől a válaszig; -1 = nincs válasz
    QString code;                // technikai kód: ECONNREFUSED, ENOTFOUND, ETIMEDOUT, ETLS, HTTP 401 …
    QString message;             // emberi mondat a UI nyelvén (hiba esetén)
    QString warning;             // siker mellett: pl. a beállított modell nincs a listában
    QStringList models;          // ha a próba modell-listát ad (rendezve)
};

// A cím a saját gépre / helyi hálózatra mutat-e (localhost, 127.0.0.0/8, ::1, RFC 1918,
// *.local) — ilyenkor az API-kulcs jellemzően nem kell.
bool isLocalEndpoint(const QString& baseUrl);

class ConnectionTester : public QObject {
    Q_OBJECT
public:
    explicit ConnectionTester(QObject* parent = nullptr);
    ~ConnectionTester() override;

    void setTimeoutMs(int ms) { m_timeoutMs = ms; }
    int timeoutMs() const { return m_timeoutMs; }

    // Elindít egy próbát; az eredmény a finished(id, …) jelben jön (hibás beállításnál is,
    // a következő eseményhurok-körben). cfg.apiKey a futásidejű kulcs (üres → fejléc nélkül).
    int test(const ProviderDescriptor& descriptor, const ProviderConfig& cfg);
    // Egy futó próba eldobása (nem jön rá finished).
    void cancel(int id);
    void cancelAll();
    bool running(int id) const { return m_replies.contains(id); }

    // Egy HTTP-státusz besorolása + emberi mondata és technikai kódja (tesztelhető).
    static void describeHttp(int httpStatus, bool hadKey, bool local, ConnectionTestResult* out);

signals:
    void finished(int id, const tanara::ConnectionTestResult& result);

private:
    void onFinished(int id, QNetworkReply* reply, qint64 startedMs, const ProviderDescriptor& d,
                    const ProviderConfig& cfg);

    QNetworkAccessManager* m_nam = nullptr;
    QHash<int, QPointer<QNetworkReply>> m_replies;
    int m_nextId = 1;
    int m_timeoutMs = 8000;
};

} // namespace tanara

Q_DECLARE_METATYPE(tanara::ConnectionTestResult)
