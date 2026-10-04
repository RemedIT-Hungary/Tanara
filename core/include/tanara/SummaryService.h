#pragma once
//
// SummaryService — a gyors összefoglaló vezénylése: a merge-elt átiratból memó + vezetői
// összefoglaló + döntések + nyitott kérdések + teendők (Summary), LLM-mel.
//
// Egy kódút minden hosszra (lásd summary/SummaryPipeline.h):
//  - 1 rész (rövid megbeszélés, ~22 percig): EGY hívás a "single" prompttal (jegyzet + JSON);
//  - n rész: n jegyzet-hívás ("notes"), egymás után, majd egy összegző hívás ("merge").
// A kész részjegyzetek a gyorsítótárba (cachePath, pl. <meeting>/summary.notes.json) kerülnek:
// egy elbukott / megszakított futás újraindításkor onnan folytatódik (a kész részek nem futnak
// újra; ha csak az összegzés bukott, csak az fut újra). Sikeres futás után a gyorsítótár törlődik.
// Hiba esetén NINCS részleges eredmény: csak summaryFailed jön.
// A provider NEM tulajdona (nem deletálja).
//
#include "tanara/Types.h"
#include "tanara/llm/ILlmProvider.h"
#include "tanara/summary/SummaryPipeline.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <functional>

namespace tanara {

struct SummaryRequest {
    MergedTranscript transcript;
    QString     contextNotes;
    QStringList glossary;
    // A rendszer-promptok, KÉSZ alakban (felülírás + nyelv alkalmazva). Üres → a beépített,
    // a `language` alkalmazásával.
    QString     notesPrompt;
    QString     mergePrompt;
    QString     singlePrompt;
    QString     language;                   // a célnyelv (beállítás szövege; az emlékeztetőhöz)
    QString     model;
    double      temperature = 0.2;
    int         maxTokens = 8000;           // single / merge hívás
    int         notesMaxTokens = 3000;      // részenkénti jegyzet
    qint64      partMs = summarypipe::kDefaultPartMs;
    QString     cachePath;                  // részjegyzet-gyorsítótár; üres → nincs
};

// A futás terve (a feladat-szakaszokhoz és a költségbecsléshez — LLM-hívás nélkül).
struct SummaryPlan {
    int  parts = 0;              // részek száma (0 = üres átirat)
    int  cachedParts = 0;        // ebből a gyorsítótárban kész
    bool singleCall = false;     // 1 rész, gyorsítótár nélkül → egy hívás ("single")
    int  llmCalls = 0;           // a hátralévő LLM-hívások száma
    QVector<int> partChars;      // részenként az átirat-markdown hossza
};

class SummaryService : public QObject {
    Q_OBJECT
public:
    explicit SummaryService(ILlmProvider* provider, QObject* parent = nullptr);
    ~SummaryService() override;

    static SummaryPlan plan(const SummaryRequest& req);

    // Elindítja a futást. Az eredmény a summaryReady / summaryFailed jelen jön (aszinkron).
    void summarize(const SummaryRequest& req);
    // A futó hívás megszakítása; ezután semmilyen jel nem jön. A gyorsítótár megmarad.
    void cancel();

    // A rövid megbeszélés beépített egylépéses promptja ("single").
    static QString defaultSystemPrompt();

    // A gyorsítótár-fájl szokásos neve a meeting mappájában.
    static QString cacheFileName() { return QStringLiteral("summary.notes.json"); }

signals:
    // Haladás. stage: "notes" (done / total rész kész), "merge" (0/1), "single" (0/1).
    void progress(const QString& stage, int done, int total);
    void summaryReady(const tanara::Summary& summary);
    void summaryFailed(const QString& error);

private:
    void startNextNotes();
    void startMerge();
    void startSingle();
    void call(const QString& system, const QString& user, int maxTokens,
              std::function<void(const QString&)> onText);
    void fail(const QString& error);
    QString userHeader() const;
    QString reminder() const;
    QString prompt(const QString& given, const char* id) const;
    QString cacheKey(int part) const;
    void loadCache();
    void storeCache(int part, const QString& raw);

    ILlmProvider* m_provider;  // not owned
    SummaryRequest m_req;
    QVector<summarypipe::TranscriptPart> m_parts;
    QVector<summarypipe::PartNotes> m_notes;
    QVector<bool> m_done;
    QStringList m_speakers;
    QPointer<LlmJob> m_job;
    bool m_cancelled = false;
    bool m_running = false;
};

} // namespace tanara
