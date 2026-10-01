#pragma once
//
// K-15 — ÁSZF-elfogadás. Két változat (kiegészítés-2 1.8):
//  - előzetes (14 napos időszak): [Megtekintés] [Elfogadom] [Később] — „Később” naponta
//    legfeljebb egyszer kérdez újra (CloudAccount::postponeTerms);
//  - kötelező (hatálybalépés után, 403 terms_acceptance_required): [Megtekintés] [Elfogadom]
//    [Mégse]. A futó feldolgozás nem szakad meg; a fiók-panel, a modellek, a kijelentkezés megy.
// Mindkettőn: „Nem fogadom el, fiók törlése” → a dashboard a böngészőben.
//
#include "tanara/cloud/CloudTypes.h"

#include <QDialog>

class QLabel;
class QPushButton;

namespace tanara { class AppController; }

namespace tanara_gui {

class CloudTermsDialog : public QDialog {
    Q_OBJECT
public:
    // version: az elfogadandó verzió; url: a szöveg; effective: hatálybalépés (előzetesnél).
    CloudTermsDialog(tanara::AppController* app, bool mandatory, const QString& version,
                     const QString& url, const QDateTime& effective, const QString& summary,
                     QWidget* parent = nullptr);

    // Indításkor / fiók-frissítés után: kell-e előzetes dialógus (és nem halasztották ma).
    static bool shouldOfferEarly(tanara::AppController* app);
    // Kényelmi belépők. true = elfogadta.
    static bool offerEarly(tanara::AppController* app, QWidget* parent);
    static bool requireFromError(tanara::AppController* app, const tanara::CloudError& e, QWidget* parent);

private:
    tanara::AppController* m_app;
    QString m_version;
    QLabel* m_status = nullptr;
    QPushButton* m_accept = nullptr;
};

} // namespace tanara_gui
