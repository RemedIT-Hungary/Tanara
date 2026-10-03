#include "SettingsPromptHighlighter.h"

#include "tanara/PromptLibrary.h"

#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>

namespace tanara_qml {

namespace {

class Highlighter : public QSyntaxHighlighter {
public:
    Highlighter(QTextDocument* doc, SettingsPromptHighlighter* owner)
        : QSyntaxHighlighter(doc), m_owner(owner)
    {
        for (const tanara::PromptVariable& v : tanara::promptVariables()) m_known << v.token;
    }

protected:
    void highlightBlock(const QString& text) override
    {
        for (const SettingsPromptHighlighter::Span& s : SettingsPromptHighlighter::spans(text, m_known)) {
            QTextCharFormat f;
            if (s.known) {
                f.setForeground(m_owner->variableColor());
                f.setBackground(m_owner->variableBackground());
            } else {
                f.setForeground(m_owner->unknownColor());
            }
            setFormat(s.start, s.length, f);
        }
    }

private:
    SettingsPromptHighlighter* m_owner;
    QStringList m_known;
};

} // namespace

SettingsPromptHighlighter::SettingsPromptHighlighter(QObject* parent) : QObject(parent) {}

QVector<SettingsPromptHighlighter::Span> SettingsPromptHighlighter::spans(const QString& line,
                                                                          const QStringList& knownTokens)
{
    // {{NÉV}}: betűk (ékezetesek is), számok, aláhúzás — szóköz nélkül.
    static const QRegularExpression re(QStringLiteral("\\{\\{[\\p{L}\\p{N}_]+\\}\\}"),
                                       QRegularExpression::UseUnicodePropertiesOption);
    QVector<Span> out;
    QRegularExpressionMatchIterator it = re.globalMatch(line);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out.push_back({int(m.capturedStart()), int(m.capturedLength()), knownTokens.contains(m.captured())});
    }
    return out;
}

void SettingsPromptHighlighter::setDocument(QQuickTextDocument* document)
{
    if (m_document == document) return;
    if (m_highlighter) m_highlighter->deleteLater();
    m_highlighter = nullptr;
    m_document = document;
    if (m_document && m_document->textDocument())
        m_highlighter = new Highlighter(m_document->textDocument(), this);
    emit documentChanged();
}

void SettingsPromptHighlighter::applyLineHeight(int pixels)
{
    QTextDocument* doc = m_document ? m_document->textDocument() : nullptr;
    if (!doc || pixels <= 0) return;
    const bool undo = doc->isUndoRedoEnabled();
    doc->setUndoRedoEnabled(false);
    QTextCursor cursor(doc);
    cursor.select(QTextCursor::Document);
    QTextBlockFormat format;
    format.setLineHeight(pixels, QTextBlockFormat::FixedHeight);
    cursor.mergeBlockFormat(format);
    doc->setUndoRedoEnabled(undo);
}

void SettingsPromptHighlighter::rehighlight()
{
    if (m_highlighter) m_highlighter->rehighlight();
    emit colorsChanged();
}

void SettingsPromptHighlighter::setVariableColor(const QColor& c)
{
    if (m_variable == c) return;
    m_variable = c;
    rehighlight();
}

void SettingsPromptHighlighter::setVariableBackground(const QColor& c)
{
    if (m_background == c) return;
    m_background = c;
    rehighlight();
}

void SettingsPromptHighlighter::setUnknownColor(const QColor& c)
{
    if (m_unknown == c) return;
    m_unknown = c;
    rehighlight();
}

} // namespace tanara_qml
