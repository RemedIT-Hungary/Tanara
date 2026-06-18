#include "tanara/SummaryService.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QJsonParseError>
#include <QStringList>
#include <QPointer>

namespace tanara {

namespace {

// A rendszerüzenet (magyar): a modell egy értekezlet-jegyzetelő, és KIZÁRÓLAG
// egy JSON objektumot adhat vissza a megadott kulcsokkal.
QString buildSystemPrompt()
{
    return QStringLiteral(
        "Te egy precíz magyar nyelvű értekezlet-jegyzetelő asszisztens vagy. "
        "A feladatod, hogy a kapott beszéd-átiratból strukturált összefoglalót készíts.\n"
        "FONTOS szabályok:\n"
        "1. KIZÁRÓLAG egyetlen érvényes JSON objektumot adj vissza, semmilyen más szöveget, "
        "magyarázatot vagy markdown kódkerítést (```), előtte vagy utána ne írj.\n"
        "2. A JSON objektum pontosan ezeket a kulcsokat tartalmazza:\n"
        "   - \"execSummary\": string — rövid vezetői összefoglaló az értekezletről.\n"
        "   - \"decisions\": string tömb — a meghozott döntések, egyenként egy elem.\n"
        "   - \"actionItems\": objektum tömb, minden elem {\"text\": string, \"owner\": string, "
        "\"due\": string} alakú — a teendő szövege, a felelős neve, és a határidő "
        "(ha nincs adat, üres string).\n"
        "   - \"participants\": string tömb — az értekezlet résztvevőinek nevei.\n"
        "3. Minden mezőt MAGYARUL tölts ki.\n"
        "4. Használd a megadott szójegyzéket (glossary) a beszédfelismerés (STT) "
        "valószínű hibáinak javítására: tulajdonneveknél, cégneveknél és szakkifejezéseknél "
        "a szójegyzék helyes alakját preferáld.\n");
}

QString buildUserPrompt(const MergedTranscript& transcript,
                        const QString& contextNotes,
                        const QStringList& glossary)
{
    QString out;
    if (!contextNotes.isEmpty()) {
        out += QStringLiteral("Kontextus / jegyzetek:\n");
        out += contextNotes;
        out += QStringLiteral("\n\n");
    }
    if (!glossary.isEmpty()) {
        out += QStringLiteral("Szójegyzék (helyes alakok az STT-hibák javításához):\n");
        out += glossary.join(QStringLiteral(", "));
        out += QStringLiteral("\n\n");
    }
    out += QStringLiteral("----\n");
    out += transcript.renderMarkdown();
    return out;
}

// Megengedő JSON-kinyerés: leveszi az esetleges ```json ... ``` kerítést, és
// ha még mindig nem objektum, megpróbálja az első { ... } blokkot kivágni.
QByteArray extractJson(const QString& raw)
{
    QString s = raw.trimmed();

    if (s.startsWith(QStringLiteral("```"))) {
        // Első sor (```json vagy ```) eldobása.
        const int nl = s.indexOf(QLatin1Char('\n'));
        if (nl >= 0)
            s = s.mid(nl + 1);
        // Záró kerítés eldobása.
        const int fence = s.lastIndexOf(QStringLiteral("```"));
        if (fence >= 0)
            s = s.left(fence);
        s = s.trimmed();
    }

    if (!s.startsWith(QLatin1Char('{'))) {
        const int start = s.indexOf(QLatin1Char('{'));
        const int end = s.lastIndexOf(QLatin1Char('}'));
        if (start >= 0 && end > start)
            s = s.mid(start, end - start + 1);
    }

    return s.toUtf8();
}

Summary parseSummary(const QJsonObject& root)
{
    Summary sum;
    sum.execSummary = root.value(QStringLiteral("execSummary")).toString();

    const QJsonArray decisions = root.value(QStringLiteral("decisions")).toArray();
    for (const QJsonValue& v : decisions) {
        const QString d = v.toString();
        if (!d.isEmpty())
            sum.decisions.append(d);
    }

    const QJsonArray items = root.value(QStringLiteral("actionItems")).toArray();
    for (const QJsonValue& v : items) {
        const QJsonObject o = v.toObject();
        ActionItem ai;
        ai.text = o.value(QStringLiteral("text")).toString();
        ai.owner = o.value(QStringLiteral("owner")).toString();
        ai.due = o.value(QStringLiteral("due")).toString();
        if (!ai.text.isEmpty())
            sum.actionItems.append(ai);
    }

    const QJsonArray participants = root.value(QStringLiteral("participants")).toArray();
    for (const QJsonValue& v : participants) {
        const QString p = v.toString();
        if (!p.isEmpty())
            sum.participants.append(p);
    }

    return sum;
}

} // namespace

SummaryService::SummaryService(ILlmProvider* provider, QObject* parent)
    : QObject(parent)
    , m_provider(provider)
{
}

SummaryService::~SummaryService() = default;

void SummaryService::summarize(const MergedTranscript& transcript,
                               const QString& contextNotes,
                               const QStringList& glossary,
                               const QString& model,
                               double temperature,
                               int maxTokens)
{
    if (!m_provider) {
        emit summaryFailed(QStringLiteral("Nincs beállított LLM provider."));
        return;
    }

    LlmRequest req;
    req.model = model;
    req.stream = false;
    req.temperature = temperature;
    req.maxTokens = maxTokens > 0 ? maxTokens : 8000;   // reasoning-modellnek bőven kell (gondolkodás + JSON kimenet)

    ChatMessage sys;
    sys.role = QStringLiteral("system");
    sys.content = buildSystemPrompt();

    ChatMessage usr;
    usr.role = QStringLiteral("user");
    usr.content = buildUserPrompt(transcript, contextNotes, glossary);

    req.messages.append(sys);
    req.messages.append(usr);

    LlmJob* job = m_provider->chat(req);
    if (!job) {
        emit summaryFailed(QStringLiteral("A provider nem adott vissza jobot."));
        return;
    }

    QPointer<SummaryService> self(this);

    connect(job, &LlmJob::finished, this, [self, job](const QString& text) {
        job->deleteLater();
        if (!self)
            return;

        const QByteArray json = extractJson(text);
        QJsonParseError perr{};
        const QJsonDocument doc = QJsonDocument::fromJson(json, &perr);
        if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
            emit self->summaryFailed(
                QStringLiteral("Nem sikerült JSON-ként értelmezni a választ: %1")
                    .arg(perr.errorString()));
            return;
        }
        emit self->summaryReady(parseSummary(doc.object()));
    });

    connect(job, &LlmJob::failed, this, [self, job](const QString& error) {
        job->deleteLater();
        if (!self)
            return;
        emit self->summaryFailed(error);
    });
}

// ---- Summary::renderMarkdown (a Types.h-ban deklarálva) --------------------

QString Summary::renderMarkdown() const
{
    QString md;

    if (!execSummary.isEmpty()) {
        md += QStringLiteral("## Vezetői összefoglaló\n\n");
        md += execSummary;
        md += QStringLiteral("\n\n");
    }

    if (!decisions.isEmpty()) {
        md += QStringLiteral("## Döntések\n\n");
        for (const QString& d : decisions)
            md += QStringLiteral("- ") + d + QStringLiteral("\n");
        md += QStringLiteral("\n");
    }

    if (!actionItems.isEmpty()) {
        md += QStringLiteral("## Teendők\n\n");
        for (const ActionItem& ai : actionItems) {
            md += QStringLiteral("- [ ] ") + ai.text;
            if (!ai.owner.isEmpty())
                md += QStringLiteral(" — ") + ai.owner;
            if (!ai.due.isEmpty())
                md += QStringLiteral(" (") + ai.due + QStringLiteral(")");
            md += QStringLiteral("\n");
        }
        md += QStringLiteral("\n");
    }

    if (!participants.isEmpty()) {
        md += QStringLiteral("## Résztvevők\n\n");
        md += participants.join(QStringLiteral(", "));
        md += QStringLiteral("\n");
    }

    return md;
}

} // namespace tanara
