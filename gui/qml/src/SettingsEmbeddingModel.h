#pragma once
//
// SettingsEmbeddingModel: the Settings > Services "Embedding" card and the "TAG SUGGESTIONS"
// section.
//
//  - mode: "None (default)" / "Local endpoint" / "Tanara Cloud": into the SettingsViewModel
//    draft (AppSettings::embeddingProviderId / embeddingConfigs); the local endpoint's fields,
//    "Fetch" and "Test connection" are the `card`'s job (SettingsProviderModel, Embedding);
//  - LIBRARY PREPARATION: the core EmbeddingPreparer's live state (running / stopped / up to
//    date), Cancel / Resume / (Re)prepare;
//  - a model change signaled BEFORE SAVING (restartPending): the core restarts the
//    preparation itself on settingsChanged, so there is nothing separate to do here on save;
//  - the two switches (tagSuggestions, llmTagSuggestions) go into the draft; resetting the
//    rejected suggestions is an IMMEDIATE action (TagService::clearRejected, QML confirms).
//
// Created by the SettingsViewModel (vm.embedding); standalone (without a parent) it only
// shows the demo states' fictional preparation data. demoState: local | localRunning |
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

    // "none" | "local" | "cloud": the draft's choice.
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY changed)
    // The segmented selector's items: [{ value, label, enabled, toolTip }] (the Tanara Cloud item
    // is always disabled: "coming soon").
    Q_PROPERTY(QVariantList modeOptions READ modeOptions NOTIFY changed)
    Q_PROPERTY(QString description READ description NOTIFY changed)
    // The local endpoint's fields (Address, Model, key) and the connection test.
    Q_PROPERTY(tanara_qml::SettingsProviderModel* card READ card CONSTANT)
    // The status pill: "neutral" | "ok" | "failed" | "testing" + label.
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)

    // ---- LIBRARY PREPARATION ----
    Q_PROPERTY(bool prepVisible READ prepVisible NOTIFY changed)
    // "idle" | "running" | "error" | "done" | "pending" (the choice is not saved yet)
    Q_PROPERTY(QString prepStatus READ prepStatus NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)        // 0..1
    Q_PROPERTY(QString countText READ countText NOTIFY changed)     // "23 / 40 meetings"
    Q_PROPERTY(QString etaText READ etaText NOTIFY changed)         // "about 4 minutes left"
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)     // "Stopped at 31 / 40: ..."
    Q_PROPERTY(QString doneText READ doneText NOTIFY changed)       // "Up to date · 40 meetings · Oct 5, 9:12 AM"

    // ---- model change ----
    Q_PROPERTY(bool restartPending READ restartPending NOTIFY changed)
    Q_PROPERTY(QString restartWarning READ restartWarning NOTIFY changed)

    // ---- TAG SUGGESTIONS ----
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

    Q_INVOKABLE void startPreparation();     // "Prepare"
    Q_INVOKABLE void cancelPreparation();    // "Cancel"
    Q_INVOKABLE void resumePreparation();    // "Resume"
    Q_INVOKABLE void reprepare();            // "Re-prepare": drop the index + start over
    Q_INVOKABLE void clearRejected();        // "Reset" (immediate; QML confirms)

    // ---- a SettingsViewModelnek ----
    void attach();                           // wiring the controller (or demo)
    void reset();                            // the draft reloaded / discarded
    void notifyDraftChanged() { emit changed(); }
    // Fills the selected provider's empty fields with the defaults.
    void normalize(tanara::AppSettings& s) const;
    // Demo: the fictional preparation state + the draft for it (called by the vm's loadDemo).
    void loadDemo(const QString& state);
    // The computed (and in-test verified) preparation state.
    tanara::EmbeddingState prepState() const;

signals:
    void changed();
    void rejectedChanged();
    void demoStateChanged();

private:
    tanara::AppController* controller() const;
    static QString modeOf(const QString& providerId);
    static QString providerFor(const QString& mode);
    // The model the core uses ("" = no embedding): the local endpoint's model, for Cloud the
    // gateway's base model.
    static QString effectiveModel(const tanara::AppSettings& s);
    int estimateMinutes(int meetings) const;

    SettingsViewModel* m_vm = nullptr;
    SettingsProviderModel* m_card = nullptr;
    QPointer<tanara::EmbeddingPreparer> m_preparer;
    QMetaObject::Connection m_prepConn, m_rejectConn;
    QString m_demoState;
    tanara::EmbeddingState m_demo;           // demo state (without a controller)
    int m_demoRejected = 0;
};

} // namespace tanara_qml
