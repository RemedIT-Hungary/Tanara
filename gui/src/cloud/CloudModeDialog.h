#pragma once
//
// K-01 — Módválasztás első indításkor (egyszerű változat): „Saját kulcsok (BYO)” vs „Tanara
// Cloud”, vizuálisan egyenrangú kártyák, egyik sincs előre kijelölve. Csak élő cloud-módban
// és csak az első indításkor jelenik meg (teaser-módban soha — ott nincs felugró ablak).
//
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
