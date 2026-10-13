#pragma once
// Expert mode: a specific model from the curated catalog (/v1/models, expert && !virtual),
// with price data and flags (speaker separation, not recommended for the language). The
// choice goes into the settings field cloudSttModel / cloudLlmModel; "Back to the Fast /
// Accurate tiers" clears it (so the tier's virtual model applies).
#include <QDialog>

class QLineEdit;
class QListWidget;
class QTabBar;
class QLabel;

namespace tanara { class AppController; }

namespace tanara_gui {

class CloudModelPickerDialog : public QDialog {
    Q_OBJECT
public:
    // kind: "stt" | "llm", the initial tab.
    CloudModelPickerDialog(tanara::AppController* app, const QString& kind, QWidget* parent = nullptr);

private:
    void rebuild();
    QString currentKind() const;
    void store(const QString& modelId);

    tanara::AppController* m_app;
    QTabBar* m_tabs = nullptr;
    QLineEdit* m_search = nullptr;
    QListWidget* m_list = nullptr;
    QLabel* m_empty = nullptr;
};

} // namespace tanara_gui
