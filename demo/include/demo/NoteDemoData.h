#pragma once
//
// The meeting note editor's fictional template suggestions (the demo states of the pre-transcript
// view and the Summary tab): earlier occurrences of the made-up "Nordvik weekly meeting".
#include <QString>
#include <QStringList>
#include <QVector>

namespace tanara_demo {

struct DemoNoteSuggestion {
    QString title;
    QString dateText;   // already formatted for the UI language
    QString note;
    QStringList tags;
};

QVector<DemoNoteSuggestion> noteSuggestions();

} // namespace tanara_demo
