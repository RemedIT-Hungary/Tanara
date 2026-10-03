#include "tanara/summary/SummaryStore.h"
#include "tanara/library/TextFold.h"
#include "tanara/store/JsonSerialization.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>

namespace tanara {
namespace summarystore {

namespace {

constexpr int kVersion = 1;

QJsonObject topicToJson(const TopicAnalysis& a)
{
    QJsonObject o;
    o[QStringLiteral("topicId")] = a.topicId;
    o[QStringLiteral("title")]   = a.title;
    o[QStringLiteral("detail")]  = a.detail;
    o[QStringLiteral("decisions")] = QJsonArray::fromStringList(a.decisions);
    QJsonArray items;
    for (const ActionItem& ai : a.actionItems) items.append(tanara::toJson(ai));
    o[QStringLiteral("actionItems")] = items;
    return o;
}

TopicAnalysis topicFromJson(const QJsonObject& o)
{
    TopicAnalysis a;
    a.topicId = o.value(QStringLiteral("topicId")).toString();
    a.title   = o.value(QStringLiteral("title")).toString();
    a.detail  = o.value(QStringLiteral("detail")).toString();
    for (const QJsonValue& v : o.value(QStringLiteral("decisions")).toArray()) a.decisions << v.toString();
    for (const QJsonValue& v : o.value(QStringLiteral("actionItems")).toArray())
        a.actionItems.append(actionItemFromJson(v.toObject()));
    return a;
}

// Egy teendő-sor törzse („szöveg — Felelős (határidő)”) → ActionItem. A renderelt formát
// (em dash + záró zárójel) olvassa vissza; a gondolatjel en dash-sel is elfogadott.
ActionItem parseActionItem(QString line)
{
    static const QRegularExpression dueRe(QStringLiteral("\\s*\\(([^()]*)\\)\\s*$"));
    ActionItem ai;
    line = line.trimmed();
    const QRegularExpressionMatch dm = dueRe.match(line);
    if (dm.hasMatch()) {
        ai.due = dm.captured(1).trimmed();
        line = line.left(dm.capturedStart()).trimmed();
    }
    for (const QString& sep : {QStringLiteral(" — "), QStringLiteral(" – ")}) {
        const int i = line.lastIndexOf(sep);
        if (i > 0) {
            ai.owner = line.mid(i + sep.size()).trimmed();
            line = line.left(i).trimmed();
            break;
        }
    }
    ai.text = line;
    return ai;
}

enum class Section { Exec, Decisions, Actions, Participants, Topics, Unknown };
enum class TopicPart { Detail, Decisions, Actions };

Section classifyHeading(const QString& heading)
{
    const QString h = textfold::foldQuery(heading);
    if (h.contains(QStringLiteral("dontes")))                       return Section::Decisions;
    if (h.contains(QStringLiteral("teendo")) || h.contains(QStringLiteral("feladat")))
                                                                    return Section::Actions;
    if (h.contains(QStringLiteral("resztvevo")))                    return Section::Participants;
    if (h.startsWith(QStringLiteral("tema")))                       return Section::Topics;
    if (h.contains(QStringLiteral("osszefoglal")) || h.contains(QStringLiteral("vezetoi")))
                                                                    return Section::Exec;
    return Section::Unknown;
}

void appendParagraph(QString& target, const QStringList& lines)
{
    const QString p = lines.join(QLatin1Char('\n')).trimmed();
    if (p.isEmpty()) return;
    if (!target.isEmpty()) target += QStringLiteral("\n\n");
    target += p;
}

} // namespace

QString jsonPath(const QString& meetingFolder)
{
    return QDir(meetingFolder).filePath(QStringLiteral("summary.json"));
}

QString markdownPath(const QString& meetingFolder)
{
    return QDir(meetingFolder).filePath(QStringLiteral("summary.md"));
}

QString modeToString(SummaryMode m)
{
    switch (m) {
    case SummaryMode::Quick:   return QStringLiteral("quick");
    case SummaryMode::Topics:  return QStringLiteral("topics");
    case SummaryMode::Unknown: break;
    }
    return QStringLiteral("unknown");
}

SummaryMode modeFromString(const QString& s)
{
    if (s == QLatin1String("quick"))  return SummaryMode::Quick;
    if (s == QLatin1String("topics")) return SummaryMode::Topics;
    return SummaryMode::Unknown;
}

SummaryDocument parseMarkdown(const QString& markdown)
{
    static const QRegularExpression h12(QStringLiteral("^\\s{0,3}#{1,2}\\s+(.+?)\\s*#*\\s*$"));
    static const QRegularExpression h3(QStringLiteral("^\\s{0,3}#{3,6}\\s+(.+?)\\s*#*\\s*$"));
    static const QRegularExpression numPrefix(QStringLiteral("^\\d+[.)]\\s*"));
    static const QRegularExpression bullet(QStringLiteral("^\\s*(?:[-*+•]|\\d+[.)])\\s+(.*)$"));
    static const QRegularExpression checkbox(QStringLiteral("^\\[[ xX]\\]\\s*"));
    static const QRegularExpression boldLabel(QStringLiteral("^\\*\\*(.+?):?\\*\\*:?\\s*$"));

    SummaryDocument doc;
    doc.markdown = markdown;
    doc.fromMarkdown = true;
    doc.exists = !markdown.trimmed().isEmpty();
    doc.meta.mode = SummaryMode::Quick;

    Section sec = Section::Exec;
    TopicPart part = TopicPart::Detail;
    QStringList para;            // a futó bekezdés sorai (Exec / Unknown / téma-detail)
    QString unknownHeading;      // ismeretlen szakasz címe (a szöveg elé kerül)
    bool inTopic = false;

    auto flushPara = [&]() {
        if (para.isEmpty()) return;
        if (sec == Section::Topics && inTopic) {
            appendParagraph(doc.topics.last().detail, para);
        } else if (sec == Section::Exec || sec == Section::Unknown || sec == Section::Topics) {
            if (!unknownHeading.isEmpty()) {
                para.prepend(unknownHeading);
                unknownHeading.clear();
            }
            appendParagraph(doc.summary.execSummary, para);
        }
        para.clear();
    };

    const QStringList lines = markdown.split(QLatin1Char('\n'));
    for (const QString& rawLine : lines) {
        QString line = rawLine;
        if (line.endsWith(QLatin1Char('\r'))) line.chop(1);
        const QString t = line.trimmed();

        const QRegularExpressionMatch m12 = h12.match(line);
        if (m12.hasMatch()) {
            flushPara();
            inTopic = false;
            sec = classifyHeading(m12.captured(1));
            unknownHeading = (sec == Section::Unknown) ? m12.captured(1).trimmed() : QString();
            if (sec == Section::Topics) doc.meta.mode = SummaryMode::Topics;
            continue;
        }
        const QRegularExpressionMatch m3 = h3.match(line);
        if (m3.hasMatch()) {
            flushPara();
            if (sec == Section::Topics) {
                TopicAnalysis a;
                QString title = m3.captured(1).trimmed();
                title.remove(numPrefix);
                a.title = title.trimmed();
                doc.topics.append(a);
                inTopic = true;
                part = TopicPart::Detail;
            } else {
                // Al-címsor egy sima szakaszban: a tartalma a szakaszé marad; a prózai
                // szakaszokban a cím szövegként megmarad.
                if (sec == Section::Exec || sec == Section::Unknown) para << m3.captured(1).trimmed();
            }
            continue;
        }

        if (t.isEmpty()) { flushPara(); continue; }

        // Téma-szekción belül: **Döntések:** / **Teendők:** al-címkék.
        if (sec == Section::Topics && inTopic) {
            const QRegularExpressionMatch bl = boldLabel.match(t);
            if (bl.hasMatch()) {
                const Section s = classifyHeading(bl.captured(1));
                if (s == Section::Decisions) { flushPara(); part = TopicPart::Decisions; continue; }
                if (s == Section::Actions)   { flushPara(); part = TopicPart::Actions;   continue; }
            }
            TopicAnalysis& a = doc.topics.last();
            const QRegularExpressionMatch bm = bullet.match(line);
            if (part == TopicPart::Decisions && bm.hasMatch()) {
                a.decisions << bm.captured(1).trimmed();
            } else if (part == TopicPart::Actions && bm.hasMatch()) {
                QString body = bm.captured(1).trimmed();
                body.remove(checkbox);
                const ActionItem ai = parseActionItem(body);
                if (!ai.text.isEmpty()) a.actionItems << ai;
            } else {
                if (part != TopicPart::Detail && !bm.hasMatch()) part = TopicPart::Detail;
                para << t;
            }
            continue;
        }

        const QRegularExpressionMatch bm = bullet.match(line);
        switch (sec) {
        case Section::Decisions:
            if (bm.hasMatch()) doc.summary.decisions << bm.captured(1).trimmed();
            else if (!doc.summary.decisions.isEmpty())      // tördelt sor folytatása
                doc.summary.decisions.last() += QLatin1Char(' ') + t;
            else doc.summary.decisions << t;
            break;
        case Section::Actions: {
            if (!bm.hasMatch()) {
                if (!doc.summary.actionItems.isEmpty())
                    doc.summary.actionItems.last().text += QLatin1Char(' ') + t;
                break;
            }
            QString body = bm.captured(1).trimmed();
            body.remove(checkbox);
            const ActionItem ai = parseActionItem(body);
            if (!ai.text.isEmpty()) doc.summary.actionItems << ai;
            break;
        }
        case Section::Participants: {
            const QString body = bm.hasMatch() ? bm.captured(1) : t;
            for (const QString& p : body.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
                const QString n = p.trimmed();
                if (!n.isEmpty() && !doc.summary.participants.contains(n))
                    doc.summary.participants << n;
            }
            break;
        }
        case Section::Exec:
        case Section::Unknown:
        case Section::Topics:      // „## Témák” alatti, téma-cím előtti szöveg
            para << t;
            break;
        }
    }
    flushPara();

    // Témánkénti mód: a felső szinten nincs „Döntések” szakasz — a témák döntéseinek uniója.
    if (doc.meta.mode == SummaryMode::Topics && doc.summary.decisions.isEmpty()) {
        for (const TopicAnalysis& a : std::as_const(doc.topics))
            for (const QString& d : a.decisions)
                if (!doc.summary.decisions.contains(d)) doc.summary.decisions << d;
    }
    return doc;
}

QJsonObject toJson(const SummaryDocument& doc)
{
    QJsonObject o;
    o[QStringLiteral("version")]   = kVersion;
    o[QStringLiteral("mode")]      = modeToString(doc.meta.mode);
    o[QStringLiteral("createdAt")] = doc.meta.createdAt.toString(Qt::ISODate);
    o[QStringLiteral("providerId")] = doc.meta.providerId;
    o[QStringLiteral("model")]     = doc.meta.model;
    o[QStringLiteral("summary")]   = tanara::toJson(doc.summary);
    if (!doc.topics.isEmpty()) {
        QJsonArray arr;
        for (const TopicAnalysis& a : doc.topics) arr.append(topicToJson(a));
        o[QStringLiteral("topics")] = arr;
    }
    return o;
}

SummaryDocument fromJson(const QJsonObject& o)
{
    SummaryDocument doc;
    doc.exists = true;
    doc.meta.mode       = modeFromString(o.value(QStringLiteral("mode")).toString());
    doc.meta.createdAt  = QDateTime::fromString(o.value(QStringLiteral("createdAt")).toString(), Qt::ISODate);
    doc.meta.providerId = o.value(QStringLiteral("providerId")).toString();
    doc.meta.model      = o.value(QStringLiteral("model")).toString();
    doc.summary = summaryFromJson(o.value(QStringLiteral("summary")).toObject());
    for (const QJsonValue& v : o.value(QStringLiteral("topics")).toArray())
        doc.topics.append(topicFromJson(v.toObject()));
    return doc;
}

SummaryDocument load(const QString& meetingFolder)
{
    SummaryDocument doc;
    if (meetingFolder.isEmpty()) return doc;

    QString markdown;
    const QFileInfo mdInfo(markdownPath(meetingFolder));
    if (mdInfo.isFile()) {
        QFile f(mdInfo.absoluteFilePath());
        if (f.open(QIODevice::ReadOnly)) markdown = QString::fromUtf8(f.readAll());
    }

    const QFileInfo jsInfo(jsonPath(meetingFolder));
    bool haveJson = false;
    if (jsInfo.isFile()) {
        QFile f(jsInfo.absoluteFilePath());
        QJsonParseError perr{};
        const QJsonDocument jd = f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll(), &perr)
                                                            : QJsonDocument();
        if (jd.isObject() && jd.object().contains(QStringLiteral("summary"))) {
            doc = fromJson(jd.object());
            haveJson = true;
        }
    }

    if (haveJson) {
        doc.markdown = markdown;
        // Kézzel szerkesztett summary.md (érdemben újabb a json-nál) → a markdown az igazság;
        // a keletkezés metaadatai a json-ból maradnak.
        if (!markdown.trimmed().isEmpty()
            && mdInfo.lastModified() > jsInfo.lastModified().addSecs(2)) {
            SummaryDocument md = parseMarkdown(markdown);
            const SummaryMode parsedMode = md.meta.mode;
            md.meta = doc.meta;
            if (md.meta.mode == SummaryMode::Unknown) md.meta.mode = parsedMode;
            // A markdownban nincs témaazonosító — cím szerint visszakötjük a json-ból.
            for (TopicAnalysis& a : md.topics)
                for (const TopicAnalysis& old : std::as_const(doc.topics))
                    if (old.title == a.title) { a.topicId = old.topicId; break; }
            doc = md;
        }
        return doc;
    }

    if (markdown.trimmed().isEmpty())
        return doc;                      // nincs összefoglaló
    doc = parseMarkdown(markdown);
    doc.meta.createdAt = mdInfo.lastModified();
    // Régi összefoglaló: a „## Témák” szakasz megléte megbízhatóan jelzi a témánkénti módot;
    // nélküle gyors összefoglalónak tekintjük (a parseMarkdown ezt állítja be).
    return doc;
}

bool save(const QString& meetingFolder, const SummaryDocument& doc)
{
    if (meetingFolder.isEmpty() || !QDir(meetingFolder).exists()) return false;
    QSaveFile f(jsonPath(meetingFolder));
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(toJson(doc)).toJson(QJsonDocument::Indented));
    return f.commit();
}

} // namespace summarystore
} // namespace tanara
