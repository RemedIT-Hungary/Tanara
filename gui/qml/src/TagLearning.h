#pragma once
//
// Tanulási kérdés a személy-címkékhez (vault UX-terv 5.) — a „Ki volt ott?” jóváhagyás utáni
// sáv (ParticipantsViewModel) és a szerkesztő változás-sávja (új személy;
// TranscriptEditorViewModel) közös szövegei. A logika a magban: TagService::learningQuestions.
//
#include "tanara/tags/PersonTags.h"

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVector>

namespace tanara_qml::taglearning {

// „A”, „A és B”, „A, B és C”.
QString nameList(const QStringList& names);
// „Fehér Gábor és Varga Árpád másodszor van #Nordvik megbeszélésen. Rájuk teszem a címkét?”
// „Szabó Bence új személy. Ráteszem a #Nordvik címkét?”
QString questionText(const tanara::PersonTagQuestion& q);
// A felrakás / elutasítás visszajelzése (toast „Visszavonás”-sal).
QString answeredText(const tanara::PersonTagQuestion& q, bool accepted);
// Az „Egyenként…” lista: minden (személy, címke) pár külön kérdésként, a sorrendben.
QVector<tanara::PersonTagQuestion> pairs(const QVector<tanara::PersonTagQuestion>& questions);
// A párok QML-hez: [{ index, name, tagName, text }].
QVariantList pairMaps(const QVector<tanara::PersonTagQuestion>& pairs);

} // namespace tanara_qml::taglearning
