#pragma once
//
// ImportDemoFacade: the fictional tanara::library::IImportFacade for --demo, --gallery,
// --qml-shot and the tests: serves the "Import audio file" window's design states ("files",
// "split", "probing", "error", "failed", "progress"); it never imports anything (live() is false).
#include "tanara/library/IImportFacade.h"

namespace tanara_demo {

class ImportDemoFacade : public tanara::library::IImportFacade {
    Q_OBJECT
public:
    explicit ImportDemoFacade(QObject* parent = nullptr);

    bool live() const override { return false; }
    bool canStart(const QString& scenario) const override { return !scenario.isEmpty(); }
    void probeAsync(const QString&) override {}
    QString start(const tanara::library::ImportRequest&) override { return {}; }
    void cancel(const QString&) override {}
    tanara::Tag createTag(const QString& name) override;
    void applyTags(const QString&, const QStringList&) override {}
    QVector<tanara::TagSuggestion> suggestionsFor(const QString&) const override { return {}; }
    std::optional<tanara::library::ImportScenario> scenario(const QString& scenario) const override;
};

} // namespace tanara_demo
