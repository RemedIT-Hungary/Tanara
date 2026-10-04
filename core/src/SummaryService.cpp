#include "tanara/SummaryService.h"
#include "tanara/PromptLibrary.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSaveFile>
#include <QStringList>
#include <QDebug>

#include <cmath>

namespace tanara {

using namespace summarypipe;

namespace {

QString resolvePrompt(const QString& given, const char* id, const QString& language)
{
    if (!given.trimmed().isEmpty()) return given;
    return applySummaryLanguage(promptBuiltin(QLatin1String(id)), language);
}

// A részjegyzet gyorsítótár-kulcsa: minden, ami a jegyzet tartalmát meghatározza (modell,
// prompt, kontextus, szójegyzék, a rész szövege). Bármelyik változik → új jegyzet kell.
QString cacheKeyFor(const SummaryRequest& req, const QString& prompt, const QString& partMarkdown)
{
    QCryptographicHash h(QCryptographicHash::Sha1);
    for (const QString& s : {QStringLiteral("v1"), req.model, prompt, req.contextNotes,
                             req.glossary.join(QLatin1Char('|')), partMarkdown}) {
        h.addData(s.toUtf8());
        h.addData(QByteArrayLiteral("\x1f"));
    }
    return QString::fromLatin1(h.result().toHex());
}

// A felhasználói üzenet közös feje: kontextus + szójegyzék.
QString headerFor(const SummaryRequest& req)
{
    QString out;
    if (!req.contextNotes.trimmed().isEmpty())
        out += QStringLiteral("Context / notes:\n") + req.contextNotes.trimmed() + QStringLiteral("\n\n");
    if (!req.glossary.isEmpty())
        out += QStringLiteral("Glossary (correct spellings, for fixing speech-recognition errors):\n")
               + req.glossary.join(QStringLiteral(", ")) + QStringLiteral("\n\n");
    return out;
}

// A jegyzet-hívás bemenete a rész nélkül: prompt + fej + rész-fejléc + emlékeztető.
qint64 notesOverheadChars(const SummaryRequest& req)
{
    return resolvePrompt(req.notesPrompt, "notes", req.language).size() + headerFor(req).size()
         + languageReminder(req.language).size() + 200;
}
qint64 singleOverheadChars(const SummaryRequest& req)
{
    return resolvePrompt(req.singlePrompt, "single", req.language).size() + headerFor(req).size()
         + languageReminder(req.language).size() + 20;
}

QJsonObject readCache(const QString& path)
{
    if (path.isEmpty()) return {};
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    return o.value(QStringLiteral("parts")).toObject();
}

} // namespace

SummaryService::SummaryService(ILlmProvider* provider, QObject* parent)
    : QObject(parent)
    , m_provider(provider)
{
}

SummaryService::~SummaryService()
{
    m_cancelled = true;
    if (m_job) m_job->cancel();
}

QString SummaryService::defaultSystemPrompt()
{
    return promptBuiltin(QStringLiteral("single"));
}

SummaryPlan SummaryService::plan(const SummaryRequest& req)
{
    SummaryPlan p;
    const QVector<TranscriptPart> parts = splitTranscript(req.transcript.segments(), req.partMs);
    p.parts = int(parts.size());
    if (parts.isEmpty()) return p;
    const QJsonObject cache = readCache(req.cachePath);
    const QString prompt = parts.size() == 1 ? resolvePrompt(req.singlePrompt, "single", req.language)
                                             : resolvePrompt(req.notesPrompt, "notes", req.language);
    for (const TranscriptPart& part : parts) {
        p.partChars.append(int(part.markdown.size()));
        if (cache.contains(cacheKeyFor(req, prompt, part.markdown))) ++p.cachedParts;
    }
    p.singleCall = p.parts == 1 && p.cachedParts == 0;
    p.llmCalls = p.singleCall ? 1 : (p.parts - p.cachedParts) + 1;
    return p;
}

int SummaryService::contextNeed(const SummaryRequest& req)
{
    const QVector<TranscriptPart> parts = splitTranscript(req.transcript.segments(), req.partMs);
    if (parts.isEmpty()) return 0;
    const SummaryPlan p = plan(req);
    const int notesOut = req.notesMaxTokens > 0 ? req.notesMaxTokens : 3000;
    const int out = req.maxTokens > 0 ? req.maxTokens : 8000;
    if (p.singleCall)
        return llmctx::callContextNeed(singleOverheadChars(req) + parts.first().markdown.size(), out);
    int need = 0;
    for (const TranscriptPart& part : parts)
        need = qMax(need, llmctx::callContextNeed(notesOverheadChars(req) + part.markdown.size(), notesOut));
    // Az összegzés bemenete a részjegyzetek: részenként jellemzően a jegyzet-keret fele.
    const qint64 mergeChars = resolvePrompt(req.mergePrompt, "merge", req.language).size()
                            + headerFor(req).size() + 400;
    const int mergeNeed = llmctx::callContextNeed(mergeChars, out)
                        + int(std::ceil(double(parts.size()) * notesOut / 2.0 * llmctx::kSafetyFactor));
    return qMax(need, mergeNeed);
}

QString SummaryService::prompt(const QString& given, const char* id) const
{
    return resolvePrompt(given, id, m_req.language);
}

QString SummaryService::cacheKey(int part) const
{
    const QString p = m_parts.size() == 1 && !m_forceNotes ? prompt(m_req.singlePrompt, "single")
                                                           : prompt(m_req.notesPrompt, "notes");
    return cacheKeyFor(m_req, p, m_parts.at(part).markdown);
}

void SummaryService::loadCache()
{
    const QJsonObject cache = readCache(m_req.cachePath);
    for (int i = 0; i < m_parts.size(); ++i) {
        const QString raw = cache.value(cacheKey(i)).toString();
        if (raw.isEmpty()) continue;
        const PartNotes n = parseNotes(raw, m_parts.at(i));
        if (n.isEmpty()) continue;
        m_notes[i] = n;
        m_done[i] = true;
    }
}

void SummaryService::storeCache(int part, const QString& raw)
{
    if (m_req.cachePath.isEmpty()) return;
    QJsonObject root;
    {
        QFile f(m_req.cachePath);
        if (f.open(QIODevice::ReadOnly)) root = QJsonDocument::fromJson(f.readAll()).object();
    }
    QJsonObject parts = root.value(QStringLiteral("parts")).toObject();
    parts.insert(cacheKey(part), raw);
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("parts"), parts);
    QSaveFile f(m_req.cachePath);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        f.commit();
    }
}

void SummaryService::summarize(const SummaryRequest& req)
{
    if (m_running) return;
    if (!m_provider) {
        emit summaryFailed(tr("Nincs beállított LLM provider."));
        return;
    }
    m_req = req;
    m_cancelled = false;
    const QVector<Utterance> segs = m_req.transcript.segments();
    m_parts = splitTranscript(segs, m_req.partMs);
    m_speakers = speakersOf(segs);
    if (m_parts.isEmpty()) {
        emit summaryFailed(tr("Az átirat üres — nincs mit összefoglalni."));
        return;
    }
    m_forceNotes = false;
    m_resplitDone = false;
    m_notes = QVector<PartNotes>(m_parts.size());
    m_done = QVector<bool>(m_parts.size(), false);
    loadCache();
    m_running = true;

    // Ismert kontextusnál a túl nagy részeket már most kisebbekre bontjuk (a kész részek
    // jegyzetei a gyorsítótárból maradnak). Ha nem sikerül, a futás így indul — a szerver
    // hibája úgyis érthetően besorolódik.
    if (m_req.contextLimit > 0) {
        const int before = int(m_parts.size());
        if (m_parts.size() == 1 && !m_done.at(0) && !singleFits(m_req.contextLimit)) {
            m_forceNotes = true;
            loadCache();   // a jegyzet-prompt kulcsával egy korábbi futás jegyzete is jó
        }
        if (m_forceNotes || m_parts.size() > 1)
            fitParts(llmctx::partBudgetChars(m_req.contextLimit, notesOverheadChars(m_req),
                                             m_req.notesMaxTokens));
        if (m_parts.size() != before || m_forceNotes) {
            int cached = 0;
            for (bool d : std::as_const(m_done)) cached += d ? 1 : 0;
            emit partsChanged(int(m_parts.size()), cached);
        }
    }
    startFlow();
}

void SummaryService::startFlow()
{
    if (m_parts.size() == 1 && !m_done.at(0) && !m_forceNotes)
        startSingle();
    else
        startNextNotes();
}

bool SummaryService::singleFits(int contextTokens) const
{
    const qint64 input = singleOverheadChars(m_req) + m_parts.value(0).markdown.size();
    return llmctx::callContextNeed(input, m_req.maxTokens) <= contextTokens;
}

bool SummaryService::fitParts(int budgetChars)
{
    if (budgetChars <= 0) return false;
    const QVector<Utterance> segs = m_req.transcript.segments();
    QVector<TranscriptPart> parts;
    QVector<PartNotes> notes;
    QVector<bool> done;
    bool changed = false;
    for (int i = 0; i < m_parts.size(); ++i) {
        const TranscriptPart& p = m_parts.at(i);
        if (m_done.at(i) || p.markdown.size() <= budgetChars) {
            parts.append(p);
            notes.append(m_notes.at(i));
            done.append(m_done.at(i));
            continue;
        }
        QVector<Utterance> mine;
        for (const Utterance& u : segs)
            if (u.startMs >= p.startMs && u.endMs <= p.endMs) mine.append(u);
        const QVector<TranscriptPart> sub = splitTranscriptByChars(mine, budgetChars);
        if (sub.isEmpty()) return false;   // egy bekezdés önmagában sem fér bele
        for (const TranscriptPart& sp : sub) {
            parts.append(sp);
            notes.append(PartNotes{});
            done.append(false);
        }
        changed = true;
    }
    if (!changed) return true;
    for (int i = 0; i < parts.size(); ++i) {
        parts[i].index = i;
        notes[i].index = i;
    }
    m_parts = parts;
    m_notes = notes;
    m_done = done;
    loadCache();   // az új (kisebb) részek közül, ami egy korábbi futásból kész, az marad
    return true;
}

bool SummaryService::recoverFromOverflow(const llmctx::ContextOverflow& ov, qint64 failedInputChars)
{
    if (!m_req.adaptToContext || m_resplitDone || !ov.matched || ov.contextTokens <= 0)
        return false;
    m_resplitDone = true;
    emit contextLimitDetected(ov.contextTokens);
    int budget = llmctx::partBudgetChars(ov.contextTokens, notesOverheadChars(m_req),
                                         m_req.notesMaxTokens);
    // A szerver a kérés valódi tokenszámát is megmondhatja: a mért karakter/token arány
    // pontosabb a becslésnél (sűrű szövegnél kevesebb karakter jut egy tokenre).
    if (budget > 0 && ov.promptTokens > 0 && failedInputChars > 0) {
        const double ratio = double(failedInputChars) / ov.promptTokens;
        const double room = double(ov.contextTokens - m_req.notesMaxTokens) / llmctx::kSafetyFactor
                          - llmctx::kTemplateOverheadTokens;
        const int measured = int(std::floor(room * ratio)) - int(notesOverheadChars(m_req));
        budget = qMin(budget, measured);
        if (budget < int(llmctx::kMinPartTokens * ratio)) budget = -1;
    }
    if (budget <= 0) {
        qWarning().noquote() << "[Summary] a kontextus (" << ov.contextTokens
                             << " token) a legkisebb részhez is kevés — nem bontunk tovább";
        return false;
    }
    const int before = int(m_parts.size());
    const bool wasSingle = m_parts.size() == 1 && !m_forceNotes;
    if (wasSingle) m_forceNotes = true;
    if (!fitParts(budget)) {
        qWarning().noquote() << "[Summary] egy bekezdés önmagában sem fér a kontextusba";
        return false;
    }
    if (m_parts.size() == before && !wasSingle) return false;   // nem lett kisebb: nem segít
    int cached = 0;
    for (bool d : std::as_const(m_done)) cached += d ? 1 : 0;
    qInfo().noquote().nospace() << "[Summary] kontextus " << ov.contextTokens
        << " token → újrabontás: " << before << " → " << m_parts.size() << " rész (keret "
        << budget << " karakter)";
    emit partsChanged(int(m_parts.size()), cached);
    return true;
}

void SummaryService::cancel()
{
    m_cancelled = true;
    m_running = false;
    if (m_job) m_job->cancel();
}

void SummaryService::fail(const QString& error)
{
    m_running = false;
    emit summaryFailed(error);
}

QString SummaryService::reminder() const
{
    return languageReminder(m_req.language);
}

// A felhasználói üzenet közös feje: kontextus + szójegyzék.
QString SummaryService::userHeader() const
{
    return headerFor(m_req);
}

void SummaryService::call(const QString& system, const QString& user, int maxTokens,
                          std::function<void(const QString&)> onText, bool adaptive)
{
    LlmRequest req;
    req.model = m_req.model;
    req.stream = false;
    req.temperature = m_req.temperature;
    req.maxTokens = maxTokens > 0 ? maxTokens : 8000;
    req.messages.append({QStringLiteral("system"), system});
    req.messages.append({QStringLiteral("user"), user});

    LlmJob* job = m_provider->chat(req);
    if (!job) { fail(tr("A provider nem adott vissza jobot.")); return; }
    m_job = job;
    QPointer<SummaryService> self(this);
    connect(job, &LlmJob::finished, this, [self, job, onText](const QString& text) {
        job->deleteLater();
        if (!self || self->m_cancelled) return;
        self->m_job.clear();
        onText(text);
    });
    const qint64 inputChars = system.size() + user.size();
    connect(job, &LlmJob::failed, this, [self, job, adaptive, inputChars](const QString& error) {
        job->deleteLater();
        if (!self || self->m_cancelled) return;
        self->m_job.clear();
        // Jegyzet- / egylépéses hívás „nem fér a kontextusba” hibája: a hátralévő részek
        // kisebbekre bontva folytatódnak (egyszer). Az összegzés bemenetét nem lehet bontani.
        if (adaptive) {
            const llmctx::ContextOverflow ov = llmctx::parseContextOverflow(job->errorBody(), error);
            if (self->recoverFromOverflow(ov, inputChars)) {
                self->startFlow();
                return;
            }
        }
        self->fail(error);
    });
}

void SummaryService::startNextNotes()
{
    const int n = int(m_parts.size());
    int k = 0;
    int doneCount = 0;
    for (bool d : std::as_const(m_done)) doneCount += d ? 1 : 0;
    while (k < n && m_done.at(k)) ++k;
    emit progress(QStringLiteral("notes"), doneCount, n);
    if (k >= n) { startMerge(); return; }

    const TranscriptPart& part = m_parts.at(k);
    QString user = userHeader();
    // Az előző rész utolsó tárgya: ha a rész ezzel folytatódik, ugyanazt a címet kapja (a memó
    // így a részhatáron sem duplikálja a szakaszt).
    if (k > 0 && m_done.at(k - 1) && !m_notes.at(k - 1).topics.isEmpty())
        user += QStringLiteral("The previous part ended with the subject: \"%1\"\n\n")
                    .arg(m_notes.at(k - 1).topics.last().title);
    user += QStringLiteral("PART %1 of %2 [%3-%4]\n----\n")
                .arg(k + 1).arg(n).arg(formatTimestamp(part.startMs), formatTimestamp(part.endMs));
    user += part.markdown;
    user += reminder();

    call(prompt(m_req.notesPrompt, "notes"), user, m_req.notesMaxTokens, [this, k, n](const QString& text) {
        const PartNotes notes = parseNotes(text, m_parts.at(k));
        if (notes.isEmpty()) {
            qWarning().noquote() << "[Summary] üres részjegyzet (" << k + 1 << "/" << n
                                 << ") — nyers válasz:\n" << text.left(2000);
            fail(tr("A(z) %1/%2. rész jegyzete üres vagy nem értelmezhető.").arg(k + 1).arg(n));
            return;
        }
        m_notes[k] = notes;
        m_done[k] = true;
        storeCache(k, text);
        startNextNotes();
    }, /*adaptive*/ true);
}

void SummaryService::startMerge()
{
    emit progress(QStringLiteral("merge"), 0, 1);
    QString user = userHeader();
    user += renderNotesForMerge(m_notes, m_speakers);
    user += reminder();
    call(prompt(m_req.mergePrompt, "merge"), user, m_req.maxTokens, [this](const QString& text) {
        const MergeResult r = parseMergeJson(text);
        if (!r.ok) {
            qWarning().noquote() << "[Summary] az összegzés nem értelmezhető — nyers válasz:\n" << text.left(3000);
            fail(tr("Az összegzés válasza nem értelmezhető: %1. A részjegyzetek megmaradtak — "
                    "újrapróbáláskor csak az összegzés fut le újra.").arg(r.error));
            return;
        }
        m_running = false;
        if (!m_req.cachePath.isEmpty()) QFile::remove(m_req.cachePath);
        emit summaryReady(buildSummary(r, m_notes, m_speakers));
    });
}

void SummaryService::startSingle()
{
    emit progress(QStringLiteral("single"), 0, 1);
    const TranscriptPart& part = m_parts.at(0);
    QString user = userHeader();
    user += QStringLiteral("----\n") + part.markdown;
    user += reminder();
    call(prompt(m_req.singlePrompt, "single"), user, m_req.maxTokens, [this](const QString& text) {
        const SingleResult r = parseSingle(text, m_parts.at(0));
        if (!r.merge.ok) {
            qWarning().noquote() << "[Summary] az egylépéses válasz nem értelmezhető — nyers válasz:\n"
                                 << text.left(3000);
            if (!r.notes.isEmpty()) {
                // A jegyzet használható: megtartjuk, az újrapróbálás csak az összegzést futtatja.
                const int cut = text.indexOf(QLatin1Char('{'));
                storeCache(0, cut > 0 ? text.left(cut) : text);
                fail(tr("Az összefoglaló válasza nem értelmezhető: %1. A jegyzet megmaradt — "
                        "újrapróbáláskor csak az összegzés fut le újra.").arg(r.merge.error));
            } else {
                fail(tr("Az összefoglaló válasza nem értelmezhető: %1.").arg(r.merge.error));
            }
            return;
        }
        m_running = false;
        if (!m_req.cachePath.isEmpty()) QFile::remove(m_req.cachePath);
        m_notes[0] = r.notes;
        emit summaryReady(buildSummary(r.merge, m_notes, m_speakers));
    }, /*adaptive*/ true);
}

// ---- Summary::renderMarkdown (a Types.h-ban deklarálva) --------------------

namespace {
QString renderActionItem(const ActionItem& ai)
{
    QString s = QStringLiteral("- [ ] ") + ai.text;
    if (!ai.owner.isEmpty()) s += QStringLiteral(" — ") + ai.owner;
    if (!ai.due.isEmpty())   s += QStringLiteral(" (") + ai.due + QStringLiteral(")");
    return s + QLatin1Char('\n');
}
} // namespace

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

    if (!openQuestions.isEmpty()) {
        md += QStringLiteral("## Nyitott kérdések\n\n");
        for (const QString& q : openQuestions)
            md += QStringLiteral("- ") + q + QStringLiteral("\n");
        md += QStringLiteral("\n");
    }

    if (!actionItems.isEmpty()) {
        md += QStringLiteral("## Teendők\n\n");
        for (const ActionItem& ai : actionItems)
            md += renderActionItem(ai);
        md += QStringLiteral("\n");
    }

    if (!participants.isEmpty()) {
        md += QStringLiteral("## Résztvevők\n\n");
        md += participants.join(QStringLiteral(", "));
        md += QStringLiteral("\n\n");
    }

    // A hosszú forma: tárgyanként egy al-cím az időkerettel, alatta a pontok.
    if (!memo.isEmpty()) {
        md += QStringLiteral("## Memó\n\n");
        for (const MemoSection& m : memo) {
            md += QStringLiteral("### ") + m.title;
            if (m.startMs >= 0 && m.endMs >= 0)
                md += QStringLiteral(" (%1–%2)").arg(summarypipe::formatTimestamp(m.startMs),
                                                    summarypipe::formatTimestamp(m.endMs));
            else if (m.startMs >= 0)
                md += QStringLiteral(" (%1)").arg(summarypipe::formatTimestamp(m.startMs));
            md += QStringLiteral("\n\n");
            for (const QString& p : m.points)
                md += QStringLiteral("- ") + p + QStringLiteral("\n");
            md += QStringLiteral("\n");
        }
    }

    while (md.endsWith(QStringLiteral("\n\n"))) md.chop(1);
    return md;
}

// ---- TopicAnalysis::renderMarkdown (a Types.h-ban deklarálva) ---------------

QString TopicAnalysis::renderMarkdown() const
{
    QString md;
    if (!detail.isEmpty())
        md += detail + QStringLiteral("\n\n");
    if (!decisions.isEmpty()) {
        md += QStringLiteral("**Döntések:**\n\n");
        for (const QString& d : decisions)
            md += QStringLiteral("- ") + d + QStringLiteral("\n");
        md += QStringLiteral("\n");
    }
    if (!openQuestions.isEmpty()) {
        md += QStringLiteral("**Nyitott kérdések:**\n\n");
        for (const QString& q : openQuestions)
            md += QStringLiteral("- ") + q + QStringLiteral("\n");
        md += QStringLiteral("\n");
    }
    if (!actionItems.isEmpty()) {
        md += QStringLiteral("**Teendők:**\n\n");
        for (const ActionItem& ai : actionItems)
            md += renderActionItem(ai);
        md += QStringLiteral("\n");
    }
    return md;
}

} // namespace tanara
