#pragma once
//
// Learning question for person tags: the shared texts of the "Who was there?" post-approval
// bar (ParticipantsViewModel) and the editor's change bar (new person; TranscriptEditorViewModel).
// The logic is in the core: TagService::learningQuestions.
//
#include "tanara/tags/PersonTags.h"

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVector>

namespace tanara_qml::taglearning {

// "A", "A and B", "A, B and C".
QString nameList(const QStringList& names);
// "Alex Smith is a new person. Add the #Nordvik tag?"
QString questionText(const tanara::PersonTagQuestion& q);
// Feedback of the attach / reject (toast with "Undo").
QString answeredText(const tanara::PersonTagQuestion& q, bool accepted);
// The "One by one..." list: every (person, tag) pair as a separate question, in order.
QVector<tanara::PersonTagQuestion> pairs(const QVector<tanara::PersonTagQuestion>& questions);
// The pairs for QML: [{ index, name, tagName, text }].
QVariantList pairMaps(const QVector<tanara::PersonTagQuestion>& pairs);

} // namespace tanara_qml::taglearning
