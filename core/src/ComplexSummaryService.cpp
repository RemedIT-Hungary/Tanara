#include "tanara/ComplexSummaryService.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QJsonParseError>
#include <QPointer>
#include <QUuid>

namespace tanara {

namespace {

// Megengedő JSON-kinyerés: leveszi az esetleges ```json ... ``` kerítést, és ha még
// mindig nem objektum, megpróbálja az első { ... } blokkot kivágni. (A SummaryService
// extractJson-jával azonos viselkedés; a két modul szándékosan független.)
QByteArray extractJson(const QString& raw)
{
    QString s = raw.trimmed();
    if (s.startsWith(QStringLiteral("```"))) {
        const int nl = s.indexOf(QLatin1Char('\n'));
        if (nl >= 0) s = s.mid(nl + 1);
        const int fence = s.lastIndexOf(QStringLiteral("```"));
        if (fence >= 0) s = s.left(fence);
        s = s.trimmed();
    }
    if (!s.startsWith(QLatin1Char('{'))) {
        const int start = s.indexOf(QLatin1Char('{'));
        const int end = s.lastIndexOf(QLatin1Char('}'));
        if (start >= 0 && end > start) s = s.mid(start, end - start + 1);
    }
    return s.toUtf8();
}

QVector<ActionItem> parseActionItems(const QJsonArray& items)
{
    QVector<ActionItem> out;
    for (const QJsonValue& v : items) {
        const QJsonObject o = v.toObject();
        ActionItem ai;
        ai.text  = o.value(QStringLiteral("text")).toString();
        ai.owner = o.value(QStringLiteral("owner")).toString();
        ai.due   = o.value(QStringLiteral("due")).toString();
        if (!ai.text.trimmed().isEmpty())
            out.append(ai);
    }
    return out;
}

QStringList parseStringArray(const QJsonArray& arr)
{
    QStringList out;
    for (const QJsonValue& v : arr) {
        const QString s = v.toString().trimmed();
        if (!s.isEmpty()) out.append(s);
    }
    return out;
}

// Egy reduce-prompt belső, fix (nem user-szerkeszthető): a per-téma elemzésekből globális
// összefoglalót + összevont teendőket kér.
QString reduceSystemPrompt()
{
    return QStringLiteral(
        "Te egy precíz magyar nyelvű jegyzetelő vagy. A kapott témánkénti elemzésekből készíts: "
        "(1) egy rövid, 2-4 mondatos GLOBÁLIS vezetői összefoglalót az egész beszélgetésről, és "
        "(2) egy ÖSSZEVONT, duplikátum-mentes teendő-listát.\n"
        "KIZÁRÓLAG egyetlen érvényes JSON objektumot adj vissza, kódkerítés (```) nélkül: "
        "{\"execSummary\": string, \"actionItems\": [{\"text\": string, \"owner\": string, "
        "\"due\": string}]}. Minden MAGYARUL. Ne találj ki új teendőt — csak a megadottakat vond "
        "össze és deduplikáld.\n");
}

// Egy context-blokk a user-prompt elejére (ha van).
QString contextBlock(const QString& contextNotes)
{
    if (contextNotes.trimmed().isEmpty()) return {};
    return QStringLiteral("Kontextus / jegyzetek:\n") + contextNotes + QStringLiteral("\n\n");
}

} // namespace

ComplexSummaryService::ComplexSummaryService(ILlmProvider* provider, QObject* parent)
    : QObject(parent), m_provider(provider) {}

ComplexSummaryService::~ComplexSummaryService() = default;

QString ComplexSummaryService::defaultTopicPrompt()
{
    return QStringLiteral(
        "Te egy magyar nyelvű elemző vagy. A kapott beszéd-átiratból azonosítsd a KÜLÖNÁLLÓ "
        "TÉMÁKAT (témakörök, amelyekről ténylegesen szó volt).\n"
        "Szabályok:\n"
        "1. KIZÁRÓLAG egyetlen érvényes JSON objektumot adj vissza, kódkerítés (```) nélkül.\n"
        "2. Szerkezet pontosan: {\"topics\": [{\"title\": string, \"summary\": string}]}. "
        "A title rövid téma-cím; a summary 1-2 mondatos összegzés a témáról.\n"
        "3. A témák száma legyen ARÁNYOS a tartalommal: kötetlen/információ-szegény "
        "beszélgetésnél kevés téma (akár 1), információ-intenzív megbeszélésnél több. Ne darabolj "
        "túl, és NE találj ki nem létező témát.\n"
        "4. Minden mezőt MAGYARUL tölts ki.\n");
}

QString ComplexSummaryService::defaultAnalysisPrompt()
{
    return QStringLiteral(
        "Te egy precíz magyar nyelvű jegyzetelő vagy. A kapott TELJES átiratból KIZÁRÓLAG a "
        "megadott TÉMÁRA vonatkozó részeket elemezd.\n"
        "Szabályok:\n"
        "1. KIZÁRÓLAG egyetlen érvényes JSON objektumot adj vissza, kódkerítés nélkül.\n"
        "2. Szerkezet pontosan: {\"detail\": string, \"decisions\": [string], \"actionItems\": "
        "[{\"text\": string, \"owner\": string, \"due\": string}]}. A detail a témára vonatkozó "
        "összegzés; a decisions a témához tartozó döntések; az actionItems a teendők "
        "(owner=felelős, due=határidő, ha nincs adat üres string).\n"
        "3. NE TALÁLJ KI semmit. Ha a témához nincs valódi döntés vagy teendő, hagyd ÜRESEN a "
        "megfelelő tömböt. Csak a megadott témára fókuszálj, a többi témát hagyd figyelmen kívül.\n"
        "4. Minden mezőt MAGYARUL tölts ki.\n");
}

void ComplexSummaryService::requestTopics(const QString& transcriptMd, const QString& contextNotes,
                                          const QString& systemPrompt, const QString& model,
                                          double temperature, int maxTokens)
{
    if (!m_provider) { emit failed(QStringLiteral("Nincs beállított LLM provider.")); return; }

    LlmRequest req;
    req.model = model;
    req.stream = false;
    req.temperature = temperature;
    req.maxTokens = maxTokens > 0 ? maxTokens : 4000;
    req.messages.append({QStringLiteral("system"),
        systemPrompt.trimmed().isEmpty() ? defaultTopicPrompt() : systemPrompt});
    req.messages.append({QStringLiteral("user"),
        contextBlock(contextNotes) + QStringLiteral("----\n") + transcriptMd});

    LlmJob* job = m_provider->chat(req);
    if (!job) { emit failed(QStringLiteral("A provider nem adott vissza jobot.")); return; }
    QPointer<ComplexSummaryService> self(this);
    connect(job, &LlmJob::finished, this, [self, job](const QString& text) {
        job->deleteLater();
        if (!self) return;
        QJsonParseError perr{};
        const QJsonDocument doc = QJsonDocument::fromJson(extractJson(text), &perr);
        if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
            emit self->failed(QStringLiteral("Nem sikerült a témákat JSON-ként értelmezni: %1")
                                  .arg(perr.errorString()));
            return;
        }
        QVector<SummaryTopic> topics;
        for (const QJsonValue& v : doc.object().value(QStringLiteral("topics")).toArray()) {
            const QJsonObject o = v.toObject();
            SummaryTopic t;
            t.id      = QUuid::createUuid().toString(QUuid::WithoutBraces);
            t.title   = o.value(QStringLiteral("title")).toString().trimmed();
            t.summary = o.value(QStringLiteral("summary")).toString().trimmed();
            if (!t.title.isEmpty()) topics.append(t);
        }
        emit self->topicsReady(topics);
    });
    connect(job, &LlmJob::failed, this, [self, job](const QString& e) {
        job->deleteLater();
        if (self) emit self->failed(e);
    });
}

void ComplexSummaryService::requestTopicAnalysis(const QString& transcriptMd, const SummaryTopic& topic,
                                                 const QString& contextNotes, const QString& systemPrompt,
                                                 const QString& model, double temperature, int maxTokens)
{
    if (!m_provider) { emit failed(QStringLiteral("Nincs beállított LLM provider.")); return; }

    LlmRequest req;
    req.model = model;
    req.stream = false;
    req.temperature = temperature;
    req.maxTokens = maxTokens > 0 ? maxTokens : 4000;
    req.messages.append({QStringLiteral("system"),
        systemPrompt.trimmed().isEmpty() ? defaultAnalysisPrompt() : systemPrompt});
    QString usr = contextBlock(contextNotes);
    usr += QStringLiteral("ELEMZENDŐ TÉMA: %1\n").arg(topic.title);
    if (!topic.summary.isEmpty())
        usr += QStringLiteral("(A téma rövid leírása: %1)\n").arg(topic.summary);
    usr += QStringLiteral("\n----\nTeljes átirat:\n") + transcriptMd;
    req.messages.append({QStringLiteral("user"), usr});

    LlmJob* job = m_provider->chat(req);
    if (!job) { emit failed(QStringLiteral("A provider nem adott vissza jobot.")); return; }
    QPointer<ComplexSummaryService> self(this);
    const SummaryTopic cap = topic;   // a topicId/title az eredménybe öröklődik
    connect(job, &LlmJob::finished, this, [self, job, cap](const QString& text) {
        job->deleteLater();
        if (!self) return;
        QJsonParseError perr{};
        const QJsonDocument doc = QJsonDocument::fromJson(extractJson(text), &perr);
        if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
            emit self->failed(QStringLiteral("Nem sikerült a téma-elemzést értelmezni („%1”): %2")
                                  .arg(cap.title, perr.errorString()));
            return;
        }
        const QJsonObject o = doc.object();
        TopicAnalysis a;
        a.topicId     = cap.id;
        a.title       = cap.title;
        a.detail      = o.value(QStringLiteral("detail")).toString().trimmed();
        a.decisions   = parseStringArray(o.value(QStringLiteral("decisions")).toArray());
        a.actionItems = parseActionItems(o.value(QStringLiteral("actionItems")).toArray());
        emit self->topicAnalysisReady(a);
    });
    connect(job, &LlmJob::failed, this, [self, job](const QString& e) {
        job->deleteLater();
        if (self) emit self->failed(e);
    });
}

void ComplexSummaryService::requestReduce(const QVector<TopicAnalysis>& analyses,
                                          const QString& contextNotes, const QString& model,
                                          double temperature, int maxTokens)
{
    if (!m_provider) { emit failed(QStringLiteral("Nincs beállított LLM provider.")); return; }

    // A per-téma elemzések szöveges összefoglalása a reduce bemenetéhez.
    QString usr = contextBlock(contextNotes);
    usr += QStringLiteral("Témánkénti elemzések:\n\n");
    for (const TopicAnalysis& a : analyses) {
        usr += QStringLiteral("## %1\n%2\n").arg(a.title, a.detail);
        if (!a.decisions.isEmpty())
            usr += QStringLiteral("Döntések: ") + a.decisions.join(QStringLiteral("; ")) + QStringLiteral("\n");
        for (const ActionItem& ai : a.actionItems) {
            usr += QStringLiteral("Teendő: %1").arg(ai.text);
            if (!ai.owner.isEmpty()) usr += QStringLiteral(" — %1").arg(ai.owner);
            if (!ai.due.isEmpty())   usr += QStringLiteral(" (%1)").arg(ai.due);
            usr += QStringLiteral("\n");
        }
        usr += QStringLiteral("\n");
    }

    LlmRequest req;
    req.model = model;
    req.stream = false;
    req.temperature = temperature;
    req.maxTokens = maxTokens > 0 ? maxTokens : 2000;
    req.messages.append({QStringLiteral("system"), reduceSystemPrompt()});
    req.messages.append({QStringLiteral("user"), usr});

    LlmJob* job = m_provider->chat(req);
    if (!job) { emit failed(QStringLiteral("A provider nem adott vissza jobot.")); return; }
    QPointer<ComplexSummaryService> self(this);
    connect(job, &LlmJob::finished, this, [self, job](const QString& text) {
        job->deleteLater();
        if (!self) return;
        QJsonParseError perr{};
        const QJsonDocument doc = QJsonDocument::fromJson(extractJson(text), &perr);
        if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
            emit self->failed(QStringLiteral("Nem sikerült az összegzést értelmezni: %1")
                                  .arg(perr.errorString()));
            return;
        }
        const QJsonObject o = doc.object();
        emit self->reduceReady(o.value(QStringLiteral("execSummary")).toString().trimmed(),
                               parseActionItems(o.value(QStringLiteral("actionItems")).toArray()));
    });
    connect(job, &LlmJob::failed, this, [self, job](const QString& e) {
        job->deleteLater();
        if (self) emit self->failed(e);
    });
}

} // namespace tanara
