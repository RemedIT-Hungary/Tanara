#pragma once
//
// Ál-backend a címke-nézetmodellek tesztjeihez: a wave 2 controller-backendjének helyén áll
// (TagBackend varrat), minden hívást naplóz, az állapotot a teszt állítja be közvetlenül.
// Kitalált adatok; fájlhoz, hálózathoz nem nyúl.
//
#include "TagBackend.h"

#include <QHash>
#include <QSet>
#include <QStringList>

// Saját jele nincs, ezért Q_OBJECT nélkül (a fejléc nem kerül az automoc elé).
class FakeTagBackend : public tanara_qml::TagBackend {
public:
    using TagBackend::TagBackend;

    QVector<tanara_qml::TagItem> items;
    QStringList recentIds;
    QHash<QString, QStringList> onMeeting;
    QHash<QString, tanara_qml::TagSuggestionState> pending;
    QSet<QString> rejected;                 // "meeting|idOrName"
    QHash<QString, tanara_qml::TagProfileItem> profiles;
    QStringList calls;
    QStringList groups;
    int depth = 0;
    QString lastLabel;
    bool renameOk = true;

    void addItem(const QString& id, const QString& name, int count, const QDateTime& last = QDateTime(QDate(2026, 9, 1), QTime(10, 0)))
    {
        items.push_back({id, name, count, last.addDays(-60), last});
    }

    QVector<tanara_qml::TagItem> tags() const override { return items; }
    QVector<tanara_qml::TagItem> recent(int limit) const override
    {
        QVector<tanara_qml::TagItem> out;
        for (const QString& id : recentIds)
            for (const auto& t : items)
                if (t.id == id && out.size() < limit) out << t;
        return out;
    }
    QStringList tagsOf(const QString& meetingId) const override { return onMeeting.value(meetingId); }
    QString addTag(const QString& meetingId, const QString& nameOrId, tanara_qml::TagAddSource source) override
    {
        const char* src = source == tanara_qml::TagAddSource::Manual ? "manual"
                        : source == tanara_qml::TagAddSource::Llm      ? "llm"
                        : source == tanara_qml::TagAddSource::Bulk     ? "bulk" : "suggestion";
        calls << QStringLiteral("add:%1:%2:%3").arg(meetingId, nameOrId, QLatin1String(src));
        QString id;
        for (const auto& t : items)
            if (t.id == nameOrId || t.name == nameOrId) id = t.id;
        if (id.isEmpty()) {
            id = QStringLiteral("new-") + nameOrId;
            addItem(id, nameOrId, 0);
        }
        if (!onMeeting[meetingId].contains(id)) onMeeting[meetingId] << id;
        emit meetingTagsChanged(meetingId);
        return id;
    }
    void removeTag(const QString& meetingId, const QString& tagId) override
    {
        calls << QStringLiteral("remove:%1:%2").arg(meetingId, tagId);
        onMeeting[meetingId].removeAll(tagId);
        emit meetingTagsChanged(meetingId);
    }
    tanara_qml::TagSuggestionState suggestions(const QString& meetingId) const override { return pending.value(meetingId); }
    void requestSuggestions(const QString& meetingId) override { calls << QStringLiteral("request:") + meetingId; }
    void requestCooccur(const QString& meetingId, const QString& tagId) override
    {
        calls << QStringLiteral("cooccur:%1:%2").arg(meetingId, tagId);
    }
    void reject(const QString& meetingId, const tanara_qml::TagSuggestionItem& s) override
    {
        calls << QStringLiteral("reject:%1:%2").arg(meetingId, s.tagId.isEmpty() ? s.name : s.tagId);
        rejected.insert(meetingId + QLatin1Char('|') + (s.tagId.isEmpty() ? s.name : s.tagId));
        emit suggestionsChanged(meetingId);
    }
    bool isRejected(const QString& meetingId, const QString& idOrName) const override
    {
        return rejected.contains(meetingId + QLatin1Char('|') + idOrName);
    }
    tanara_qml::TagProfileItem profile(const QString& tagId) const override
    {
        const_cast<FakeTagBackend*>(this)->calls << QStringLiteral("profile:") + tagId;
        return profiles.value(tagId);
    }
    bool rename(const QString& tagId, const QString& name) override
    {
        calls << QStringLiteral("rename:%1:%2").arg(tagId, name);
        if (!renameOk) return false;
        for (auto& t : items)
            if (t.id == tagId) t.name = name;
        emit tagsChanged();
        return true;
    }
    void merge(const QString& fromId, const QString& keepId) override
    {
        calls << QStringLiteral("merge:%1:%2").arg(fromId, keepId);
        for (int i = 0; i < items.size(); ++i)
            if (items[i].id == fromId) items.removeAt(i--);
        emit tagsChanged();
    }
    void remove(const QString& tagId) override
    {
        calls << QStringLiteral("delete:") + tagId;
        for (int i = 0; i < items.size(); ++i)
            if (items[i].id == tagId) items.removeAt(i--);
        emit tagsChanged();
    }
    void beginGroup(const QString& label) override
    {
        if (depth++ == 0) groups << label;
        lastLabel = label;
    }
    void endGroup() override
    {
        --depth;
        emit undoChanged();
    }
    bool canUndo() const override { return !groups.isEmpty(); }
    QString undoLabel() const override { return groups.isEmpty() ? QString() : groups.last(); }
    void undo() override
    {
        calls << QStringLiteral("undo");
        if (!groups.isEmpty()) groups.removeLast();
        emit undoChanged();
    }
};
