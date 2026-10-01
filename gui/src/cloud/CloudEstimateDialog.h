#pragma once
//
// K-06 — Költségbecslés és megerősítés (POST /v1/estimate) minden cloud-feldolgozás előtt.
// A kliens NEM számol árat: a gateway becslését mutatja (pontos STT-sor, becsült LLM-sor),
// és a három egyenleg-állapotot (kiegészítés-2 1.12): elég / „valószínűleg elég, de kevés
// tartalék” / nincs elég (indítás helyett feltöltés). A szint (K-04) itt is állítható → újrabecslés.
//
#include "tanara/cloud/CloudTypes.h"

#include <QDialog>

class QLabel;
class QPushButton;
class QVBoxLayout;

namespace tanara { class AppController; }

namespace tanara_gui {

class CloudEstimateDialog : public QDialog {
    Q_OBJECT
public:
    // task: transcribe | summarize; summaryMode: quick | complex (összefoglalónál).
    CloudEstimateDialog(tanara::AppController* app, const QString& meetingId, const QString& task,
                        const QString& summaryMode, QWidget* parent = nullptr);

protected:
    void showEvent(QShowEvent* e) override;

private:
    void requestEstimate();
    void render(const tanara::EstimateResult& e);
    void onError(const tanara::CloudError& e);

    tanara::AppController* m_app;
    QString m_meetingId, m_task, m_mode;
    int m_seq = 0;                 // a legutóbbi kérés sorszáma (a régi válasz eldobható)
    bool m_started = false;
    bool m_retryMode = false;      // hálózati hiba után az elsődleges gomb = „Újra”
    tanara::EstimateResult m_last;

    QLabel* m_main = nullptr;
    QLabel* m_range = nullptr;
    QVBoxLayout* m_lines = nullptr;
    QLabel* m_balance = nullptr;
    QLabel* m_after = nullptr;
    QLabel* m_vat = nullptr;
    QLabel* m_error = nullptr;
    QPushButton* m_start = nullptr;
    QPushButton* m_topup = nullptr;
};

} // namespace tanara_gui
