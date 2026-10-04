#pragma once
//
// A beállított LLM-szerver képességei és a modell előkészítése (LM Studio natív API).
//
//  - LlmServerProbe: GET <gyökér>/api/v1/models (csak olvas). Ha válaszol, a szerver LM Studio:
//    a beállított modell betöltött példányai (kontextus, párhuzamosság) és a modell maximuma.
//    Más szervernél (llama.cpp, Ollama, vLLM, felhő) az eredmény „ismeretlen”. A választ
//    rövid ideig (kCacheMs) gyorsítótárazzuk címenként; a hívás sosem blokkol (aszinkron jel).
//  - LlmModelPreparer: egy LLM-feladat ELŐTT gondoskodik róla, hogy a modell legalább a
//    feladathoz szükséges kontextussal és parallel = 1-gyel legyen betöltve (a parallel > 1
//    ezen a GPU-n már összeomlást okozott). Ha a betöltött példány megfelel, nem nyúl hozzá;
//    ha nincs betöltve, betölti; ha kicsi / párhuzamos, kiveszi és újratölti — CSAK a
//    beállított modell példányait, és csak ha a Tanara épp nem futtat rajta más kérést.
//    Feladatonként legfeljebb EGY betöltési kísérlet; a kudarc érthető hibát ad.
//
// A Tanara Cloud útvonal ezeket nem használja (ott nincs natív API).
//
#include "tanara/Types.h"
#include "tanara/jobs/JobTypes.h"
#include "tanara/llm/LlmContext.h"

#include <QObject>
#include <QPointer>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

namespace tanara {

class LlmServerProbe : public QObject {
    Q_OBJECT
public:
    static constexpr int kCacheMs = 10000;

    explicit LlmServerProbe(QObject* parent = nullptr);
    ~LlmServerProbe() override;

    void setTimeoutMs(int ms) { m_timeoutMs = ms; }
    // Lekérdezés; az eredmény a finished jelben jön (gyorsítótár-találatnál is aszinkron).
    void probe(const ProviderConfig& cfg, bool useCache = true);
    void cancel();

    // A gyorsítótár elvetése egy címre (üres → mind). Betöltés / kivétel után kötelező.
    static void invalidateCache(const QString& baseUrl = QString());

signals:
    void finished(const tanara::llmctx::LlmServerInfo& info);

private:
    QNetworkAccessManager* m_nam = nullptr;
    QPointer<QNetworkReply> m_reply;
    int m_timeoutMs = 3000;
    quint64 m_generation = 0;
};

class LlmModelPreparer : public QObject {
    Q_OBJECT
public:
    explicit LlmModelPreparer(const ProviderConfig& cfg, QObject* parent = nullptr);
    ~LlmModelPreparer() override;

    // Igaz, ha a Tanara épp egy MÁSIK LLM-kérést futtat (ilyenkor nem veszünk ki modellt).
    void setBusyCheck(std::function<bool()> busy) { m_busy = std::move(busy); }
    void setProbeTimeoutMs(int ms) { m_probeTimeoutMs = ms; }
    void setLoadTimeoutMs(int ms) { m_loadTimeoutMs = ms; }

    // need: a feladat becsült kontextus-igénye; floor: a kért minimum (0 = nincs).
    void start(int need, int floor = 0);
    // A futó lekérdezés / kivétel / betöltés eldobása; utána semmilyen jel nem jön.
    void cancel();

    const llmctx::LlmServerInfo& serverInfo() const { return m_info; }
    const llmctx::PreloadDecision& decision() const { return m_decision; }
    // A modell kontextusa a ready után (-1: ismeretlen — pl. nem LM Studio).
    int loadedContext() const { return m_loadedContext; }
    bool didLoad() const { return m_didLoad; }

signals:
    // Betöltés / újratöltés kezdődik (a feladat „Modell betöltése…” szakasza).
    void loadingStarted(int contextLength);
    // A feladat indulhat.
    void ready();
    // Az előkészítés elbukott (a kind-ot a hívó állítja).
    void failed(const tanara::JobError& error);

private:
    void onProbed(const llmctx::LlmServerInfo& info);
    void unloadNext();
    void load();
    QNetworkReply* post(const QString& path, const QByteArray& json, int timeoutMs);
    void fail(const QString& message, const QString& detail, const QString& hint);

    ProviderConfig m_cfg;
    QNetworkAccessManager* m_nam = nullptr;
    LlmServerProbe* m_probe = nullptr;
    QPointer<QNetworkReply> m_reply;
    std::function<bool()> m_busy;
    int m_need = 0;
    int m_floor = 0;
    int m_probeTimeoutMs = 3000;
    int m_loadTimeoutMs = 10 * 60 * 1000;
    llmctx::LlmServerInfo m_info;
    llmctx::PreloadDecision m_decision;
    QStringList m_toUnload;
    int m_loadedContext = -1;
    bool m_didLoad = false;
    bool m_cancelled = false;
    bool m_started = false;
};

} // namespace tanara

Q_DECLARE_METATYPE(tanara::llmctx::LlmServerInfo)
