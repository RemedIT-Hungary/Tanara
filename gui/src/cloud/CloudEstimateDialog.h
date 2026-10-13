#pragma once
// Cost estimate and confirmation (POST /v1/estimate) before every cloud processing.
// The client does NOT compute prices: it shows the gateway's estimate (an exact STT row, an
// estimated LLM row) and the three balance states: enough / "probably enough, but little
// reserve" / not enough (top-up instead of start). The tier can be changed here too, which
// re-estimates.
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
    // task: transcribe | summarize; summaryMode: quick | complex (for the summary).
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
    int m_seq = 0;                 // sequence number of the latest request (an old reply can be dropped)
    bool m_started = false;
    bool m_retryMode = false;      // after a network error the primary button becomes "Retry"
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
