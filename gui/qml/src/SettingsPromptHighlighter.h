#pragma once
//
// SettingsPromptHighlighter: the prompt editor's highlighting: the `{{VARIABLE}}` markers with
// an accentSoft background and accent text. The variables the code REALLY substitutes
// (tanara::promptVariables) get the strong highlight; an unknown `{{...}}` a fainter one
// (a typo or a custom marker; the model receives it verbatim).
//
// From QML:  SettingsPromptHighlighter { document: editor.textDocument }
// The colors come from the Theme (QML binds them), so it recolors on a theme change.
//
#include <QColor>
#include <QObject>
#include <QPointer>
#include <QQuickTextDocument>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

class QSyntaxHighlighter;

namespace tanara_qml {

class SettingsPromptHighlighter : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QQuickTextDocument* document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(QColor variableColor READ variableColor WRITE setVariableColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor variableBackground READ variableBackground WRITE setVariableBackground NOTIFY colorsChanged)
    Q_PROPERTY(QColor unknownColor READ unknownColor WRITE setUnknownColor NOTIFY colorsChanged)

public:
    explicit SettingsPromptHighlighter(QObject* parent = nullptr);

    QQuickTextDocument* document() const { return m_document; }
    void setDocument(QQuickTextDocument* document);
    QColor variableColor() const { return m_variable; }
    void setVariableColor(const QColor& c);
    QColor variableBackground() const { return m_background; }
    void setVariableBackground(const QColor& c);
    QColor unknownColor() const { return m_unknown; }
    void setUnknownColor(const QColor& c);

    // Fixed line height (px) for every paragraph of the document: QML's TextEdit has no
    // lineHeight, the spec asks for 12 px x 1.65. To call after a PROGRAMMATIC text load: it
    // clears the undo stack (the formatting must not be an "undoable" step); lines typed later
    // inherit the previous paragraph's format.
    Q_INVOKABLE void applyLineHeight(int pixels);

    // A line's spans to highlight: [start, length, known]: the shared core of the highlighter and the tests.
    struct Span { int start = 0; int length = 0; bool known = false; };
    static QVector<Span> spans(const QString& line, const QStringList& knownTokens);

signals:
    void documentChanged();
    void colorsChanged();

private:
    void rehighlight();

    QPointer<QQuickTextDocument> m_document;
    QPointer<QSyntaxHighlighter> m_highlighter;
    QColor m_variable, m_background, m_unknown;
};

} // namespace tanara_qml
