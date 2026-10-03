#include "tanara/PromptLibrary.h"
#include "tanara/Paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>

namespace tanara {

QString promptBuiltin(const QString& id)
{
    if (id == QStringLiteral("simple")) {
        // A modell egy értekezlet-jegyzetelő, és KIZÁRÓLAG egy JSON objektumot adhat
        // vissza a megadott kulcsokkal. A felhasználó felülírhatja a Beállításokban.
        return QStringLiteral(
            "Te egy precíz értekezlet-jegyzetelő asszisztens vagy. "
            "A feladatod, hogy a kapott beszéd-átiratból strukturált összefoglalót készíts.\n"
            "FONTOS szabályok:\n"
            "1. KIZÁRÓLAG egyetlen érvényes JSON objektumot adj vissza, semmilyen más szöveget, "
            "magyarázatot vagy markdown kódkerítést (```), előtte vagy utána ne írj.\n"
            "2. A JSON objektum pontosan ezeket a kulcsokat tartalmazza:\n"
            "   - \"execSummary\": string — vezetői összefoglaló a beszélgetésről.\n"
            "   - \"decisions\": string tömb — a meghozott döntések, egyenként egy elem.\n"
            "   - \"actionItems\": objektum tömb, minden elem {\"text\": string, \"owner\": string, "
            "\"due\": string} alakú — a teendő szövege, a felelős neve, és a határidő "
            "(ha nincs adat, üres string).\n"
            "   - \"participants\": string tömb — a beszélgetés résztvevőinek nevei.\n"
            "3. Minden mezőt {{NYELV}} nyelven tölts ki.\n"
            "4. Használd a megadott szójegyzéket (glossary) a beszédfelismerés (STT) "
            "valószínű hibáinak javítására: tulajdonneveknél, cégneveknél és szakkifejezéseknél "
            "a szójegyzék helyes alakját preferáld.\n"
            "5. A terjedelem és a részletesség legyen ARÁNYOS a beszélgetés tényleges tartalmával, "
            "NEM az időtartamával. Egy hosszú, de kötetlen vagy információ-szegény beszélgetés "
            "(pl. játék, csevegés) RÖVID összefoglalót kapjon; egy információ-intenzív megbeszélés "
            "részletesebbet. Ne tölts ki egy mezőt sem csak azért, hogy hosszabb legyen.\n"
            "6. NE TALÁLJ KI döntéseket, teendőket vagy résztvevőket. Ha nincs valódi döntés vagy "
            "teendő, hagyd ÜRESEN a megfelelő tömböt (az üres tömb teljesen rendben van). Kizárólag "
            "azt rögzítsd, ami ténylegesen elhangzott.\n");
    }

    if (id == QStringLiteral("topic")) {
        return QStringLiteral(
            "Te egy elemző vagy. A kapott beszéd-átiratból azonosítsd a KÜLÖNÁLLÓ "
            "TÉMÁKAT (témakörök, amelyekről ténylegesen szó volt).\n"
            "KIMENETI FORMÁTUM — pontosan ez, semmi más (se bevezető, se JSON, se kódkerítés): "
            "minden témát egy `## ` kezdetű sor vezet be a téma rövid CÍMÉVEL, alatta 1-2 mondatos "
            "összegzés a témáról. Példa:\n"
            "## Szállítási határidők\n"
            "A csapat egyeztette a Q3-as csúszást és a pótlási tervet.\n\n"
            "## Költségkeret\n"
            "Áttekintették a keret túllépését és a fedezeti lehetőségeket.\n\n"
            "Szabályok:\n"
            "1. A témák száma legyen ARÁNYOS a tartalommal: kötetlen/információ-szegény "
            "beszélgetésnél kevés téma (akár 1), információ-intenzív megbeszélésnél több. Ne darabolj "
            "túl, és NE találj ki nem létező témát.\n"
            "2. Csak a `## Cím` + összegzés blokkokat add vissza, mást ne.\n"
            "3. Minden szöveg {{NYELV}} nyelven.\n");
    }

    if (id == QStringLiteral("analysis")) {
        return QStringLiteral(
            "Te egy precíz jegyzetelő vagy. A kapott TELJES átiratból KIZÁRÓLAG a "
            "megadott TÉMÁRA vonatkozó részeket elemezd.\n"
            "KIMENETI FORMÁTUM — markdown, pontosan így (se JSON, se kódkerítés):\n"
            "Először 1 bekezdés összegzés a témáról (cím nélkül). Utána — CSAK ha van valódi tartalom "
            "— ezek a szakaszok jöhetnek:\n"
            "## Döntések\n"
            "- egy döntés soronként\n"
            "## Teendők\n"
            "- a teendő szövege — Felelős (határidő)\n"
            "(A felelős és a határidő rész opcionális; ha nincs rá adat, hagyd el.)\n"
            "Szabályok:\n"
            "1. NE TALÁLJ KI semmit. Ha a témához nincs valódi döntés vagy teendő, hagyd EL az adott "
            "szakaszt (ne írj üres címet). Csak a megadott témára fókuszálj.\n"
            "2. Minden szöveg {{NYELV}} nyelven — de a `## Döntések` és `## Teendők` "
            "szakaszcímek PONTOSAN így, magyarul maradnak.\n");
    }

    if (id == QStringLiteral("reduce")) {
        // SZŰK feladat: KIZÁRÓLAG egy rövid vezetői összefoglaló — a teendők összevonását
        // NEM az LLM végzi (azt a kód deduplikálja a per-téma elemzésekből), így nincs mit
        // „hangosan gondolkodnia", és a kimenet modellfüggetlenül stabil marad.
        return QStringLiteral(
            "Te egy precíz jegyzetelő vagy. A kapott témánkénti elemzésekből írj "
            "EGYETLEN, 2-4 mondatos GLOBÁLIS vezetői összefoglalót az egész beszélgetésről.\n"
            "KIZÁRÓLAG ezt a bekezdést add vissza — semmi mást: se cím, se felsorolás, se teendők, "
            "se döntések, se JSON, se kódkerítés, se magyarázat, se gondolatmenet. "
            "{{NYELV}} nyelven.\n");
    }

    return QString();
}

QString promptFilePath(const QString& id, const QString& metadataDir)
{
    const QString dir = paths::resolveMetadataDir(metadataDir);   // TANARA_HOME-tudatos
    return QDir(dir).filePath(QStringLiteral("prompts/%1.md").arg(id));
}

QString promptDefault(const QString& id, const QString& metadataDir)
{
    QFile f(promptFilePath(id, metadataDir));
    if (f.exists() && f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString text = QString::fromUtf8(f.readAll()).trimmed();
        if (!text.isEmpty())
            return text;
    }
    return promptBuiltin(id);
}

QString applySummaryLanguage(QString prompt, const QString& language)
{
    QString lang = language.trimmed();
    if (lang.isEmpty())
        lang = QStringLiteral("magyar");

    if (prompt.contains(QStringLiteral("{{NYELV}}")))
        return prompt.replace(QStringLiteral("{{NYELV}}"), lang);

    // Placeholder nélküli (saját/fájl) prompt: magyar célnyelvnél nem nyúlunk hozzá
    // (visszafelé kompatibilis), más célnyelvnél direktívát fűzünk a végére.
    if (lang.compare(QStringLiteral("magyar"), Qt::CaseInsensitive) == 0)
        return prompt;
    return prompt + QStringLiteral(
        "\nFONTOS: a kimenet szövege KIZÁRÓLAG %1 nyelven íródjon. A strukturális "
        "szakaszcímek (pl. `## Döntések`, `## Teendők`) változatlanul magyarul maradnak.\n")
        .arg(lang);
}

QVector<PromptVariable> promptVariables()
{
    return {
        { QStringLiteral("{{NYELV}}"),
          QCoreApplication::translate("PromptLibrary", "az összefoglaló nyelve") },
    };
}

PromptOutputFormat promptOutputFormat(const QString& id)
{
    PromptOutputFormat f;
    if (id == QStringLiteral("simple")) {
        f.kind = QStringLiteral("json");
        f.summary = QStringLiteral("execSummary, decisions[], actionItems[], participants[]");
        f.body = QStringLiteral(
            "{\n"
            "  \"execSummary\": string,\n"
            "  \"decisions\": [ string, … ],\n"
            "  \"actionItems\": [\n"
            "    { \"text\": string, \"owner\": string, \"due\": string }, …\n"
            "  ],\n"
            "  \"participants\": [ string, … ]\n"
            "}");
    } else if (id == QStringLiteral("topic")) {
        f.kind = QStringLiteral("markdown");
        f.summary = QCoreApplication::translate("PromptLibrary", "## Cím + 1–2 mondat, témánként");
        f.body = QCoreApplication::translate("PromptLibrary",
            "## <a téma címe>\n"
            "<1–2 mondatos összegzés>\n"
            "\n"
            "## <a következő téma címe>\n"
            "<1–2 mondatos összegzés>");
    } else if (id == QStringLiteral("analysis")) {
        f.kind = QStringLiteral("markdown");
        f.summary = QCoreApplication::translate("PromptLibrary",
                                                "összegzés, ## Döntések, ## Teendők");
        f.body = QCoreApplication::translate("PromptLibrary",
            "<egy bekezdés összegzés a témáról>\n"
            "\n"
            "## Döntések\n"
            "- <egy döntés soronként>\n"
            "\n"
            "## Teendők\n"
            "- <a teendő szövege> — <felelős> (<határidő>)");
    } else if (id == QStringLiteral("reduce")) {
        f.kind = QStringLiteral("text");
        f.summary = QCoreApplication::translate("PromptLibrary", "egy bekezdés");
        f.body = QCoreApplication::translate("PromptLibrary", "<2–4 mondatos vezetői összefoglaló>");
    }
    return f;
}


} // namespace tanara
