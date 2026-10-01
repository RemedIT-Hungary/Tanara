#pragma once
//
// K-05 — Expert mód: konkrét modell a kurált katalógusból (/v1/models, expert && !virtual),
// áradattal és jelölésekkel (beszélők elkülönítése, a nyelvhez nem ajánlott). A választás a
// settings cloudSttModel / cloudLlmModel mezőjébe kerül; „Vissza a Gyors / Pontos szintekhez”
// törli (→ a tier virtuális modellje).
//
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
    // kind: "stt" | "llm" — a kezdő fül.
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
