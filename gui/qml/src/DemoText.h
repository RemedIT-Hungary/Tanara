#pragma once
//
// Tanara QML: the QML side of the demo-language switch. The switch itself and the fictional
// names live in tanara_demo (demo/include/demo/DemoText.h); this header keeps the old
// tanara_qml spelling (demotext::, demoText(), demoName(), demoRaw()) for the view models
// and exposes the switch to QML (DemoText.qml).
//
#include "demo/DemoText.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

namespace tanara_qml {

namespace demotext = tanara_demo::demotext;
using tanara_demo::demoText;
using tanara_demo::demoName;
using tanara_demo::demoRaw;

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
