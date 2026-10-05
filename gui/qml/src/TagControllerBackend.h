#pragma once
//
// TagControllerBackend — a TagBackend varrat valódi megvalósítása: tanara::AppController +
// tanara::TagService (CONTRACT-TAGS.md, „Core API”).
//
// Szálkezelés: minden hívás és minden jel a GUI-szálon fut. A core a profilokat háttérszálon
// építi, de a javaslat-jeleket (tagSuggestionsComputing → tagSuggestionsReady) a GUI-szálon
// adja ki; ez az osztály csak továbbítja őket.
//
// Jelsorrend: egy címke felrakásakor a TagService::meetingTagsChanged-re ELŐBB a controller
// adja ki újra a szűrt javaslat-listát (tagSuggestionsReady → suggestionsChanged), utána jön a
// továbbított meetingTagsChanged — a nézetmodell mindkettőre frissít, a sorrend nem számít.
//
// A javaslat-állapot: meetingenként az utolsó FRISS lista (amely előtt Computing jött). A core
// felrakás / elutasítás után a szűrt listát újra kiadja (Computing nélkül); ezt a backend nem
// veszi át, ha a korábbi része — a szűrést a nézetmodell végzi, így a visszavonás után a
// javaslat visszajön. Ami még nem jött ide, azt a controller pendingTagSuggestions()-éből adja
// (a később csatlakozó nézetmodellnek). A `computing` jelző a Computing jeltől a következő
// Ready-ig igaz; a törölt címkére mutató elem kimarad.
//
#include "TagBackend.h"

#include "tanara/tags/TagTypes.h"

#include <QHash>
#include <QPointer>
#include <QSet>

namespace tanara {
class AppController;
class TagService;
} // namespace tanara

namespace tanara_qml {

class TagControllerBackend : public TagBackend {
public:
    TagControllerBackend(tanara::AppController* controller, QObject* parent = nullptr);

    QVector<TagItem> tags() const override;
    QVector<TagItem> recent(int limit) const override;
    QStringList tagsOf(const QString& meetingId) const override;
    QString addTag(const QString& meetingId, const QString& nameOrId, TagAddSource source) override;
    void removeTag(const QString& meetingId, const QString& tagId) override;
    TagSuggestionState suggestions(const QString& meetingId) const override;
    void requestSuggestions(const QString& meetingId) override;
    void requestCooccur(const QString& meetingId, const QString& tagId) override;
    void reject(const QString& meetingId, const TagSuggestionItem& suggestion) override;
    bool isRejected(const QString& meetingId, const QString& tagIdOrName) const override;
    TagProfileItem profile(const QString& tagId) const override;
    bool rename(const QString& tagId, const QString& name) override;
    void merge(const QString& fromId, const QString& keepId) override;
    void remove(const QString& tagId) override;
    void beginGroup(const QString& label) override;
    void endGroup() override;
    bool canUndo() const override;
    QString undoLabel() const override;
    void undo() override;

    // Vázlat (még nem létező megbeszélés, pl. átirat előtti lépés): cím alapján, szinkron.
    QVector<TagSuggestionItem> draftSuggestions(const QString& title) const override;

    // core → UI átalakítás (tesztekhez és a vázlat-javaslatokhoz is).
    static TagSuggestionItem toItem(const tanara::TagSuggestion& s);

private:
    tanara::TagService* service() const;

    QPointer<tanara::AppController> m_controller;
    QSet<QString> m_computing;   // meetingId-k, amelyekre a Computing után még nem jött Ready
    QHash<QString, QVector<tanara::TagSuggestion>> m_lists;   // meetingId → utolsó friss lista
};

} // namespace tanara_qml
