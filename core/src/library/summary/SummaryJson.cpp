#include "tanara/library/summary/detail/SummaryJson.h"
#include "tanara/domain/SpeakerKeys.h"
#include "tanara/library/summary/SummaryFormat.h"

#include <QJsonValue>

namespace tanara::library {

namespace {
QJsonArray stringListToArray(const QStringList& list) { return QJsonArray::fromStringList(list); }
QStringList arrayToStringList(const QJsonArray& arr)
{
    QStringList list;
    for (const auto& v : arr) list.append(v.toString());
    return list;
}
} // namespace

// ---- ActionItem -----------------------------------------------------------
QJsonObject toJson(const ActionItem& a)
{
    QJsonObject o;
    o[QStringLiteral("text")]  = a.text;
    o[QStringLiteral("owner")] = a.owner;
    o[QStringLiteral("due")]   = a.due;
    return o;
}

ActionItem actionItemFromJson(const QJsonObject& o)
{
    ActionItem a;
    a.text  = o.value(QStringLiteral("text")).toString();
    a.owner = o.value(QStringLiteral("owner")).toString();
    a.due   = o.value(QStringLiteral("due")).toString();
    return a;
}

// ---- Summary --------------------------------------------------------------
QJsonObject toJson(const Summary& s)
{
    QJsonObject o;
    o[QStringLiteral("execSummary")] = s.execSummary;
    o[QStringLiteral("decisions")]   = stringListToArray(s.decisions);

    QJsonArray items;
    for (const auto& a : s.actionItems)
        items.append(toJson(a));
    o[QStringLiteral("actionItems")] = items;

    o[QStringLiteral("participants")] = stringListToArray(s.participants);
    o[QStringLiteral("openQuestions")] = stringListToArray(s.openQuestions);

    // The memo's sections in time order (time in ms; -1 = unknown).
    QJsonArray memo;
    for (const MemoSection& m : s.memo) {
        QJsonObject mo;
        mo[QStringLiteral("title")]   = m.title;
        mo[QStringLiteral("startMs")] = static_cast<double>(m.startMs);
        mo[QStringLiteral("endMs")]   = static_cast<double>(m.endMs);
        mo[QStringLiteral("points")]  = stringListToArray(m.points);
        if (!m.speakers.isEmpty())
            mo[QStringLiteral("speakers")] = stringListToArray(m.speakers);
        if (!m.intro.isEmpty())
            mo[QStringLiteral("intro")] = m.intro;
        if (!m.pointGroups.isEmpty())
            mo[QStringLiteral("pointGroups")] = stringListToArray(m.pointGroups);
        memo.append(mo);
    }
    o[QStringLiteral("memo")] = memo;

    // Source references: the statements (staleBecause is runtime-only, not stored) and the speaker of the
    // referenced utterances at creation time.
    if (!s.statements.isEmpty()) {
        QJsonArray sts;
        for (const SummaryStatement& st : s.statements) {
            QJsonObject so;
            so[QStringLiteral("id")]   = st.id;
            so[QStringLiteral("kind")] = summaryformat::kindToString(st.kind);
            so[QStringLiteral("text")] = st.text;
            if (!st.owner.isEmpty()) so[QStringLiteral("owner")] = st.owner;
            if (st.flagged) so[QStringLiteral("flagged")] = true;
            QJsonArray spans;
            for (const SourceSpan& sp : st.sourceSpans) {
                QJsonObject spo;
                spo[QStringLiteral("startMs")] = static_cast<double>(sp.startMs);
                spo[QStringLiteral("endMs")]   = static_cast<double>(sp.endMs);
                spo[QStringLiteral("utteranceIds")] = stringListToArray(sp.utteranceIds);
                spans.append(spo);
            }
            so[QStringLiteral("sourceSpans")] = spans;
            sts.append(so);
        }
        o[QStringLiteral("statements")] = sts;
    }
    if (!s.sourceSpeakers.isEmpty()) {
        QJsonObject ss;
        for (auto it = s.sourceSpeakers.cbegin(); it != s.sourceSpeakers.cend(); ++it) {
            QJsonObject so;
            so[QStringLiteral("key")]  = it->key;
            so[QStringLiteral("name")] = it->name;
            ss[it.key()] = so;
        }
        o[QStringLiteral("sourceSpeakers")] = ss;
    }
    return o;
}

Summary summaryFromJson(const QJsonObject& o)
{
    Summary s;
    s.execSummary = o.value(QStringLiteral("execSummary")).toString();
    s.decisions   = arrayToStringList(o.value(QStringLiteral("decisions")).toArray());

    const QJsonArray items = o.value(QStringLiteral("actionItems")).toArray();
    for (const auto& v : items)
        s.actionItems.append(actionItemFromJson(v.toObject()));

    s.participants = arrayToStringList(o.value(QStringLiteral("participants")).toArray());
    // In an old summary.json these fields are missing -> they stay empty.
    s.openQuestions = arrayToStringList(o.value(QStringLiteral("openQuestions")).toArray());
    for (const auto& v : o.value(QStringLiteral("memo")).toArray()) {
        const QJsonObject mo = v.toObject();
        MemoSection m;
        m.title   = mo.value(QStringLiteral("title")).toString();
        m.startMs = static_cast<qint64>(mo.value(QStringLiteral("startMs")).toDouble(-1));
        m.endMs   = static_cast<qint64>(mo.value(QStringLiteral("endMs")).toDouble(-1));
        m.points  = arrayToStringList(mo.value(QStringLiteral("points")).toArray());
        m.speakers = speakerkeys::normalizeSpeakerLabels(arrayToStringList(mo.value(QStringLiteral("speakers")).toArray()));
        m.intro    = mo.value(QStringLiteral("intro")).toString();          // v4
        m.pointGroups = arrayToStringList(mo.value(QStringLiteral("pointGroups")).toArray());
        if (!m.title.isEmpty() || !m.points.isEmpty() || !m.intro.isEmpty())
            s.memo.append(m);
    }
    // Old summary.json: no statements / sourceSpeakers -> empty.
    for (const auto& v : o.value(QStringLiteral("statements")).toArray()) {
        const QJsonObject so = v.toObject();
        SummaryStatement st;
        st.id      = so.value(QStringLiteral("id")).toString();
        st.kind    = summaryformat::kindFromString(so.value(QStringLiteral("kind")).toString());
        st.text    = so.value(QStringLiteral("text")).toString();
        st.owner   = so.value(QStringLiteral("owner")).toString();
        st.flagged = so.value(QStringLiteral("flagged")).toBool();
        for (const auto& sv : so.value(QStringLiteral("sourceSpans")).toArray()) {
            const QJsonObject spo = sv.toObject();
            SourceSpan sp;
            sp.startMs = static_cast<qint64>(spo.value(QStringLiteral("startMs")).toDouble(-1));
            sp.endMs   = static_cast<qint64>(spo.value(QStringLiteral("endMs")).toDouble(-1));
            sp.utteranceIds = arrayToStringList(spo.value(QStringLiteral("utteranceIds")).toArray());
            st.sourceSpans.append(sp);
        }
        if (!st.id.isEmpty()) s.statements.append(st);
    }
    const QJsonObject ss = o.value(QStringLiteral("sourceSpeakers")).toObject();
    for (auto it = ss.constBegin(); it != ss.constEnd(); ++it) {
        const QJsonObject so = it.value().toObject();
        // Legacy "Beszélő N" (key and name) -> the raw key, as the current snapshots store it.
        s.sourceSpeakers.insert(it.key(), SourceSpeaker{speakerkeys::normalizeSpeakerLabel(so.value(QStringLiteral("key")).toString()),
                                                        speakerkeys::normalizeSpeakerLabel(so.value(QStringLiteral("name")).toString())});
    }
    return s;
}

// ---- summary.topics.json ---------------------------------------------------------
QJsonArray topicsToJson(const QVector<SummaryTopic>& topics)
{
    QJsonArray arr;
    for (const auto& t : topics) {
        QJsonObject o;
        o[QStringLiteral("id")]      = t.id;
        o[QStringLiteral("title")]   = t.title;
        o[QStringLiteral("summary")] = t.summary;
        arr.append(o);
    }
    return arr;
}

QVector<SummaryTopic> topicsFromJson(const QJsonArray& arr)
{
    QVector<SummaryTopic> topics;
    for (const auto& v : arr) {
        const auto o = v.toObject();
        SummaryTopic t;
        t.id      = o.value(QStringLiteral("id")).toString();
        t.title   = o.value(QStringLiteral("title")).toString();
        t.summary = o.value(QStringLiteral("summary")).toString();
        if (!t.title.trimmed().isEmpty())
            topics.append(t);
    }
    return topics;
}

// ---- summary.analyses.json -------------------------------------------------------
QJsonArray analysesToJson(const QVector<TopicAnalysis>& analyses)
{
    QJsonArray arr;
    for (const auto& a : analyses) {
        QJsonObject o;
        o[QStringLiteral("topicId")]   = a.topicId;
        o[QStringLiteral("title")]     = a.title;
        o[QStringLiteral("detail")]    = a.detail;
        o[QStringLiteral("decisions")] = QJsonArray::fromStringList(a.decisions);
        QJsonArray items;
        for (const ActionItem& ai : a.actionItems)
            items.append(toJson(ai));
        o[QStringLiteral("actionItems")] = items;
        arr.append(o);
    }
    return arr;
}

QVector<TopicAnalysis> analysesFromJson(const QJsonArray& arr)
{
    QVector<TopicAnalysis> analyses;
    for (const auto& v : arr) {
        const auto o = v.toObject();
        TopicAnalysis a;
        a.topicId = o.value(QStringLiteral("topicId")).toString();
        a.title   = o.value(QStringLiteral("title")).toString();
        a.detail  = o.value(QStringLiteral("detail")).toString();
        for (const auto& dv : o.value(QStringLiteral("decisions")).toArray()) a.decisions << dv.toString();
        for (const auto& iv : o.value(QStringLiteral("actionItems")).toArray())
            a.actionItems.append(actionItemFromJson(iv.toObject()));
        if (!a.topicId.isEmpty())
            analyses.append(a);
    }
    return analyses;
}

} // namespace tanara::library
