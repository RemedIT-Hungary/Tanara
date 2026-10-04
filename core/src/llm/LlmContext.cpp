#include "tanara/llm/LlmContext.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace tanara {
namespace llmctx {

int estimateTokens(qint64 chars)
{
    if (chars <= 0) return 0;
    return int(std::ceil(double(chars) / kCharsPerToken));
}

int callContextNeed(qint64 inputChars, int outputTokens)
{
    const double input = double(estimateTokens(inputChars) + kTemplateOverheadTokens) * kSafetyFactor;
    return int(std::ceil(input)) + qMax(0, outputTokens);
}

int partBudgetChars(int contextTokens, qint64 promptChars, int outputTokens)
{
    if (contextTokens <= 0) return -1;
    // ceil((est(prompt + part) + overhead) * f) + out <= ctx  →  est(part) <= (ctx - out)/f - overhead - est(prompt)
    const double room = double(contextTokens - qMax(0, outputTokens)) / kSafetyFactor
                      - kTemplateOverheadTokens - estimateTokens(promptChars);
    const int tokens = int(std::floor(room));
    if (tokens < kMinPartTokens) return -1;
    return int(std::floor(tokens * kCharsPerToken));
}

int typicalSummaryPartNeed()
{
    // ~15 perc magyar beszéd ≈ 13–15 ezer karakter; a jegyzet-prompt + fejléc ≈ 3–4 ezer.
    return callContextNeed(18000, 3000);
}

QVector<int> contextSteps()
{
    return {8192, 16384, 20480, 24576, 32768, 49152, 65536, 98304, 131072, 196608, 262144};
}

int contextStepFor(int need, int maxContext)
{
    int step = 0;
    for (int s : contextSteps())
        if (s >= need) { step = s; break; }
    if (step == 0) step = ((qMax(need, 1) + 1023) / 1024) * 1024;
    if (maxContext > 0) step = qMin(step, maxContext);
    return step;
}

// ---- kontextus-túllépés felismerése ------------------------------------------------------

namespace {

bool isOverflowCode(const QString& code)
{
    const QString c = code.toLower();
    return c == QLatin1String("exceed_context_size_error")
        || c == QLatin1String("context_length_exceeded")
        || c == QLatin1String("context_window_exceeded")
        || c == QLatin1String("context_length_exceeded_error");
}

bool textMentionsOverflow(const QString& text)
{
    static const QRegularExpression re(QStringLiteral(
        "exceed_context_size_error|context_length_exceeded|maximum context length|"
        "exceeds the available context size|exceeds the context (?:size|length|window)|"
        "context (?:length|window|size) (?:exceeded|is too small)|prompt is too long|"
        "too many tokens in the prompt"),
        QRegularExpression::CaseInsensitiveOption);
    return re.match(text).hasMatch();
}

// A szövegből a két szám (llama.cpp, OpenAI, vLLM, Anthropic alakok).
void numbersFromText(const QString& text, ContextOverflow* o)
{
    static const QRegularExpression llama(QStringLiteral(
        "request \\((\\d+) tokens\\) exceeds the available context size \\((\\d+) tokens\\)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression maxCtx(QStringLiteral("maximum context length is (\\d+)"),
                                           QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression req(QStringLiteral("(?:resulted in|requested) (\\d+) tokens"),
                                        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression tooLong(QStringLiteral("prompt is too long: (\\d+) tokens > (\\d+)"),
                                            QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch m = llama.match(text);
    if (m.hasMatch()) {
        if (o->promptTokens < 0) o->promptTokens = m.captured(1).toInt();
        if (o->contextTokens < 0) o->contextTokens = m.captured(2).toInt();
        return;
    }
    m = tooLong.match(text);
    if (m.hasMatch()) {
        if (o->promptTokens < 0) o->promptTokens = m.captured(1).toInt();
        if (o->contextTokens < 0) o->contextTokens = m.captured(2).toInt();
        return;
    }
    m = maxCtx.match(text);
    if (m.hasMatch() && o->contextTokens < 0) o->contextTokens = m.captured(1).toInt();
    m = req.match(text);
    if (m.hasMatch() && o->promptTokens < 0) o->promptTokens = m.captured(1).toInt();
}

int intField(const QJsonObject& o, const char* key)
{
    const QJsonValue v = o.value(QLatin1String(key));
    return v.isDouble() ? v.toInt(-1) : -1;
}

void scanObject(const QJsonObject& root, ContextOverflow* o, int depth);

// Egy (esetleg JSON-t tartalmazó) szöveg feldolgozása: a beágyazott JSON-objektum is.
void scanText(const QString& text, ContextOverflow* o, int depth)
{
    if (text.isEmpty()) return;
    if (textMentionsOverflow(text)) o->matched = true;
    numbersFromText(text, o);
    if (depth > 3) return;
    const int open = text.indexOf(QLatin1Char('{'));
    const int close = text.lastIndexOf(QLatin1Char('}'));
    if (open >= 0 && close > open) {
        const QJsonDocument doc = QJsonDocument::fromJson(text.mid(open, close - open + 1).toUtf8());
        if (doc.isObject()) scanObject(doc.object(), o, depth + 1);
    }
}

void scanObject(const QJsonObject& root, ContextOverflow* o, int depth)
{
    QJsonObject e = root;
    if (root.value(QStringLiteral("error")).isObject())
        e = root.value(QStringLiteral("error")).toObject();
    else if (root.value(QStringLiteral("error")).isString())
        scanText(root.value(QStringLiteral("error")).toString(), o, depth);
    for (const char* k : {"type", "code"}) {
        const QString c = e.value(QLatin1String(k)).toVariant().toString();
        if (isOverflowCode(c)) {
            o->matched = true;
            if (o->code.isEmpty()) o->code = c;
        }
    }
    const int prompt = intField(e, "n_prompt_tokens");
    const int ctx = intField(e, "n_ctx");
    if (prompt > 0 && o->promptTokens < 0) o->promptTokens = prompt;
    if (ctx > 0 && o->contextTokens < 0) o->contextTokens = ctx;
    scanText(e.value(QStringLiteral("message")).toString(), o, depth);
}

} // namespace

ContextOverflow parseContextOverflow(const QByteArray& body, const QString& text)
{
    ContextOverflow o;
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (doc.isObject())
        scanObject(doc.object(), &o, 0);
    else if (!body.trimmed().isEmpty())
        scanText(QString::fromUtf8(body), &o, 0);
    scanText(text, &o, 0);
    if (!o.matched) return ContextOverflow{};
    if (o.code.isEmpty()) {
        const QString all = QString::fromUtf8(body) + text;
        o.code = all.contains(QLatin1String("context_length_exceeded"))
            ? QStringLiteral("context_length_exceeded")
            : all.contains(QLatin1String("exceed_context_size_error"))
            ? QStringLiteral("exceed_context_size_error") : QString();
    }
    return o;
}

// ---- LM Studio natív API -------------------------------------------------------------------

QString nativeApiRoot(const QString& baseUrl)
{
    QString b = baseUrl.trimmed();
    while (b.endsWith(QLatin1Char('/'))) b.chop(1);
    if (b.endsWith(QLatin1String("/v1"), Qt::CaseInsensitive)) b.chop(3);
    while (b.endsWith(QLatin1Char('/'))) b.chop(1);
    return b;
}

LlmServerInfo parseLmStudioModels(const QByteArray& json, const QString& model)
{
    LlmServerInfo info;
    info.model = model.trimmed();
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject()) return info;
    const QJsonValue mv = doc.object().value(QStringLiteral("models"));
    if (!mv.isArray()) return info;
    const QJsonArray models = mv.toArray();
    // A natív lista elemei kulcsot és loaded_instances-t hordoznak; a Soniox-szerű
    // {"models":[{"id":…}]} nem ilyen → ismeretlen szerver.
    bool native = models.isEmpty();
    for (const QJsonValue& v : models) {
        const QJsonObject m = v.toObject();
        if (m.contains(QStringLiteral("key")) && m.value(QStringLiteral("loaded_instances")).isArray()) {
            native = true;
            break;
        }
    }
    if (!native) return info;
    info.kind = LlmServerInfo::Kind::LmStudio;

    const QString want = info.model.toLower();
    for (const QJsonValue& v : models) {
        const QJsonObject m = v.toObject();
        const QString key = m.value(QStringLiteral("key")).toString();
        const QString type = m.value(QStringLiteral("type")).toString();
        const QJsonArray inst = m.value(QStringLiteral("loaded_instances")).toArray();
        bool mine = !want.isEmpty() && key.toLower() == want;
        if (!mine && !want.isEmpty())
            for (const QJsonValue& iv : inst)
                if (iv.toObject().value(QStringLiteral("id")).toString().toLower() == want) mine = true;
        if (!mine) {
            if (!inst.isEmpty() && type != QLatin1String("embedding")) info.otherLoaded << key;
            continue;
        }
        info.modelListed = true;
        info.modelKey = key;
        info.maxContext = m.value(QStringLiteral("max_context_length")).toInt(-1);
        for (const QJsonValue& iv : inst) {
            const QJsonObject io = iv.toObject();
            const QJsonObject cfg = io.value(QStringLiteral("config")).toObject();
            LlmInstanceInfo li;
            li.id = io.value(QStringLiteral("id")).toString();
            li.contextLength = cfg.value(QStringLiteral("context_length")).toInt(-1);
            li.parallel = cfg.value(QStringLiteral("parallel")).toInt(-1);
            info.instances.append(li);
        }
    }
    return info;
}

PreloadDecision decidePreload(const LlmServerInfo& info, int need, int setting, int floor)
{
    PreloadDecision d;
    if (!info.isLmStudio()) { d.reason = QStringLiteral("not-lmstudio"); return d; }
    if (!info.modelListed || info.modelKey.isEmpty()) { d.reason = QStringLiteral("model-unlisted"); return d; }

    const int effectiveNeed = qMax(need, floor);
    int target = setting > 0 ? setting : contextStepFor(effectiveNeed, info.maxContext);
    // A kifejezett kérés („Betöltés nagyobb kontextussal”) a rögzített beállítást is felülírja.
    if (floor > 0) target = qMax(target, contextStepFor(floor, info.maxContext));
    if (info.maxContext > 0) target = qMin(target, info.maxContext);
    // Amit a betöltött példánytól megkövetelünk: az igény, de legfeljebb amit kérnénk (ha a
    // kérendő kisebb, egy újratöltés úgysem segítene).
    const int required = qMin(effectiveNeed, target);
    d.contextLength = target;
    d.contextInsufficient = target < effectiveNeed;

    bool anyCtxOk = false;
    for (const LlmInstanceInfo& i : info.instances) {
        const bool ctxOk = i.contextLength < 0 || i.contextLength >= required;
        const bool parOk = i.parallel <= 1;   // -1 (ismeretlen) → régebbi szerver, egy szál
        if (ctxOk && parOk) {
            d.action = PreloadDecision::Action::None;
            d.reason = QStringLiteral("ok");
            d.contextLength = i.contextLength;
            d.contextInsufficient = i.contextLength >= 0 && i.contextLength < effectiveNeed;
            return d;
        }
        anyCtxOk = anyCtxOk || ctxOk;
    }
    if (info.instances.isEmpty()) {
        d.action = PreloadDecision::Action::Load;
        d.reason = QStringLiteral("not-loaded");
        return d;
    }
    d.action = PreloadDecision::Action::Reload;
    d.reason = anyCtxOk ? QStringLiteral("parallel") : QStringLiteral("context-small");
    // Csak párhuzamosság miatti újratöltésnél a meglévő (nagyobb) kontextust megtartjuk —
    // automatikus módban; a rögzített beállítás a felhasználó döntése.
    if (anyCtxOk && setting <= 0) {
        for (const LlmInstanceInfo& i : info.instances)
            if (i.contextLength > d.contextLength) d.contextLength = i.contextLength;
        if (info.maxContext > 0) d.contextLength = qMin(d.contextLength, info.maxContext);
    }
    for (const LlmInstanceInfo& i : info.instances) d.unloadIds << i.id;
    return d;
}

} // namespace llmctx
} // namespace tanara
