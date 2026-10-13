#pragma once
//
// VoiceprintStore: the global voiceprint database (~/.tanara/voiceprints.json).
// A name can have several embeddings (another microphone/condition). Matching takes the
// LARGEST cosine similarity over the person's voiceprint set. Everything is local (privacy).
//
// Several processes may write it: every modification rereads the fresh state from disk under lock
// (if it changed), applies the change to that, and writes atomically (see store/SharedFile.h).
#include "tanara/domain/Voice.h"
#include "tanara/foundation/SharedFile.h"

#include <QString>
#include <QStringList>
#include <QVector>
#include <QMap>

namespace tanara::library {

class VoiceprintStore {
public:
    explicit VoiceprintStore(const QString& filePath = QString()); // empty = ~/.tanara/voiceprints.json

    // Names that have at least one voiceprint (alphabetical).
    QStringList people() const;
    QVector<Voiceprint> printsFor(const QString& name) const;
    int printCount(const QString& name) const;
    int totalPrintCount() const;

    // Adds a voiceprint to a name. With an empty print.id it generates one; an empty print.model =
    // the base model. It sets print.dim to the embedding's size. Persists.
    void addPrint(const QString& name, Voiceprint print);
    // Deletes a voiceprint by id (from any person). true if it deleted.
    bool removePrint(const QString& printId);
    // Renames a person (the voiceprints move); if the target exists, merges.
    void renamePerson(const QString& oldName, const QString& newName);
    // Deletes all of a person's voiceprints.
    void removePerson(const QString& name);
    // Merges two people: 'from's voiceprints go to 'into', 'from' is deleted.
    void merge(const QString& from, const QString& into);

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
    QStringList printsMissingModel(const QString& name, const QString& modelId) const;

    // Sibling voiceprints: the same sample with other models (same sampleRef + sourceMeetingId +
    // createdAt, DIFFERENT model). The key ties the triple together.
    static QString siblingKey(const Voiceprint& p);
    // Voiceprints into sibling groups (one group = one sample): within the same key at most one
    // voiceprint per model; a repeat of the same model opens a new group. An empty sampleRef = its own
    // group. Groups in the order of their first member, within a group by model.
    static QVector<QVector<Voiceprint>> siblingGroups(const QVector<Voiceprint>& prints);
    // A voiceprint's sibling group at its owner (itself included); empty if there is no such id.
    QVector<Voiceprint> siblingsOf(const QString& printId, QString* owner = nullptr) const;
    // The person's samples (one representative per group: the smallest model id) that have no voiceprint
    // with the given model yet (the sibling-exact counterpart of printsMissingModel: the same span of
    // same-named files of two meetings never gets mixed up).
    QVector<Voiceprint> samplesMissingModel(const QString& name, const QString& modelId) const;
    // Number of the person's samples (sibling groups).
    int sampleCount(const QString& name) const;

    // A voiceprint and its owner by id; false if there is no such.
    bool findPrint(const QString& printId, QString* owner, Voiceprint* print) const;
    // If the file changed on disk (another process wrote it), rereads it.
    void refresh() { reloadIfChanged(); }

    QString filePath() const { return m_filePath; }

    // --- helper math (public + static, so it is testable and reusable) ---
    static double cosineSimilarity(const QVector<float>& a, const QVector<float>& b);
    static QVector<float> l2normalize(const QVector<float>& v);

private:
    void load();
    void reloadIfChanged();   // if the file changed on disk (another process wrote it)
    void persist();

    QString m_filePath;
    QMap<QString, QVector<Voiceprint>> m_people;   // name -> voiceprints
    FileStamp m_stamp;            // state of the file at the last load / save
    bool      m_corrupt = false;  // the file on disk is unparseable (set aside on save)
};

} // namespace tanara::library