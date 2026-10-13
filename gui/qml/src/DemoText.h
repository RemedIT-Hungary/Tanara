#pragma once
//
// Tanara QML: the language switch of the FICTIONAL demo content (demo mode, --qml-shot,
// Gallery, the view models' demo states). Demo texts are NOT translations (not qsTr / tr):
// with a Hungarian UI the Hungarian, with an English UI (tanara::activeUiLanguage() == "en")
// a natural English version speaks, with different names (Kovács Lilla -> Emma Carter ...) so
// the international images are consistent. The real (non-demo) code paths never use this.
//
// C++:  demoText("<Hungarian text>", "<English text>")
//       demoName("Kovács Lilla")   -> in English "Emma Carter" (unknown name: unchanged)
//       demoRaw("<Hungarian raw label>") -> in English e.g. "Remote 1" (for the fictional raw speaker labels)
// QML:  DemoText.t("...", "..."), DemoText.name("Kovács Lilla"), DemoText.en  (DemoText.qml)
//
#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

namespace tanara_qml {

namespace demotext {

// True if the demo content is English (per the UI language or the tests' override).
bool english();
// "hu" / "en".
QString language();
// For tests: "en" / "hu" forces it, empty -> back to activeUiLanguage().
void setLanguageOverride(const QString& lang);

// A person name from the demo pool: in English its counterpart; an unknown name unchanged.
QString name(const QString& hu);
QStringList names(const QStringList& hu);
// A fictional raw speaker label ("Speaker 3" / "Remote 1" in Hungarian): in English "Speaker 3" / "Remote 1".
QString raw(const QString& hu);

} // namespace demotext

// The one of the two texts matching the demo language (UTF-8 literals).
inline QString demoText(const char* hu, const char* en)
{
    return QString::fromUtf8(demotext::english() ? en : hu);
}
inline QString demoName(const QString& hu) { return demotext::name(hu); }
inline QString demoName(const char* hu) { return demotext::name(QString::fromUtf8(hu)); }
inline QString demoRaw(const QString& hu) { return demotext::raw(hu); }

// The C++ switch exposed to QML; QML code uses the DemoText.qml singleton.
class DemoTextBridge : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(DemoTextCpp)
    QML_SINGLETON
    Q_PROPERTY(bool en READ en CONSTANT)
public:
    using QObject::QObject;
    bool en() const { return demotext::english(); }
    Q_INVOKABLE QString name(const QString& hu) const { return demotext::name(hu); }
    Q_INVOKABLE QStringList names(const QStringList& hu) const { return demotext::names(hu); }
    Q_INVOKABLE QString raw(const QString& hu) const { return demotext::raw(hu); }
};

} // namespace tanara_qml
