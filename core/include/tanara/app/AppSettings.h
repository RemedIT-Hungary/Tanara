#pragma once
//
// AppSettings: the aggregate of the module settings sections. settings.json (format version 2)
// stores one JSON object per section (foundation::SettingsStore):
//
//   { "version": 2,
//     "paths":      PathSettings        (foundation/paths)
//     "recording":  RecordingSettings   (recording)
//     "watcher":    WatcherSettings     (watcher)
//     "processing": ProcessingSettings  (processing)
//     "providers":  ProviderSettings    (providers)
//     "cloud":      CloudSettings       (cloud)
//     "ui":         UiSettings          (app, below)
//     "secrets":    SecretSettings      (foundation/secrets) }
//
// app::SettingsManager is the typed interface (load / save / defaults).
#include "tanara/cloud/CloudSettings.h"
#include "tanara/foundation/paths/PathSettings.h"
#include "tanara/foundation/secrets/SecretStore.h"
#include "tanara/processing/ProcessingSettings.h"
#include "tanara/providers/core/ProviderSettings.h"
#include "tanara/recording/RecordingSettings.h"
#include "tanara/watcher/WatcherSettings.h"

#include <QJsonObject>
#include <QMetaType>
#include <QString>

namespace tanara::app {

struct UiSettings {
    // UI language: "auto" (system locale decides) | "hu" | "en". The source language is Hungarian;
    // a restart is needed after switching. See foundation/i18n/Localization.h.
    QString language{QStringLiteral("auto")};
    // The "Getting started" window already ran (closed, skipped or completed): only turns off the
    // automatic opening on first launch.
    bool onboardingDone = false;
    // To review (focus view): the line stepped onto plays by itself. On by default.
    bool reviewAutoPlay = true;

    QJsonObject toJson() const;
    static UiSettings fromJson(const QJsonObject& o);
};

struct AppSettings {
    paths::PathSettings paths;
    recording::RecordingSettings recording;
    watcher::WatcherSettings watcher;
    processing::ProcessingSettings processing;
    providers::ProviderSettings providers;
    cloud::CloudSettings cloud;
    UiSettings ui;
    SecretSettings secrets;

    // Section names in settings.json.
    static QStringList sectionNames();
    QJsonObject sectionJson(const QString& name) const;
    void setSectionJson(const QString& name, const QJsonObject& o);
    // Every section, keyed by its name (the settings.json document without "version").
    QJsonObject toJson() const;
    static AppSettings fromJson(const QJsonObject& o);
};

} // namespace tanara::app

Q_DECLARE_METATYPE(tanara::app::AppSettings)
