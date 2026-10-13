#pragma once
// Processing tier (Fast / Accurate) and the meeting language, per task (transcription or
// summary). Warns based on the catalog (/v1/models) metadata: a non-diarizing transcription,
// a model not recommended for the language. If an Expert model is selected it shows that, and
// one click returns to the tiers. The choice goes into the settings IMMEDIATELY (one source
// of truth: the estimate dialog and the Settings Tanara Cloud panel change the same thing).
// Embedded by the estimate dialog (CloudEstimateDialog).
#include "tanara/provider/ReadinessModel.h"

#include <QWidget>

class QButtonGroup;
class QComboBox;
class QLabel;
class QPushButton;

namespace tanara { class AppController; }

namespace tanara_gui {

class CloudTierWidget : public QWidget {
    Q_OBJECT
public:
    // step: Transcribe or Summarize. showLanguage: also a language picker.
    CloudTierWidget(tanara::AppController* app, tanara::WorkflowStep step, bool showLanguage,
                    QWidget* parent = nullptr);

    void refresh();                       // after the settings / catalog change

    // Fills the language picker.
    static void fillLanguages(QComboBox* combo, const QString& current);

signals:
    void changed();                       // tier / language / Expert changed (re-estimate)

private:
    tanara::AppController* m_app;
    tanara::WorkflowStep m_step;
    QButtonGroup* m_group = nullptr;
    QPushButton* m_fast = nullptr;
    QPushButton* m_accurate = nullptr;
    QLabel* m_expertLabel = nullptr;
    QComboBox* m_lang = nullptr;
    QLabel* m_price = nullptr;
    QLabel* m_warn = nullptr;
    bool m_updating = false;
};

} // namespace tanara_gui
