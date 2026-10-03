#pragma once
//
// SummaryStore — a meeting összefoglalójának STRUKTURÁLT elérése (M06–M08).
//
// A lemezen eddig csak a summary.md létezett. Mostantól minden ÚJ összefoglaló mellé
// summary.json is készül (a strukturált Summary + a keletkezés metaadatai: mikor, melyik
// szolgáltató/modell, gyors vagy témánkénti). Régi meetingnél (csak summary.md) a
// struktúrát a markdownból nyerjük vissza egy hibatűrő értelmezővel — az a formátum, amit
// a Summary::renderMarkdown és a komplex összefoglaló ír, veszteség nélkül visszaolvasható.
//
// A summary.md marad az ember-olvasható / jegyzetekbe másolt forma; ha valaki kézzel
// szerkeszti (újabb, mint a summary.json), a markdown az igazság, a metaadat a json-ból jön.
//
#include "tanara/Types.h"

#include <QJsonObject>
#include <QMetaType>

namespace tanara {

enum class SummaryMode {
    Unknown,   // régi összefoglaló, a mód nem állapítható meg biztosan
    Quick,     // gyors összefoglaló (egy LLM-hívás)
    Topics,    // témánkénti elemzés (témák → elemzések → összegzés)
};

// Az összefoglaló keletkezésének adatai (M07 jobb oszlop). Régi összefoglalónál a
// providerId / model üres, a createdAt a summary.md módosítási ideje.
struct SummaryMeta {
    QDateTime   createdAt;
    QString     providerId;    // pl. "openai-compat", "tanara-cloud"
    QString     model;         // a ténylegesen küldött modell-azonosító
    SummaryMode mode = SummaryMode::Unknown;
};

struct SummaryDocument {
    bool        exists = false;        // van-e összefoglaló a meetinghez
    Summary     summary;               // vezetői összefoglaló, döntések, teendők, résztvevők
    QVector<TopicAnalysis> topics;     // témánkénti mód: a témaszekciók, sorrendben
    SummaryMeta meta;
    QString     markdown;              // a summary.md tartalma
    bool        fromMarkdown = false;  // a struktúra a markdownból lett visszanyerve
};

namespace summarystore {

QString jsonPath(const QString& meetingFolder);       // <mappa>/summary.json
QString markdownPath(const QString& meetingFolder);   // <mappa>/summary.md

// Markdown → struktúra. Felismeri a gyors (## Vezetői összefoglaló / ## Döntések /
// ## Teendők / ## Résztvevők) és a témánkénti (## Teendők (összevont) / ## Témák /
// ### N. cím + **Döntések:** / **Teendők:**) formát; a címsorokat ékezet- és kisbetű-
// függetlenül illeszti. Ismeretlen szakasz szövege nem vész el: a vezetői összefoglaló
// végére kerül. Témánkénti módban a summary.decisions a témák döntéseinek uniója.
SummaryDocument parseMarkdown(const QString& markdown);

// A meeting összefoglalójának betöltése (summary.json, különben summary.md értelmezve).
SummaryDocument load(const QString& meetingFolder);

// A strukturált forma + metaadat mentése (summary.json). A summary.md-t NEM írja.
bool save(const QString& meetingFolder, const SummaryDocument& doc);

QJsonObject    toJson(const SummaryDocument& doc);
SummaryDocument fromJson(const QJsonObject& o);

QString     modeToString(SummaryMode m);              // "quick" | "topics" | "unknown"
SummaryMode modeFromString(const QString& s);

} // namespace summarystore
} // namespace tanara

Q_DECLARE_METATYPE(tanara::SummaryMode)
Q_DECLARE_METATYPE(tanara::SummaryMeta)
Q_DECLARE_METATYPE(tanara::SummaryDocument)
