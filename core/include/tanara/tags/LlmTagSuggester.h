#pragma once
//
// LlmTagSuggester — címkejavaslat a nyelvi modelltől, az összefoglaló elkészülte után.
//
// Egy chat-hívás a "tags" prompttal (PromptLibrary): a felhasználói üzenet az összefoglaló
// szövege + a jelölt címkék számozott listája (a hasonlóság-alapú javaslatok és a 20
// leggyakoribb címke, mindegyik egysoros profillal: jellemző résztvevők, kifejezések, két
// példa-cím). A modell SIMA SOROKKAT ad (nem JSON-t — a json_schema lokálisan megbízhatatlan):
//   pick: 3, 7
//   new: Név1; Név2
// A feldolgozás engedékeny (kis-/nagybetű, felsorolásjel, idézőjel, „none”). A pick meglévő
// címkéket ad (source = Llm), a new legfeljebb 2 új név-ötletet (isNew); ha egy új név
// nearDuplicate egy meglévőnek, a meglévőt javasolja helyette. A gondolkodás ki van kapcsolva
// (a ProviderConfig::reasoning szerint, mint az összefoglalónál).
//
#include "tanara/tags/TagTypes.h"

#include <QObject>
#include <QPointer>

namespace tanara {

class ILlmProvider;
class LlmJob;
class TagService;

struct LlmTagCandidate {
    QString tagId;
    QString name;
    QString profileLine;
};

class LlmTagSuggester : public QObject {
    Q_OBJECT
public:
    static constexpr int kTopUsed = 20;
    static constexpr int kMaxNew = 2;
    static constexpr int kSummaryChars = 8000;

    explicit LlmTagSuggester(TagService* tags, QObject* parent = nullptr);
    ~LlmTagSuggester() override;

    // ---- tiszta segédek ----
    // A jelöltek: előbb a javaslatok (meglévő címkék), aztán a leggyakoribbak, ismétlés nélkül.
    QVector<LlmTagCandidate> candidates(const QVector<TagSuggestion>& suggested, int topUsed = kTopUsed) const;
    static QString buildUserMessage(const QString& summary, const QVector<LlmTagCandidate>& candidates);
    struct Parsed {
        QVector<int> picks;      // 1-alapú sorszámok, a válasz sorrendjében
        QStringList  newNames;
    };
    static Parsed parse(const QString& reply);
    // A feldolgozott válasz → javaslatok (a meetingen lévő / elutasított kimarad).
    QVector<TagSuggestion> toSuggestions(const Parsed& parsed, const QVector<LlmTagCandidate>& candidates,
                                         const QString& meetingId) const;

    // Egy futás: a provider chat-hívása a rendszer-prompttal. finished / failed jel; a provider
    // a hívóé (a futás alatt élnie kell).
    void start(ILlmProvider* provider, const QString& model, const QString& systemPrompt,
               const QString& meetingId, const QString& summary,
               const QVector<TagSuggestion>& suggested);
    void cancel();
    bool isRunning() const { return !m_job.isNull(); }

signals:
    void finished(QString meetingId, QVector<tanara::TagSuggestion> suggestions);
    void failed(QString meetingId, QString error);

private:
    TagService* m_tags;
    QPointer<LlmJob> m_job;
};

} // namespace tanara
