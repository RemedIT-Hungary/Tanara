#include "tanara/processing/summary/ComplexSummaryService.h"
#include "tanara/processing/summary/PromptLibrary.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QJsonParseError>
#include <QPointer>
#include <QRegularExpression>
#include <QUuid>
#include <QDebug>

namespace tanara::processing {

namespace {

// Permissive JSON extraction: strips an optional ```json ... ``` fence, and if it is still not an
// object, tries to cut out the first { ... } block. (The same behavior as SummaryService's
// extractJson; the two modules are intentionally independent.)
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

// The topic list from MARKDOWN: every `#` heading is a topic's TITLE, the following (non-heading)
// lines are the summary until the next heading. Permissive: prose that does not fit (e.g. leftover
// reasoning, an intro) is simply skipped, so one botched section does NOT sink everything (unlike JSON,
// where one missing `}` = total failure).
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
            title.remove(QLatin1Char('*'));   // stripping the **bold** title markup
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

// Fallback: if the model did give JSON after all (or a custom JSON prompt is saved), we extract the
// topics from that too. Empty if not parseable.
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

// A markdown action-item line -> ActionItem: "- text - Owner (deadline)". The " - Owner" and the
// "(deadline)" are optional; several dash variants (em dash, en dash, hyphen) are accepted as separator.
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

// One topic analysis from MARKDOWN: the intro paragraph = detail; the bullets of the Hungarian decisions
// section = decisions; those of the Hungarian action-items section = actionItems. Fault-tolerant: one
// truncated/botched line does not sink everything (unlike JSON).
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
        } else if (bm.hasMatch()) {   // Actions: only the bullets count
            const ActionItem ai = parseActionItemLine(t);
            if (!ai.text.isEmpty()) a.actionItems << ai;
        }
    }
    a.detail = detail.join(QLatin1Char(' ')).simplified();
    return a;
}

// Fallback: the old JSON analysis (a custom JSON prompt / if the model gives JSON anyway). ok=false if
// not parseable.
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

// Reduce from MARKDOWN: the intro paragraph = execSummary; the bullets of the Hungarian action-items section = actionItems.
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

// The built-in default of the reduce prompt lives in PromptLibrary ("reduce"); AppController may give a
// resolved (file-override) prompt with setReducePrompt().

// A context block at the start of the user prompt (if any).
QString contextBlock(const QString& contextNotes)
{
    if (contextNotes.trimmed().isEmpty()) return {};
    return QStringLiteral("Context / notes:\n") + contextNotes + QStringLiteral("\n\n");
}

} // namespace

ComplexSummaryService::ComplexSummaryService(providers::ILlmProvider* provider, QObject* parent)
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

    providers::LlmRequest req;
    req.model = model;
    req.stream = false;
    req.temperature = temperature;
    req.maxTokens = maxTokens > 0 ? maxTokens : 4000;
    req.messages.append({QStringLiteral("system"),
        systemPrompt.trimmed().isEmpty() ? defaultTopicPrompt() : systemPrompt});
    req.messages.append({QStringLiteral("user"),
        contextBlock(contextNotes) + QStringLiteral("----\n") + transcriptMd
            + languageReminder(m_language)});

    providers::LlmJob* job = m_provider->chat(req);
    if (!job) { emit failed(tr("A provider nem adott vissza jobot.")); return; }
    QPointer<ComplexSummaryService> self(this);
    connect(job, &providers::LlmJob::finished, this, [self, job](const QString& text) {
        job->deleteLater();
        if (!self) return;
        // Primarily markdown (`## title` + summary): fault-tolerant; if empty, a JSON fallback.
        QVector<SummaryTopic> topics = parseTopicsMarkdown(text);
        if (topics.isEmpty()) topics = parseTopicsJson(text);
        if (topics.isEmpty()) {
            qWarning().noquote() << "[ComplexSummary] topic parse EMPTY — raw response:\n"
                                 << text.left(2000);
            emit self->failed(tr(
                "Nem sikerült témát kinyerni a válaszból (sem `## cím` szakasz, sem JSON)."));
            return;
        }
        emit self->topicsReady(topics);
    });
    connect(job, &providers::LlmJob::failed, this, [self, job](const QString& e) {
        job->deleteLater();
        if (self) emit self->failed(e);
    });
}

void ComplexSummaryService::requestTopicAnalysis(const QString& transcriptMd, const SummaryTopic& topic,
                                                 const QString& contextNotes, const QString& systemPrompt,
                                                 const QString& model, double temperature, int maxTokens)
{
    if (!m_provider) { emit failed(tr("Nincs beállított LLM provider.")); return; }

    providers::LlmRequest req;
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

    providers::LlmJob* job = m_provider->chat(req);
    if (!job) { emit failed(tr("A provider nem adott vissza jobot.")); return; }
    QPointer<ComplexSummaryService> self(this);
    const SummaryTopic cap = topic;   // topicId/title are inherited into the result
    connect(job, &providers::LlmJob::finished, this, [self, job, cap](const QString& text) {
        job->deleteLater();
        if (!self) return;
        // Primarily markdown (summary + the Hungarian decisions / action-items headings); if empty, a JSON fallback.
        TopicAnalysis a = parseAnalysisMarkdown(text, cap);
        if (a.detail.isEmpty() && a.decisions.isEmpty() && a.actionItems.isEmpty()) {
            bool ok = false;
            const TopicAnalysis j = parseAnalysisJson(text, cap, &ok);
            if (ok) a = j;
        }
        if (a.detail.isEmpty() && a.decisions.isEmpty() && a.actionItems.isEmpty()) {
            qWarning().noquote() << "[ComplexSummary] analysis EMPTY (" << cap.title
                                 << ") — raw response:\n" << text.left(2000);
            emit self->failed(tr("Nem sikerült a téma-elemzést értelmezni („%1”).")
                                  .arg(cap.title));
            return;
        }
        emit self->topicAnalysisReady(a);
    });
    connect(job, &providers::LlmJob::failed, this, [self, job](const QString& e) {
        job->deleteLater();
        if (self) emit self->failed(e);
    });
}

void ComplexSummaryService::requestReduce(const QVector<TopicAnalysis>& analyses,
                                          const QString& contextNotes, const QString& model,
                                          double temperature, int maxTokens)
{
    if (!m_provider) { emit failed(tr("Nincs beállított LLM provider.")); return; }

    // A text summary of the per-topic analyses for the reduce's input.
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

    providers::LlmRequest req;
    req.model = model;
    req.stream = false;
    req.temperature = temperature;
    req.maxTokens = maxTokens > 0 ? maxTokens : 2000;
    req.messages.append({QStringLiteral("system"),
        m_reducePrompt.trimmed().isEmpty() ? promptBuiltin(QStringLiteral("reduce"))
                                           : m_reducePrompt});
    req.messages.append({QStringLiteral("user"), usr});

    providers::LlmJob* job = m_provider->chat(req);
    if (!job) { emit failed(tr("A provider nem adott vissza jobot.")); return; }
    QPointer<ComplexSummaryService> self(this);
    connect(job, &providers::LlmJob::finished, this, [self, job](const QString& text) {
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
            qWarning().noquote() << "[ComplexSummary] reduce EMPTY — raw response:\n" << text.left(2000);
            emit self->failed(tr("Nem sikerült az összegzést értelmezni."));
            return;
        }
        emit self->reduceReady(execSummary, items);
    });
    connect(job, &providers::LlmJob::failed, this, [self, job](const QString& e) {
        job->deleteLater();
        if (self) emit self->failed(e);
    });
}

} // namespace tanara::processing