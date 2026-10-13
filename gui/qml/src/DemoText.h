#pragma once
//
// Tanara QML — a KITALÁLT demo-tartalom (demó-mód, --qml-shot, Gallery, nézetmodellek demo-
// állapotai) nyelvi kapcsolója. A demo-szövegek NEM fordítások (nem qsTr / tr): magyar UI-nál
// a magyar, angol UI-nál (tanara::activeUiLanguage() == "en") egy természetes angol változat
// szól — más nevekkel (Kovács Lilla → Emma Carter …), hogy a nemzetközi képek következetesek
// legyenek. A valódi (nem demo) kódutak ezt sosem használják.
//
// C++:  demoText("Negyedéves partnertalálkozó", "Quarterly partner review")
//       demoName("Kovács Lilla")   → angolul "Emma Carter" (ismeretlen név: változatlan)
//       demoRaw("Távoli 1")        → angolul "Remote 1" (a kitalált nyers beszélő-címkékhez)
// QML:  DemoText.t("…", "…"), DemoText.name("Kovács Lilla"), DemoText.en  (DemoText.qml)
//
#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

namespace tanara_qml {

namespace demotext {

// true, ha a demo-tartalom angol (a felület nyelve vagy a tesztek felülbírálása szerint).
bool english();
// "hu" / "en".
QString language();
// Tesztekhez: "en" / "hu" kényszerít, üres → vissza az activeUiLanguage()-hez.
void setLanguageOverride(const QString& lang);

// Személynév a demo-készletből: angol nyelvnél a párja; ismeretlen név változatlan.
QString name(const QString& hu);
QStringList names(const QStringList& hu);
// Kitalált nyers beszélő-címke („Beszélő 3” / „Távoli 1”): angolul „Speaker 3” / „Remote 1”.
QString raw(const QString& hu);

} // namespace demotext

// A két szöveg közül a demo-nyelvnek megfelelő (UTF-8 literálok).
inline QString demoText(const char* hu, const char* en)
{
    return QString::fromUtf8(demotext::english() ? en : hu);
}
inline QString demoName(const QString& hu) { return demotext::name(hu); }
inline QString demoName(const char* hu) { return demotext::name(QString::fromUtf8(hu)); }
inline QString demoRaw(const QString& hu) { return demotext::raw(hu); }

// A C++ kapcsoló QML-nek kitéve; a QML-kód a DemoText.qml singletont használja.
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
