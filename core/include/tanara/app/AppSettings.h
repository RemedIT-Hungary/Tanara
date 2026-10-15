#pragma once
//
// AppSettings: the aggregate of the module settings sections. settings.json (format version 2)
// stores one JSON object per section (foundation::SettingsStore):
//
//   { "version": 2,
//     "paths":      PathSettings        (foundation/paths)
//     "recording":  RecordingSettings   (recording)
//     "media":      MediaSettings       (media: ffmpeg / ffprobe location)
//     "watcher":    WatcherSettings     (watcher)
//     "processing": ProcessingSettings  (processing)
//     "providers":  ProviderSettings    (providers)
//     "routes":     RouteSettings       (providers/core/Routes.h: where each capability runs)
//     "cloud":      CloudSettings       (cloud)
//     "ui":         UiSettings          (app, below)
//     "secrets":    SecretSettings      (foundation/secrets) }
//
// app::SettingsManager is the typed interface (load / save / defaults).
#include "tanara/cloud/CloudSettings.h"
#include "tanara/foundation/paths/PathSettings.h"
#include "tanara/foundation/secrets/SecretStore.h"
#include "tanara/media/MediaSettings.h"
#include "tanara/processing/ProcessingSettings.h"
#include "tanara/providers/core/ProviderSettings.h"
#include "tanara/providers/core/Routes.h"
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
    media::MediaSettings media;
    watcher::WatcherSettings watcher;
    processing::ProcessingSettings processing;
    providers::ProviderSettings providers;
    providers::RouteSettings routes;
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

    // After loading the sections: a missing "routes" section (settings written before S3) is converted from
    // the older fields (rawProcessing: the stored "processing" object, its removed "localDiarization" switch;
    // cloudLive: stt / llm default to Ask), then the routes are aligned with the provider selection
    // (RouteSettings::normalize: a selected cloud id becomes route Cloud).
    void finishRoutes(const QJsonObject& rawProcessing, bool cloudLive);
};

} // namespace tanara::app

Q_DECLARE_METATYPE(tanara::app::AppSettings)
