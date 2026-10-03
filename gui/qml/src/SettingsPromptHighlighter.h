#pragma once
//
// SettingsPromptHighlighter — a prompt-szerkesztő (B07) kiemelése: a `{{VÁLTOZÓ}}` jelölők
// accentSoft háttérrel és accent betűvel. A kód által TÉNYLEG behelyettesített változók
// (tanara::promptVariables) kapják az erős kiemelést; az ismeretlen `{{…}}` halványabbat
// (elgépelés vagy saját jelölő — a modell szó szerint megkapja).
//
// QML-ből:  SettingsPromptHighlighter { document: editor.textDocument }
// A színek a Theme-ből jönnek (a QML köti), így témaváltáskor újraszínez.
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

    // Rögzített sormagasság (px) a dokumentum minden bekezdésére — a QML TextEdit-nek nincs
    // lineHeight-ja, a spec 12 px × 1,65-öt kér. PROGRAMOZOTT szövegbetöltés után hívandó: a
    // visszavonás-vermet üríti (a formázás ne legyen „visszavonható” lépés); a később gépelt
    // sorok az előző bekezdés formátumát öröklik.
    Q_INVOKABLE void applyLineHeight(int pixels);

    // Egy sor kiemelendő szakaszai: [start, length, ismert-e] — a kiemelő és a tesztek közös magja.
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
