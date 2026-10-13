#pragma once
// Terms acceptance. Two variants:
//  - preliminary (14-day period): [View] [Accept] [Later]; "Later" asks again at most once a
//    day (CloudAccount::postponeTerms);
//  - mandatory (after the effective date, 403 terms_acceptance_required): [View] [Accept]
//    [Cancel]. Running processing is not interrupted; account data, models and sign-out still work.
// Both have "I do not accept, delete my account", which opens the dashboard in the browser.
#include "tanara/cloud/CloudTypes.h"

#include <QDialog>

class QLabel;
class QPushButton;

namespace tanara { class AppController; }

namespace tanara_gui {

class CloudTermsDialog : public QDialog {
    Q_OBJECT
public:
    // version: the version to accept; url: the text; effective: effective date (for the preliminary variant).
    CloudTermsDialog(tanara::AppController* app, bool mandatory, const QString& version,
                     const QString& url, const QDateTime& effective, const QString& summary,
                     QWidget* parent = nullptr);

    // At startup / after an account refresh: whether a preliminary dialog is needed (and was not postponed today).
    static bool shouldOfferEarly(tanara::AppController* app);
    // Convenience entry points. true = accepted.
    static bool offerEarly(tanara::AppController* app, QWidget* parent);
    static bool requireFromError(tanara::AppController* app, const tanara::CloudError& e, QWidget* parent);

private:
    tanara::AppController* m_app;
    QString m_version;
    QLabel* m_status = nullptr;
    QPushButton* m_accept = nullptr;
};

} // namespace tanara_gui
