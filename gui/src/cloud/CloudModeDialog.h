#pragma once
// Mode choice on first launch (simple variant): "Own keys (BYO)" vs "Tanara Cloud", visually
// equal cards, neither preselected. Shown only in live cloud mode and only on first launch
// (never in teaser mode, where there is no pop-up window).
#include <QDialog>

namespace tanara { class AppController; }

namespace tanara_gui {

class CloudModeDialog : public QDialog {
    Q_OBJECT
public:
    enum Choice { Later = 0, Byo = 1, Cloud = 2 };
    explicit CloudModeDialog(tanara::AppController* app, QWidget* parent = nullptr);
    Choice choice() const { return m_choice; }

private:
    Choice m_choice = Later;
};

} // namespace tanara_gui
