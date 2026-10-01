#pragma once
//
// K-02 — Beállítások → Tanara Cloud fiók-panel (a cloud indulása után; előtte ugyanitt a
// „Hamarosan” panel). Egy bejelentkezés az átíráshoz és az összefoglalóhoz is.
//  Kijelentkezve: [Bejelentkezés a Tanara Cloudba] (K-03).
//  Bejelentkezve: e-mail, egyenleg (+ ÁFA-jelölés, ≈ óra), inline állapotok (offline, leválasztva,
//  felfüggesztve, túl régi kliens, elfogyott), notice-ok, [Egyenleg feltöltése] / [Írj nekünk],
//  [↻], [Fiókom a weben ↗], [Napló a weben ↗] (K-14 helye), [Kijelentkezés], alapértelmezett
//  szintek + nyelv (K-04), [Expert mód…] (K-05), „Mi hagyja el a gépet?” (CLI-15).
//
#include "tanara/cloud/CloudTypes.h"

#include <QWidget>

#include <functional>

class QFrame;
class QLabel;
class QPushButton;
class QVBoxLayout;
class QWidget;

namespace tanara { class AppController; }

namespace tanara_gui {

class CloudTierWidget;

class CloudAccountPanel : public QWidget {
    Q_OBJECT
public:
    explicit CloudAccountPanel(tanara::AppController* app, QWidget* parent = nullptr);

signals:
    // „Használat”: az átírás és az összefoglaló szolgáltatója legyen a Tanara Cloud.
    void useCloudRequested();

private:
    void refresh();
    void showInline(const QString& glyph, const QString& text, const QString& cta,
                    std::function<void()> action, bool critical);

    tanara::AppController* m_app;
    tanara::CloudError m_lastError;
    QWidget* m_outBox = nullptr;
    QWidget* m_inBox = nullptr;
    QFrame* m_inline = nullptr;
    QLabel* m_inlineText = nullptr;
    QPushButton* m_inlineCta = nullptr;
    std::function<void()> m_inlineAction;
    QLabel* m_email = nullptr;
    QLabel* m_balance = nullptr;
    QLabel* m_hours = nullptr;
    QLabel* m_trial = nullptr;
    QLabel* m_notices = nullptr;
    QPushButton* m_topup = nullptr;
    QPushButton* m_useCloud = nullptr;
    CloudTierWidget* m_sttTier = nullptr;
    CloudTierWidget* m_llmTier = nullptr;
};

} // namespace tanara_gui
