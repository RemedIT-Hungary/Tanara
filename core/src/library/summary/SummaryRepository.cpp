#include "tanara/library/summary/SummaryRepository.h"
#include "tanara/library/MeetingLayout.h"
#include "tanara/library/MeetingNotes.h"
#include "tanara/library/summary/SummaryFormat.h"
#include "tanara/library/summary/detail/SummaryJson.h"
#include "tanara/foundation/text/Text.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>

namespace tanara::library {

namespace {

// 2: openQuestions + memo, per-topic openQuestions; 3: statements + sourceSpeakers + memo.speakers;
// 4: memo points, intros and open questions are statements too (q / i / m), memo.intro + pointGroups
constexpr int kVersion = 4;

QJsonObject topicToJson(const TopicAnalysis& a)
{
    QJsonObject o;
    o[QStringLiteral("topicId")] = a.topicId;
    o[QStringLiteral("title")]   = a.title;
    o[QStringLiteral("detail")]  = a.detail;
    o[QStringLiteral("decisions")] = QJsonArray::fromStringList(a.decisions);
    if (!a.openQuestions.isEmpty())
        o[QStringLiteral("openQuestions")] = QJsonArray::fromStringList(a.openQuestions);
    QJsonArray items;
    for (const ActionItem& ai : a.actionItems) items.append(tanara::library::toJson(ai));
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
    for (const QJsonValue& v : o.value(QStringLiteral("openQuestions")).toArray()) a.openQuestions << v.toString();
    for (const QJsonValue& v : o.value(QStringLiteral("actionItems")).toArray())
        a.actionItems.append(actionItemFromJson(v.toObject()));
    return a;
}

// The body of an action-item line ("text - Owner (deadline)") -> ActionItem. It reads back the rendered
// form (em dash + closing parenthesis); an en dash is accepted as the dash too.
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

enum class Section { Exec, Decisions, Open, Actions, Participants, Topics, Memo, Unknown };
enum class TopicPart { Detail, Decisions, Open, Actions };

Section classifyHeading(const QString& heading)
{
    const QString h = text::foldQuery(heading);
    if (h.startsWith(QStringLiteral("nyitott")))                    return Section::Open;
    if (h.startsWith(QStringLiteral("memo")))                       return Section::Memo;
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

QString SummaryRepository::jsonPath(const QString& meetingFolder)
{
    return QDir(meetingFolder).filePath(MeetingLayout::summaryJson());
}

QString SummaryRepository::markdownPath(const QString& meetingFolder)
{
    return QDir(meetingFolder).filePath(MeetingLayout::summaryMd());
}

QString SummaryRepository::modeToString(SummaryMode m)
{
    switch (m) {
    case SummaryMode::Quick:   return QStringLiteral("quick");
    case SummaryMode::Topics:  return QStringLiteral("topics");
    case SummaryMode::Unknown: break;
    }
    return QStringLiteral("unknown");
}

SummaryMode SummaryRepository::modeFromString(const QString& s)
{
    if (s == QLatin1String("quick"))  return SummaryMode::Quick;
    if (s == QLatin1String("topics")) return SummaryMode::Topics;
    return SummaryMode::Unknown;
}

SummaryDocument SummaryRepository::parseMarkdown(const QString& markdown)
{
    static const QRegularExpression h12(QStringLiteral("^\\s{0,3}#{1,2}\\s+(.+?)\\s*#*\\s*$"));
    static const QRegularExpression h3(QStringLiteral("^\\s{0,3}#{3,6}\\s+(.+?)\\s*#*\\s*$"));
    static const QRegularExpression numPrefix(QStringLiteral("^\\d+[.)]\\s*"));
    static const QRegularExpression bullet(QStringLiteral("^\\s*(?:[-*+•]|\\d+[.)])\\s+(.*)$"));
    static const QRegularExpression checkbox(QStringLiteral("^\\[[ xX]\\]\\s*"));
    static const QRegularExpression boldLabel(QStringLiteral("^\\*\\*(.+?):?\\*\\*:?\\s*$"));
    static const QRegularExpression memoRange(
        QStringLiteral("\\s*\\((\\d{1,3}:\\d{2}(?::\\d{2})?)(?:\\s*[–—-]\\s*(\\d{1,3}:\\d{2}(?::\\d{2})?))?\\)\\s*$"));

    SummaryDocument doc;
    doc.markdown = markdown;
    doc.fromMarkdown = true;
    doc.exists = !markdown.trimmed().isEmpty();
    doc.meta.mode = SummaryMode::Quick;

    Section sec = Section::Exec;
    TopicPart part = TopicPart::Detail;
    QStringList para;            // the running paragraph's lines (Exec / Unknown / topic detail)
    QString unknownHeading;      // title of an unknown section (goes in front of the text)
    bool inTopic = false;
    QString memoGroup;           // key of the running memo subsection ("" = the section body)
    bool memoGroupFresh = false; // the subsection has no point yet

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
            if (sec == Section::Memo) {
                // "### Title (12:30-18:05)" -> a memo section; the time frame from the title's end.
                MemoSection ms;
                QString title = m3.captured(1).trimmed();
                const QRegularExpressionMatch rm = memoRange.match(title);
                if (rm.hasMatch()) {
                    ms.startMs = summaryformat::parseTimestamp(rm.captured(1));
                    ms.endMs = rm.captured(2).isEmpty() ? -1 : summaryformat::parseTimestamp(rm.captured(2));
                    title = title.left(rm.capturedStart()).trimmed();
                }
                ms.title = title;
                doc.summary.memo.append(ms);
                memoGroup.clear();
                memoGroupFresh = false;
            } else if (sec == Section::Topics) {
                TopicAnalysis a;
                QString title = m3.captured(1).trimmed();
                title.remove(numPrefix);
                a.title = title.trimmed();
                doc.topics.append(a);
                inTopic = true;
                part = TopicPart::Detail;
            } else {
                // A subheading in a plain section: its content stays the section's; in prose sections the heading stays as text.
                if (sec == Section::Exec || sec == Section::Unknown) para << m3.captured(1).trimmed();
            }
            continue;
        }

        if (t.isEmpty()) { flushPara(); continue; }

        // Inside a topic section: the **Decisions:** / **Action items:** sublabels.
        if (sec == Section::Topics && inTopic) {
            const QRegularExpressionMatch bl = boldLabel.match(t);
            if (bl.hasMatch()) {
                const Section s = classifyHeading(bl.captured(1));
                if (s == Section::Decisions) { flushPara(); part = TopicPart::Decisions; continue; }
                if (s == Section::Open)      { flushPara(); part = TopicPart::Open;      continue; }
                if (s == Section::Actions)   { flushPara(); part = TopicPart::Actions;   continue; }
            }
            TopicAnalysis& a = doc.topics.last();
            const QRegularExpressionMatch bm = bullet.match(line);
            if (part == TopicPart::Decisions && bm.hasMatch()) {
                a.decisions << bm.captured(1).trimmed();
            } else if (part == TopicPart::Open && bm.hasMatch()) {
                a.openQuestions << bm.captured(1).trimmed();
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
            else if (!doc.summary.decisions.isEmpty())      // continuation of a wrapped line
                doc.summary.decisions.last() += QLatin1Char(' ') + t;
            else doc.summary.decisions << t;
            break;
        case Section::Open:
            if (bm.hasMatch()) doc.summary.openQuestions << bm.captured(1).trimmed();
            else if (!doc.summary.openQuestions.isEmpty())
                doc.summary.openQuestions.last() += QLatin1Char(' ') + t;
            else doc.summary.openQuestions << t;
            break;
        case Section::Memo: {
            if (doc.summary.memo.isEmpty()) doc.summary.memo.append(MemoSection{});
            MemoSection& ms = doc.summary.memo.last();
            QStringList& pts = ms.points;
            // "**Arguments made:**" / "**Open question:**" -> a subsection.
            const QRegularExpressionMatch bl = boldLabel.match(t);
            if (bl.hasMatch()) {
                memoGroup = summaryformat::memoGroupKey(bl.captured(1));
                memoGroupFresh = true;
                break;
            }
            auto addPoint = [&](const QString& p) {
                if (!memoGroup.isEmpty())
                    while (ms.pointGroups.size() < pts.size()) ms.pointGroups << QString();
                pts << p;
                if (!memoGroup.isEmpty() || !ms.pointGroups.isEmpty()) ms.pointGroups << memoGroup;
                memoGroupFresh = false;
            };
            if (bm.hasMatch()) addPoint(bm.captured(1).trimmed());
            else if (pts.isEmpty() && memoGroup.isEmpty())      // prose after the title: the intro
                ms.intro += (ms.intro.isEmpty() ? QString() : QStringLiteral(" ")) + t;
            else if (!pts.isEmpty() && !memoGroupFresh) pts.last() += QLatin1Char(' ') + t;
            else addPoint(t);
            break;
        }
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
        case Section::Topics:      // text under "## Topics", before a topic title
            para << t;
            break;
        }
    }
    flushPara();

    // Per-topic mode: there is no "Decisions" section at the top level; it is the union of the topics' decisions.
    if (doc.meta.mode == SummaryMode::Topics && doc.summary.decisions.isEmpty()) {
        for (const TopicAnalysis& a : std::as_const(doc.topics))
            for (const QString& d : a.decisions)
                if (!doc.summary.decisions.contains(d)) doc.summary.decisions << d;
    }
    if (doc.meta.mode == SummaryMode::Topics && doc.summary.openQuestions.isEmpty()) {
        for (const TopicAnalysis& a : std::as_const(doc.topics))
            for (const QString& q : a.openQuestions)
                if (!doc.summary.openQuestions.contains(q)
                    && doc.summary.openQuestions.size() < summaryformat::kMaxOpenQuestions)
                    doc.summary.openQuestions << q;
    }
    return doc;
}

QJsonObject SummaryRepository::toJson(const SummaryDocument& doc)
{
    QJsonObject o;
    o[QStringLiteral("version")]   = kVersion;
    o[QStringLiteral("mode")]      = modeToString(doc.meta.mode);
    o[QStringLiteral("createdAt")] = doc.meta.createdAt.toString(Qt::ISODate);
    o[QStringLiteral("providerId")] = doc.meta.providerId;
    o[QStringLiteral("model")]     = doc.meta.model;
    if (doc.meta.contextNoteKnown)
        o[QStringLiteral("contextNote")] = doc.meta.contextNote;
    o[QStringLiteral("summary")]   = tanara::library::toJson(doc.summary);
    if (!doc.topics.isEmpty()) {
        QJsonArray arr;
        for (const TopicAnalysis& a : doc.topics) arr.append(topicToJson(a));
        o[QStringLiteral("topics")] = arr;
    }
    return o;
}

SummaryDocument SummaryRepository::fromJson(const QJsonObject& o)
{
    SummaryDocument doc;
    doc.exists = true;
    doc.meta.mode       = modeFromString(o.value(QStringLiteral("mode")).toString());
    doc.meta.createdAt  = QDateTime::fromString(o.value(QStringLiteral("createdAt")).toString(), Qt::ISODate);
    doc.meta.providerId = o.value(QStringLiteral("providerId")).toString();
    doc.meta.model      = o.value(QStringLiteral("model")).toString();
    // The note it was made with (absent in an old summary.json -> unknown). The watcher's old automatic
    // sentence counts as an empty note, as in meeting.json.
    doc.meta.contextNoteKnown = o.contains(QStringLiteral("contextNote"));
    doc.meta.contextNote = o.value(QStringLiteral("contextNote")).toString();
    if (meetingnotes::parseAutoCallNote(doc.meta.contextNote))
        doc.meta.contextNote.clear();
    doc.summary = summaryFromJson(o.value(QStringLiteral("summary")).toObject());
    for (const QJsonValue& v : o.value(QStringLiteral("topics")).toArray())
        doc.topics.append(topicFromJson(v.toObject()));
    return doc;
}

SummaryDocument SummaryRepository::load(const QString& meetingFolder)
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
        // A hand-edited summary.md (substantially newer than the json) -> the markdown is the truth; the
        // provenance stays from the json.
        if (!markdown.trimmed().isEmpty()
            && mdInfo.lastModified() > jsInfo.lastModified().addSecs(2)) {
            SummaryDocument md = parseMarkdown(markdown);
            const SummaryMode parsedMode = md.meta.mode;
            md.meta = doc.meta;
            if (md.meta.mode == SummaryMode::Unknown) md.meta.mode = parsedMode;
            // There is no topic id in the markdown: we bind back by title from the json.
            for (TopicAnalysis& a : md.topics)
                for (const TopicAnalysis& old : std::as_const(doc.topics))
                    if (old.title == a.title) { a.topicId = old.topicId; break; }
            // Of the source references, those stay whose text is still unchanged in the hand-edited markdown
            // (the others have nothing left to point to).
            for (const SummaryStatement& st : std::as_const(doc.summary.statements))
                if (!st.text.isEmpty() && markdown.contains(st.text)) md.summary.statements.append(st);
            if (!md.summary.statements.isEmpty()) md.summary.sourceSpeakers = doc.summary.sourceSpeakers;
            for (MemoSection& ms : md.summary.memo)
                for (const MemoSection& old : std::as_const(doc.summary.memo))
                    if (old.title == ms.title) { ms.speakers = old.speakers; break; }
            doc = md;
        }
        return doc;
    }

    if (markdown.trimmed().isEmpty())
        return doc;                      // no summary
    doc = parseMarkdown(markdown);
    doc.meta.createdAt = mdInfo.lastModified();
    // Old summary: the presence of the "## Topics" section reliably signals the per-topic mode;
    // without it we treat it as a quick summary (parseMarkdown sets this).
    return doc;
}

bool SummaryRepository::save(const QString& meetingFolder, const SummaryDocument& doc)
{
    if (meetingFolder.isEmpty() || !QDir(meetingFolder).exists()) return false;
    QSaveFile f(jsonPath(meetingFolder));
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(toJson(doc)).toJson(QJsonDocument::Indented));
    return f.commit();
}

bool SummaryRepository::setStatementFlagged(const QString& meetingFolder, const QString& statementId,
                                            bool flagged)
{
    if (meetingFolder.isEmpty() || statementId.isEmpty()) return false;
    const QString path = jsonPath(meetingFolder);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    QJsonObject summary = root.value(QStringLiteral("summary")).toObject();
    QJsonArray sts = summary.value(QStringLiteral("statements")).toArray();
    bool found = false;
    for (int i = 0; i < sts.size(); ++i) {
        QJsonObject so = sts.at(i).toObject();
        if (so.value(QStringLiteral("id")).toString() != statementId) continue;
        found = true;
        if (so.value(QStringLiteral("flagged")).toBool() == flagged) return true;
        if (flagged) so[QStringLiteral("flagged")] = true;
        else so.remove(QStringLiteral("flagged"));
        sts[i] = so;
        break;
    }
    if (!found) return false;
    summary[QStringLiteral("statements")] = sts;
    root[QStringLiteral("summary")] = summary;

    const QDateTime mtime = QFileInfo(path).lastModified();
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly)) return false;
    out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!out.commit()) return false;
    QFile touched(path);
    if (touched.open(QIODevice::ReadWrite))
        touched.setFileTime(mtime, QFileDevice::FileModificationTime);
    return true;
}

// ---- writing helpers ---------------------------------------------------------------------------

namespace {

// Always LF line endings (WITHOUT QIODevice::Text): on Windows the Text mode would write CRLF, but
// the readback reads raw, so a \r would end up in the text.
bool writeBytes(const QString& path, const QByteArray& data)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(data);
    return f.commit();
}

QJsonArray readArray(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).array();
}

} // namespace

bool SummaryRepository::writeMarkdown(const QString& meetingFolder, const QString& markdown)
{
    if (meetingFolder.isEmpty()) return false;
    return writeBytes(markdownPath(meetingFolder), markdown.toUtf8());
}

bool SummaryRepository::writeNoteCopy(const QString& notesDir, const QString& fileName, const QString& markdown)
{
    if (notesDir.isEmpty() || fileName.isEmpty()) return false;
    QDir().mkpath(notesDir);
    return writeBytes(QDir(notesDir).filePath(fileName), markdown.toUtf8());
}

// ---- per-topic mode files ----------------------------------------------------------------------

QVector<SummaryTopic> SummaryRepository::readTopics(const QString& meetingFolder)
{
    if (meetingFolder.isEmpty()) return {};
    return topicsFromJson(readArray(MeetingLayout::path(meetingFolder, MeetingLayout::topicsJson())));
}

bool SummaryRepository::writeTopics(const QString& meetingFolder, const QVector<SummaryTopic>& topics)
{
    if (meetingFolder.isEmpty()) return false;
    return writeBytes(MeetingLayout::path(meetingFolder, MeetingLayout::topicsJson()),
                      QJsonDocument(topicsToJson(topics)).toJson(QJsonDocument::Indented));
}

QVector<TopicAnalysis> SummaryRepository::readAnalyses(const QString& meetingFolder)
{
    if (meetingFolder.isEmpty()) return {};
    return analysesFromJson(readArray(MeetingLayout::path(meetingFolder, MeetingLayout::analysesJson())));
}

bool SummaryRepository::writeAnalyses(const QString& meetingFolder, const QVector<TopicAnalysis>& analyses)
{
    if (meetingFolder.isEmpty()) return false;
    return writeBytes(MeetingLayout::path(meetingFolder, MeetingLayout::analysesJson()),
                      QJsonDocument(analysesToJson(analyses)).toJson(QJsonDocument::Indented));
}

bool SummaryRepository::upsertAnalysis(const QString& meetingFolder, const TopicAnalysis& a)
{
    if (meetingFolder.isEmpty() || !QDir(meetingFolder).exists()) return false;
    QVector<TopicAnalysis> all = readAnalyses(meetingFolder);
    bool replaced = false;
    for (TopicAnalysis& existing : all)
        if (existing.topicId == a.topicId) { existing = a; replaced = true; break; }
    if (!replaced) all.append(a);
    return writeAnalyses(meetingFolder, all);
}

QStringList SummaryRepository::openQuestionsOf(const QVector<TopicAnalysis>& topics)
{
    QStringList out;
    for (const TopicAnalysis& a : topics)
        for (const QString& q : a.openQuestions)
            if (!out.contains(q) && out.size() < summaryformat::kMaxOpenQuestions) out << q;
    return out;
}

QString SummaryRepository::renderComplexMarkdown(const QString& execSummary, const QVector<ActionItem>& items,
                                                 const QVector<TopicAnalysis>& topics)
{
    auto renderItem = [](const ActionItem& ai) {
        QString s = QStringLiteral("- [ ] ") + ai.text;
        if (!ai.owner.isEmpty()) s += QStringLiteral(" — ") + ai.owner;
        if (!ai.due.isEmpty())   s += QStringLiteral(" (") + ai.due + QStringLiteral(")");
        return s + QStringLiteral("\n");
    };
    QString md;
    if (!execSummary.isEmpty())
        md += QStringLiteral("## Vezetői összefoglaló\n\n") + execSummary + QStringLiteral("\n\n");
    const QStringList open = openQuestionsOf(topics);
    if (!open.isEmpty()) {
        md += QStringLiteral("## Nyitott kérdések\n\n");
        for (const QString& q : open) md += QStringLiteral("- ") + q + QStringLiteral("\n");
        md += QStringLiteral("\n");
    }
    if (!items.isEmpty()) {
        md += QStringLiteral("## Teendők (összevont)\n\n");
        for (const ActionItem& ai : items) md += renderItem(ai);
        md += QStringLiteral("\n");
    }
    if (!topics.isEmpty()) {
        md += QStringLiteral("## Témák\n\n");
        int n = 1;
        for (const TopicAnalysis& t : topics) {
            md += QStringLiteral("### %1. %2\n\n").arg(n++).arg(t.title);
            md += t.renderMarkdown();   // detail + decisions + action items (domain/Summary.h)
        }
    }
    return md;
}

} // namespace tanara::library
