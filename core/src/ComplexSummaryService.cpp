#include "tanara/ComplexSummaryService.h"
#include "tanara/PromptLibrary.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QJsonParseError>
#include <QPointer>
#include <QRegularExpression>
#include <QUuid>
#include <QDebug>

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

// A téma-lista MARKDOWNból: minden `#`-heading egy téma CÍME, az utána következő (nem-heading)
// sorok az összegzés a következő headingig. Megengedő: a nem illeszkedő prózát (pl. reasoning-
// maradék, bevezető) egyszerűen átugorja — egy elrontott szakasz NEM dönti be az egészet
// (szemben a JSON-nal, ahol egy hiányzó `}` = teljes bukás).
QVector<SummaryTopic> parseTopicsMarkdown(const QString& raw)
{
    static const QRegularExpression head(QStringLiteral("^\\s*#{1,6}\\s+(.+)$"));
    static const QRegularExpression numPrefix(QStringLiteral("^\\s*\\d+[.)]\\s*"));
    QVector<SummaryTopic> out;
    SummaryTopic cur; QStringList summaryLines; bool have = false;
    auto flush = [&]() {
        if (have && !cur.title.trimmed().isEmpty()) {
            cur.summary = summaryLines.join(QLatin1Char(' ')).simplified();
            cur.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            out.append(cur);
        }
        cur = SummaryTopic{}; summaryLines.clear(); have = false;
    };
    for (const QString& line : raw.split(QLatin1Char('\n'))) {
        const QRegularExpressionMatch m = head.match(line);
        if (m.hasMatch()) {
            flush();
            QString title = m.captured(1);
            title.remove(numPrefix);
            title.remove(QLatin1Char('*'));   // **félkövér** cím-jelölés levétele
            cur.title = title.trimmed();
            have = true;
        } else if (have) {
            const QString t = line.trimmed();
            if (!t.isEmpty()) summaryLines << t;
        }
    }
    flush();
    return out;
}

// Fallback: ha a modell mégis JSON-t adott (vagy egyedi JSON-prompt van mentve), abból is
// kinyerjük a témákat. Üres, ha nem értelmezhető.
QVector<SummaryTopic> parseTopicsJson(const QString& raw)
{
    QVector<SummaryTopic> out;
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(extractJson(raw), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) return out;
    for (const QJsonValue& v : doc.object().value(QStringLiteral("topics")).toArray()) {
        const QJsonObject o = v.toObject();
        SummaryTopic t;
        t.id      = QUuid::createUuid().toString(QUuid::WithoutBraces);
        t.title   = o.value(QStringLiteral("title")).toString().trimmed();
        t.summary = o.value(QStringLiteral("summary")).toString().trimmed();
        if (!t.title.isEmpty()) out.append(t);
    }
    return out;
}

// Egy markdown teendő-sor → ActionItem: "- szöveg — Felelős (határidő)". A „ — Felelős" és a
// „(határidő)" opcionális; több gondolatjel-variánst (—, –, -) elfogad elválasztónak.
ActionItem parseActionItemLine(QString line)
{
    static const QRegularExpression bullet(QStringLiteral("^\\s*(?:[-*•]|\\d+[.)])\\s+"));
    static const QRegularExpression dueRe(QStringLiteral("\\s*[（(]([^)）]*)[)）]\\s*$"));
    line.remove(bullet);
    ActionItem ai;
    const QRegularExpressionMatch dm = dueRe.match(line);
    if (dm.hasMatch()) { ai.due = dm.captured(1).trimmed(); line = line.left(dm.capturedStart()).trimmed(); }
    for (const QString& sep : {QStringLiteral(" — "), QStringLiteral(" – "), QStringLiteral(" - ")}) {
        const int i = line.lastIndexOf(sep);
        if (i >= 0) { ai.owner = line.mid(i + sep.size()).trimmed(); line = line.left(i).trimmed(); break; }
    }
    ai.text = line.trimmed();
    return ai;
}

enum class MdSection { Detail, Decisions, Open, Actions };

// Egy téma-elemzés MARKDOWNból: bevezető bekezdés = detail; `## Döntések` bulletjei =
// decisions; `## Teendők` bulletjei = actionItems. Hibatűrő: egy csonka/elrontott sor nem
// dönti be az egészet (szemben a JSON-nal).
TopicAnalysis parseAnalysisMarkdown(const QString& raw, const SummaryTopic& topic)
{
    static const QRegularExpression head(QStringLiteral("^\\s*#{1,6}\\s+(.+)$"));
    static const QRegularExpression bullet(QStringLiteral("^\\s*(?:[-*•]|\\d+[.)])\\s+(.+)$"));
    TopicAnalysis a; a.topicId = topic.id; a.title = topic.title;
    QStringList detail;
    MdSection sec = MdSection::Detail;
    for (const QString& line : raw.split(QLatin1Char('\n'))) {
        const QRegularExpressionMatch hm = head.match(line);
        if (hm.hasMatch()) {
            const QString h = hm.captured(1).toLower();
            if (h.contains(QStringLiteral("dönt")))        sec = MdSection::Decisions;
            else if (h.contains(QStringLiteral("nyitott")) || h.contains(QStringLiteral("open")))
                                                           sec = MdSection::Open;
            else if (h.contains(QStringLiteral("teendő")) || h.contains(QStringLiteral("feladat")))
                                                           sec = MdSection::Actions;
            else                                           sec = MdSection::Detail;
            continue;
        }
        const QString t = line.trimmed();
        if (t.isEmpty()) continue;
        if (sec == MdSection::Detail) { detail << t; continue; }
        const QRegularExpressionMatch bm = bullet.match(line);
        if (sec == MdSection::Decisions) {
            const QString d = bm.hasMatch() ? bm.captured(1).trimmed() : t;
            if (!d.isEmpty()) a.decisions << d;
        } else if (sec == MdSection::Open) {
            const QString q = bm.hasMatch() ? bm.captured(1).trimmed() : t;
            if (!q.isEmpty()) a.openQuestions << q;
        } else if (bm.hasMatch()) {   // Actions — csak a bulletek számítanak
            const ActionItem ai = parseActionItemLine(t);
            if (!ai.text.isEmpty()) a.actionItems << ai;
        }
    }
    a.detail = detail.join(QLatin1Char(' ')).simplified();
    return a;
}

// Fallback: régi JSON-elemzés (egyedi JSON-prompt / ha a modell mégis JSON-t ad). ok=false, ha
// nem értelmezhető.
TopicAnalysis parseAnalysisJson(const QString& raw, const SummaryTopic& topic, bool* ok)
{
    TopicAnalysis a; a.topicId = topic.id; a.title = topic.title;
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(extractJson(raw), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) { if (ok) *ok = false; return a; }
    const QJsonObject o = doc.object();
    a.detail      = o.value(QStringLiteral("detail")).toString().trimmed();
    a.decisions   = parseStringArray(o.value(QStringLiteral("decisions")).toArray());
    a.openQuestions = parseStringArray(o.value(QStringLiteral("openQuestions")).toArray());
    a.actionItems = parseActionItems(o.value(QStringLiteral("actionItems")).toArray());
    if (ok) *ok = true;
    return a;
}

// Reduce MARKDOWNból: bevezető bekezdés = execSummary; `## Teendők` bulletjei = actionItems.
void parseReduceMarkdown(const QString& raw, QString* execSummary, QVector<ActionItem>* items)
{
    static const QRegularExpression head(QStringLiteral("^\\s*#{1,6}\\s+(.+)$"));
    static const QRegularExpression bullet(QStringLiteral("^\\s*(?:[-*•]|\\d+[.)])\\s+(.+)$"));
    QStringList summary; bool inActions = false;
    for (const QString& line : raw.split(QLatin1Char('\n'))) {
        const QRegularExpressionMatch hm = head.match(line);
        if (hm.hasMatch()) {
            const QString h = hm.captured(1).toLower();
            inActions = h.contains(QStringLiteral("teendő")) || h.contains(QStringLiteral("feladat"));
            continue;
        }
        const QString t = line.trimmed();
        if (t.isEmpty()) continue;
        if (!inActions) { summary << t; continue; }
        if (bullet.match(line).hasMatch()) {
            const ActionItem ai = parseActionItemLine(t);
            if (!ai.text.isEmpty()) items->append(ai);
        }
    }
    *execSummary = summary.join(QLatin1Char(' ')).simplified();
}

// A reduce-prompt beépített defaultja a PromptLibrary-ben él ("reduce"); az AppController
// a setReducePrompt()-tal adhat feloldott (fájl-override-os) promptot.

// Egy context-blokk a user-prompt elejére (ha van).
QString contextBlock(const QString& contextNotes)
{
    if (contextNotes.trimmed().isEmpty()) return {};
    return QStringLiteral("Context / notes:\n") + contextNotes + QStringLiteral("\n\n");
}

} // namespace

ComplexSummaryService::ComplexSummaryService(ILlmProvider* provider, QObject* parent)
    : QObject(parent), m_provider(provider) {}

ComplexSummaryService::~ComplexSummaryService() = default;

QString ComplexSummaryService::defaultTopicPrompt()
{
    return promptBuiltin(QStringLiteral("topic"));
}

QString ComplexSummaryService::defaultAnalysisPrompt()
{
    return promptBuiltin(QStringLiteral("analysis"));
}

void ComplexSummaryService::requestTopics(const QString& transcriptMd, const QString& contextNotes,
                                          const QString& systemPrompt, const QString& model,
                                          double temperature, int maxTokens)
{
    if (!m_provider) { emit failed(tr("Nincs beállított LLM provider.")); return; }

    LlmRequest req;
    req.model = model;
    req.stream = false;
    req.temperature = temperature;
    req.maxTokens = maxTokens > 0 ? maxTokens : 4000;
    req.messages.append({QStringLiteral("system"),
        systemPrompt.trimmed().isEmpty() ? defaultTopicPrompt() : systemPrompt});
    req.messages.append({QStringLiteral("user"),
        contextBlock(contextNotes) + QStringLiteral("----\n") + transcriptMd
            + languageReminder(m_language)});

    LlmJob* job = m_provider->chat(req);
    if (!job) { emit failed(tr("A provider nem adott vissza jobot.")); return; }
    QPointer<ComplexSummaryService> self(this);
    connect(job, &LlmJob::finished, this, [self, job](const QString& text) {
        job->deleteLater();
        if (!self) return;
        // Elsődlegesen markdown (`## cím` + összegzés) — hibatűrő; ha üres, JSON-fallback.
        QVector<SummaryTopic> topics = parseTopicsMarkdown(text);
        if (topics.isEmpty()) topics = parseTopicsJson(text);
        if (topics.isEmpty()) {
            qWarning().noquote() << "[ComplexSummary] téma-parse ÜRES — nyers válasz:\n"
                                 << text.left(2000);
            emit self->failed(tr(
                "Nem sikerült témát kinyerni a válaszból (sem `## cím` szakasz, sem JSON)."));
            return;
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
    if (!m_provider) { emit failed(tr("Nincs beállított LLM provider.")); return; }

    LlmRequest req;
    req.model = model;
    req.stream = false;
    req.temperature = temperature;
    req.maxTokens = maxTokens > 0 ? maxTokens : 4000;
    req.messages.append({QStringLiteral("system"),
        systemPrompt.trimmed().isEmpty() ? defaultAnalysisPrompt() : systemPrompt});
    QString usr = contextBlock(contextNotes);
    usr += QStringLiteral("TOPIC TO ANALYSE: %1\n").arg(topic.title);
    if (!topic.summary.isEmpty())
        usr += QStringLiteral("(Short description of the topic: %1)\n").arg(topic.summary);
    usr += QStringLiteral("\n----\nFull transcript:\n") + transcriptMd;
    usr += languageReminder(m_language);
    req.messages.append({QStringLiteral("user"), usr});

    LlmJob* job = m_provider->chat(req);
    if (!job) { emit failed(tr("A provider nem adott vissza jobot.")); return; }
    QPointer<ComplexSummaryService> self(this);
    const SummaryTopic cap = topic;   // a topicId/title az eredménybe öröklődik
    connect(job, &LlmJob::finished, this, [self, job, cap](const QString& text) {
        job->deleteLater();
        if (!self) return;
        // Elsődlegesen markdown (összegző + `## Döntések` / `## Teendők`); ha üres, JSON-fallback.
        TopicAnalysis a = parseAnalysisMarkdown(text, cap);
        if (a.detail.isEmpty() && a.decisions.isEmpty() && a.actionItems.isEmpty()) {
            bool ok = false;
            const TopicAnalysis j = parseAnalysisJson(text, cap, &ok);
            if (ok) a = j;
        }
        if (a.detail.isEmpty() && a.decisions.isEmpty() && a.actionItems.isEmpty()) {
            qWarning().noquote() << "[ComplexSummary] elemzés ÜRES (" << cap.title
                                 << ") — nyers válasz:\n" << text.left(2000);
            emit self->failed(tr("Nem sikerült a téma-elemzést értelmezni („%1”).")
                                  .arg(cap.title));
            return;
        }
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
    if (!m_provider) { emit failed(tr("Nincs beállított LLM provider.")); return; }

    // A per-téma elemzések szöveges összefoglalása a reduce bemenetéhez.
    QString usr = contextBlock(contextNotes);
    usr += QStringLiteral("Analyses per topic:\n\n");
    for (const TopicAnalysis& a : analyses) {
        usr += QStringLiteral("## %1\n%2\n").arg(a.title, a.detail);
        if (!a.decisions.isEmpty())
            usr += QStringLiteral("Decisions: ") + a.decisions.join(QStringLiteral("; ")) + QStringLiteral("\n");
        if (!a.openQuestions.isEmpty())
            usr += QStringLiteral("Open questions: ") + a.openQuestions.join(QStringLiteral("; ")) + QStringLiteral("\n");
        for (const ActionItem& ai : a.actionItems) {
            usr += QStringLiteral("Task: %1").arg(ai.text);
            if (!ai.owner.isEmpty()) usr += QStringLiteral(" — %1").arg(ai.owner);
            if (!ai.due.isEmpty())   usr += QStringLiteral(" (%1)").arg(ai.due);
            usr += QStringLiteral("\n");
        }
        usr += QStringLiteral("\n");
    }
    usr += languageReminder(m_language);

    LlmRequest req;
    req.model = model;
    req.stream = false;
    req.temperature = temperature;
    req.maxTokens = maxTokens > 0 ? maxTokens : 2000;
    req.messages.append({QStringLiteral("system"),
        m_reducePrompt.trimmed().isEmpty() ? promptBuiltin(QStringLiteral("reduce"))
                                           : m_reducePrompt});
    req.messages.append({QStringLiteral("user"), usr});

    LlmJob* job = m_provider->chat(req);
    if (!job) { emit failed(tr("A provider nem adott vissza jobot.")); return; }
    QPointer<ComplexSummaryService> self(this);
    connect(job, &LlmJob::finished, this, [self, job](const QString& text) {
        job->deleteLater();
        if (!self) return;
        QString execSummary; QVector<ActionItem> items;
        parseReduceMarkdown(text, &execSummary, &items);
        if (execSummary.isEmpty() && items.isEmpty()) {   // JSON-fallback
            QJsonParseError perr{};
            const QJsonDocument doc = QJsonDocument::fromJson(extractJson(text), &perr);
            if (perr.error == QJsonParseError::NoError && doc.isObject()) {
                const QJsonObject o = doc.object();
                execSummary = o.value(QStringLiteral("execSummary")).toString().trimmed();
                items = parseActionItems(o.value(QStringLiteral("actionItems")).toArray());
            }
        }
        if (execSummary.isEmpty() && items.isEmpty()) {
            qWarning().noquote() << "[ComplexSummary] reduce ÜRES — nyers válasz:\n" << text.left(2000);
            emit self->failed(tr("Nem sikerült az összegzést értelmezni."));
            return;
        }
        emit self->reduceReady(execSummary, items);
    });
    connect(job, &LlmJob::failed, this, [self, job](const QString& e) {
        job->deleteLater();
        if (self) emit self->failed(e);
    });
}

} // namespace tanara
