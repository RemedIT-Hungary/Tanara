#pragma once
//
// SettingsProviderModel — egy szolgáltató-kártya (B04 / B05) a Beállítások „Szolgáltatások”
// lapján: „Átírás (STT)” vagy „Összefoglaló (LLM)”.
//
// ADATVEZÉRELT: a választható szolgáltatók a provider-registryből, a mezők a kiválasztott
// ProviderDescriptor.fields-éből jönnek (típus, kötelező, titok, alapértelmezés, haladó,
// lekérhető modell-lista); új provider itt kód nélkül megjelenik. A mezők a SettingsViewModel
// piszkozatát írják (a titkok a titok-piszkozatot); szolgáltató-váltáskor a másik
// szolgáltató beállítása megmarad.
//
// „Kapcsolat tesztelése” és „Lekérés”: tanara::ConnectionTester a PISZKOZAT címével és
// kulcsával (mentés előtt is kipróbálható). Bármelyik mező módosítása elavulttá teszi az
// előző teszt eredményét.
//
#include "tanara/Types.h"
#include "tanara/provider/ConnectionTester.h"
#include "tanara/provider/ProviderDescriptor.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace tanara_qml {

class SettingsViewModel;

class SettingsProviderModel : public QObject {
    Q_OBJECT
    QML_ANONYMOUS

    Q_PROPERTY(QString kind READ kindName CONSTANT)   // "stt" | "llm"
    // [{ value (a provider id-ja), label, tag }] — a saját kulcsos szolgáltatók (+ a Tanara Cloud, ha épp az van kiválasztva).
    Q_PROPERTY(QVariantList providers READ providers NOTIFY providersChanged)
    Q_PROPERTY(QString providerId READ providerId WRITE setProviderId NOTIFY providerChanged)
    Q_PROPERTY(QString providerLabel READ providerLabel NOTIFY providerChanged)
    // A kiválasztott szolgáltató a Tanara Cloud (bejelentkezős): nincs mező, nincs teszt.
    Q_PROPERTY(bool loginProvider READ loginProvider NOTIFY providerChanged)
    // A fő mezők és a „Haladó” mezők SZERKEZETE (csak szolgáltató-váltáskor változik, hogy
    // gépelés közben a szerkesztők ne épüljenek újra):
    // [{ key, label, type: text|url|secret|number|combo, help, required, dynamic,
    //    minValue, maxValue, decimals }]
    // Az értékek a value(key) / fieldError(key) / placeholder(key) / options(key) hívásokból
    // jönnek; a QML a `revision`-t veszi kötésbe, hogy a változást észrevegye.
    Q_PROPERTY(QVariantList fields READ fields NOTIFY fieldsChanged)
    Q_PROPERTY(QVariantList advancedFields READ advancedFields NOTIFY fieldsChanged)
    Q_PROPERTY(int revision READ revision NOTIFY valuesChanged)
    Q_PROPERTY(bool advancedOpen READ advancedOpen WRITE setAdvancedOpen NOTIFY advancedOpenChanged)
    // Kapcsolat-teszt: "" (nincs / elavult) | "testing" | "ok" | "failed"
    Q_PROPERTY(bool testable READ testable NOTIFY providerChanged)
    Q_PROPERTY(QString testState READ testState NOTIFY testChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY testChanged)     // „Kapcsolódva · 210 ms”
    Q_PROPERTY(QString errorText READ errorText NOTIFY testChanged)       // emberi mondat
    Q_PROPERTY(QString errorCode READ errorCode NOTIFY testChanged)       // ECONNREFUSED, HTTP 401 …
    Q_PROPERTY(QString warningText READ warningText NOTIFY testChanged)   // siker, de pl. a modell nincs a listán
    Q_PROPERTY(bool fetching READ fetching NOTIFY fetchChanged)
    Q_PROPERTY(QString fetchError READ fetchError NOTIFY fetchChanged)
    // A modell „gondolkodása” az összefoglalónál (ProviderConfig::reasoning): "auto" | "off" |
    // "on". Csak a saját kulcsos LLM-szolgáltatónál (a Haladó részben); a cloud maga dönt.
    Q_PROPERTY(bool reasoningAvailable READ reasoningAvailable NOTIFY providerChanged)
    Q_PROPERTY(QString reasoning READ reasoning WRITE setReasoning NOTIFY valuesChanged)
    // B04: ehhez a kártyához vezetett a mély hivatkozás.
    Q_PROPERTY(bool highlighted READ highlighted NOTIFY highlightedChanged)

public:
    SettingsProviderModel(SettingsViewModel* vm, tanara::ProviderKind kind);

    QString kindName() const;
    QVariantList providers() const;
    QString providerId() const;
    void setProviderId(const QString& id);
    QString providerLabel() const;
    bool loginProvider() const;
    QVariantList fields() const { return fieldList(false); }
    QVariantList advancedFields() const { return fieldList(true); }
    bool advancedOpen() const { return m_advancedOpen; }
    void setAdvancedOpen(bool open);
    bool testable() const;
    QString testState() const { return m_testState; }
    QString statusText() const;
    QString errorText() const { return m_testState == QLatin1String("failed") ? m_result.message : QString(); }
    QString errorCode() const { return m_testState == QLatin1String("failed") ? m_result.code : QString(); }
    QString warningText() const { return m_testState == QLatin1String("ok") ? m_result.warning : QString(); }
    bool fetching() const { return m_fetchId != 0; }
    QString fetchError() const { return m_fetchError; }
    bool highlighted() const;
    bool reasoningAvailable() const;
    QString reasoning() const;
    void setReasoning(const QString& mode);

    Q_INVOKABLE void setValue(const QString& key, const QVariant& value);
    Q_INVOKABLE QVariant value(const QString& key) const;
    Q_INVOKABLE QString fieldError(const QString& key) const;
    Q_INVOKABLE QString placeholder(const QString& key) const;
    Q_INVOKABLE QStringList options(const QString& key) const;
    int revision() const { return m_revision; }
    Q_INVOKABLE void test();
    Q_INVOKABLE void fetchModels();

    // ---- a SettingsViewModelnek ----
    tanara::ProviderDescriptor descriptor() const;
    tanara::ProviderDescriptor descriptor(const QString& id) const;
    QVector<tanara::ProviderDescriptor> allDescriptors() const;
    // A hiányzó (üres) mezők kitöltése a leíró alapértelmezéseivel.
    void fillDefaults(tanara::ProviderConfig& cfg, const tanara::ProviderDescriptor& d) const;
    // A piszkozat futásidejű configja a kiválasztott szolgáltatóra (a kulccsal együtt).
    tanara::ProviderConfig runtimeConfig() const;
    // Mező-hibák a mentés előtti ellenőrzéshez: key → szöveg.
    QHash<QString, QString> fieldErrors() const;
    // A piszkozat / a szolgáltató megváltozott (újratöltés, eldobás, külső változás).
    void reset();
    void notifyFocusChanged() { emit highlightedChanged(); }
    bool lastTestFailed() const { return m_testState == QLatin1String("failed"); }
    // Demó: rögzített teszt-eredmény.
    void setDemoResult(const QString& state, int latencyMs, const QString& message,
                       const QString& code);
    void setTester(tanara::ConnectionTester* tester);   // tesztekhez (pl. rövid időkorlát)
    tanara::ConnectionTester* tester() const { return m_tester; }

signals:
    void providersChanged();
    void providerChanged();
    void fieldsChanged();
    void valuesChanged();
    void advancedOpenChanged();
    void testChanged();
    void fetchChanged();
    void highlightedChanged();

private:
    QVariantList fieldList(bool advanced) const;
    void bumpValues() { ++m_revision; emit valuesChanged(); }
    QMap<QString, tanara::ProviderConfig>& configs() const;
    QString& selectedId() const;
    void invalidateTest();
    void onFinished(int id, const tanara::ConnectionTestResult& result);

    SettingsViewModel* m_vm;
    tanara::ProviderKind m_kind;
    tanara::ConnectionTester* m_tester = nullptr;
    bool m_advancedOpen = false;
    int m_revision = 0;
    QString m_testState;
    tanara::ConnectionTestResult m_result;
    int m_testId = 0;
    int m_fetchId = 0;
    QString m_fetchError;
    QStringList m_models;          // a lekért modell-lista (a kiválasztott szolgáltatóhoz)
    QString m_modelsFor;           // melyik szolgáltató + cím listája
};

} // namespace tanara_qml
