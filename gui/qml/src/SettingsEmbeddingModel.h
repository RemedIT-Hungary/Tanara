#pragma once
//
// SettingsEmbeddingModel — a Beállítások › Szolgáltatások „Beágyazás” kártyája és a
// „CÍMKEJAVASLATOK” rész (C09, T14–T16).
//
//  - mód: „Nincs (alap)” / „Helyi végpont” / „Tanara Cloud” — a SettingsViewModel
//    piszkozatába (AppSettings::embeddingProviderId / embeddingConfigs); a helyi végpont mezői,
//    a „Lekérés” és a „Kapcsolat tesztelése” a `card` (SettingsProviderModel, Embedding) dolga;
//  - KÖNYVTÁR ELŐKÉSZÍTÉSE: a core EmbeddingPreparer élő állapota (fut / megállt / naprakész),
//    Megszakítás / Folytatás / (Újra)előkészítés;
//  - modellváltás MENTÉS ELŐTT jelezve (restartPending): a core a settingsChanged-re maga
//    indítja újra az előkészítést, mentéskor itt nincs külön teendő;
//  - a két kapcsoló (tagSuggestions, llmTagSuggestions) a piszkozatba; az elutasított
//    javaslatok visszaállítása AZONNALI művelet (TagService::clearRejected, a QML megerősít).
//
// A SettingsViewModel hozza létre (vm.embedding); önmagában (szülő nélkül) csak a
// demó-állapotok kitalált előkészítési adatát mutatja. demoState: local | localRunning |
// localError | localDone | cloud | modelChange | none.
//
#include "SettingsProviderModel.h"

#include "tanara/embedding/EmbeddingPreparer.h"

#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace tanara {
class AppController;
}

namespace tanara_qml {

class SettingsViewModel;

class SettingsEmbeddingModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    // "none" | "local" | "cloud" — a piszkozat választása.
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY changed)
    // A szegmentált választó elemei: [{ value, label, enabled, toolTip }] (Cloud nélküli
    // buildben a Tanara Cloud elem hiányzik; bejelentkezés nélkül tiltott).
    Q_PROPERTY(QVariantList modeOptions READ modeOptions NOTIFY changed)
    Q_PROPERTY(QString description READ description NOTIFY changed)
    // A helyi végpont mezői (Cím, Modell, kulcs) és a kapcsolat-teszt.
    Q_PROPERTY(tanara_qml::SettingsProviderModel* card READ card CONSTANT)
    // Az állapot-pirula: "neutral" | "ok" | "failed" | "testing" + felirat.
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    // Tanara Cloud: a tájékoztató doboz és (ha kell) a bejelentkezés hiánya.
    Q_PROPERTY(QString cloudInfo READ cloudInfo NOTIFY changed)
    Q_PROPERTY(QString cloudHint READ cloudHint NOTIFY changed)

    // ---- KÖNYVTÁR ELŐKÉSZÍTÉSE ----
    Q_PROPERTY(bool prepVisible READ prepVisible NOTIFY changed)
    // "idle" | "running" | "error" | "done" | "pending" (a választás még nincs elmentve)
    Q_PROPERTY(QString prepStatus READ prepStatus NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)        // 0..1
    Q_PROPERTY(QString countText READ countText NOTIFY changed)     // „23 / 40 megbeszélés”
    Q_PROPERTY(QString etaText READ etaText NOTIFY changed)         // „kb. 4 perc van hátra”
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)     // „Megállt 31 / 40-nél: …”
    Q_PROPERTY(QString doneText READ doneText NOTIFY changed)       // „Naprakész · 40 megbeszélés · okt. 5. 09:12”

    // ---- modellváltás ----
    Q_PROPERTY(bool restartPending READ restartPending NOTIFY changed)
    Q_PROPERTY(QString restartWarning READ restartWarning NOTIFY changed)

    // ---- CÍMKEJAVASLATOK ----
    Q_PROPERTY(bool tagSuggestions READ tagSuggestions WRITE setTagSuggestions NOTIFY changed)
    Q_PROPERTY(bool llmTagSuggestions READ llmTagSuggestions WRITE setLlmTagSuggestions NOTIFY changed)
    Q_PROPERTY(int rejectedCount READ rejectedCount NOTIFY rejectedChanged)

    Q_PROPERTY(QString demoState READ demoState WRITE setDemoState NOTIFY demoStateChanged)

public:
    explicit SettingsEmbeddingModel(QObject* parent = nullptr);

    QString mode() const;
    void setMode(const QString& mode);
    QVariantList modeOptions() const;
    QString description() const;
    SettingsProviderModel* card() const { return m_card; }
    QString status() const;
    QString statusText() const;
    QString cloudInfo() const;
    QString cloudHint() const;

    bool prepVisible() const;
    QString prepStatus() const;
    double progress() const;
    QString countText() const;
    QString etaText() const;
    QString errorText() const;
    QString doneText() const;

    bool restartPending() const;
    QString restartWarning() const;

    bool tagSuggestions() const;
    void setTagSuggestions(bool on);
    bool llmTagSuggestions() const;
    void setLlmTagSuggestions(bool on);
    int rejectedCount() const;

    QString demoState() const { return m_demoState; }
    void setDemoState(const QString& state);

    Q_INVOKABLE void startPreparation();     // „Előkészítés”
    Q_INVOKABLE void cancelPreparation();    // „Megszakítás”
    Q_INVOKABLE void resumePreparation();    // „Folytatás”
    Q_INVOKABLE void reprepare();            // „Újraelőkészítés”: az index eldobása + előről
    Q_INVOKABLE void clearRejected();        // „Visszaállítás” (azonnal; a QML megerősít)

    // ---- a SettingsViewModelnek ----
    void attach();                           // a controller (vagy demó) bekötése
    void reset();                            // a piszkozat újratöltve / eldobva
    void notifyDraftChanged() { emit changed(); }
    // A kiválasztott provider üres mezőinek kitöltése az alapértelmezésekkel.
    void normalize(tanara::AppSettings& s) const;
    // Demó: az előkészítés kitalált állapota + a piszkozat ehhez (a vm loadDemo-ja hívja).
    void loadDemo(const QString& state);
    // A számolt (és a tesztben ellenőrzött) előkészítési állapot.
    tanara::EmbeddingState prepState() const;

signals:
    void changed();
    void rejectedChanged();
    void demoStateChanged();

private:
    tanara::AppController* controller() const;
    static QString modeOf(const QString& providerId);
    static QString providerFor(const QString& mode);
    // A core által használt modell ("" = nincs beágyazás): a helyi végpont modellje, a
    // Cloudé a gateway alapmodellje.
    static QString effectiveModel(const tanara::AppSettings& s);
    bool cloudSelectable() const;
    int estimateMinutes(int meetings) const;

    SettingsViewModel* m_vm = nullptr;
    SettingsProviderModel* m_card = nullptr;
    QPointer<tanara::EmbeddingPreparer> m_preparer;
    QMetaObject::Connection m_prepConn, m_rejectConn;
    QString m_demoState;
    tanara::EmbeddingState m_demo;           // demó-állapot (controller nélkül)
    int m_demoRejected = 0;
};

} // namespace tanara_qml
