#pragma once
//
// VoiceprintStore: the global voiceprint database (<data>/voiceprints.json), keyed by PersonId.
// A person can have several embeddings (another microphone/condition). Matching takes the
// LARGEST cosine similarity over the person's voiceprint set. Everything is local (privacy).
//
// File shape (version 3): {"version": 3, "people": {"<personId>": [ {voiceprint}, ... ]}}.
// Older (name-keyed) files are NOT read: the store stays empty and read-only (legacy()) and logs that
// `tanara-cli migrate person-ids` must convert them.
//
// Several processes may write it: every modification rereads the fresh state from disk under lock
// (if it changed), applies the change to that, and writes atomically (see store/SharedFile.h).
#include "tanara/domain/Voice.h"
#include "tanara/foundation/SharedFile.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QMap>

namespace tanara::library {

class VoiceprintStore {
public:
    explicit VoiceprintStore(const QString& filePath = QString()); // empty = <data>/voiceprints.json (AppPaths)

    static constexpr int kFormatVersion = 3;

    // The people that have at least one voiceprint (by id).
    PersonIds people() const;
    QVector<Voiceprint> printsFor(const PersonId& person) const;
    int printCount(const PersonId& person) const;
    int totalPrintCount() const;

    // Adds a voiceprint to a person. With an empty print.id it generates one; an empty print.model =
    // the base model. It sets print.dim to the embedding's size. Persists.
    void addPrint(const PersonId& person, Voiceprint print);
    // Deletes a voiceprint by id (from any person). true if it deleted.
    bool removePrint(const QString& printId);
    // Deletes all of a person's voiceprints.
    void removePerson(const PersonId& person);
    // Moves every voiceprint of 'from' to 'into' (into may have none yet); 'from' is left without prints.
    // Returns the ids of the moved voiceprints.
    QStringList merge(const PersonId& from, const PersonId& into);

    // Multi-model matching: per person per model the MAX cosine over the person's voiceprints of that
    // model, then the mean of these over the shared models (of the requested modelIds, those for which the
    // query has a vector AND the person has a voiceprint). No shared model -> -1.
    // Best match; an empty DB / empty query / no shared model anywhere -> { "", -1 }.
    VoiceMatch bestMatch(const EmbeddingSet& query, const QStringList& modelIds) const;
    // Every person's score in descending order (UI/diagnostics); -1 without a shared model.
    QVector<VoiceMatch> rankedMatches(const EmbeddingSet& query, const QStringList& modelIds) const;
    // Single-model (old) form: {"campplus" -> embedding}.
    VoiceMatch bestMatch(const QVector<float>& embedding) const;
    QVector<VoiceMatch> rankedMatches(const QVector<float>& embedding) const;

    // The person's samples (sampleRef, alphabetical, unique) that have no voiceprint with the given
    // model yet (for lazy backfill). A voiceprint with an empty sampleRef does not count.
    QStringList printsMissingModel(const PersonId& person, const QString& modelId) const;

    // Sibling voiceprints: the same sample with other models (same sampleRef + sourceMeetingId +
    // createdAt, DIFFERENT model). The key ties the triple together.
    static QString siblingKey(const Voiceprint& p);
    // Voiceprints into sibling groups (one group = one sample): within the same key at most one
    // voiceprint per model; a repeat of the same model opens a new group. An empty sampleRef = its own
    // group. Groups in the order of their first member, within a group by model.
    static QVector<QVector<Voiceprint>> siblingGroups(const QVector<Voiceprint>& prints);
    // A voiceprint's sibling group at its owner (itself included); empty if there is no such id.
    QVector<Voiceprint> siblingsOf(const QString& printId, PersonId* owner = nullptr) const;
    // The person's samples (one representative per group: the smallest model id) that have no voiceprint
    // with the given model yet (the sibling-exact counterpart of printsMissingModel: the same span of
    // same-named files of two meetings never gets mixed up).
    QVector<Voiceprint> samplesMissingModel(const PersonId& person, const QString& modelId) const;
    // Number of the person's samples (sibling groups).
    int sampleCount(const PersonId& person) const;

    // A voiceprint and its owner by id; false if there is no such.
    bool findPrint(const QString& printId, PersonId* owner, Voiceprint* print) const;
    // If the file changed on disk (another process wrote it), rereads it.
    void refresh() { reloadIfChanged(); }

    QString filePath() const { return m_filePath; }
    // The file on disk is in an older (name-keyed) format: empty and read-only until converted.
    bool legacy() const { return m_legacy; }

    // --- helper math (public + static, so it is testable and reusable) ---
    static double cosineSimilarity(const QVector<float>& a, const QVector<float>& b);
    static QVector<float> l2normalize(const QVector<float>& v);

private:
    void load();
    void reloadIfChanged();   // if the file changed on disk (another process wrote it)
    void persist();

    QString m_filePath;
    QMap<PersonId, QVector<Voiceprint>> m_people;   // person -> voiceprints
    FileStamp m_stamp;            // state of the file at the last load / save
    bool      m_corrupt = false;  // the file on disk is unparseable (set aside on save)
    bool      m_legacy = false;   // old format on disk: read-only
};

// The JSON form of one voiceprint (the items of a person's array in voiceprints.json; the same shape
// in every version, so the one-time converter reuses it).
QJsonObject voiceprintToJson(const Voiceprint& vp);
Voiceprint voiceprintFromJson(const QJsonObject& o);

} // namespace tanara::library