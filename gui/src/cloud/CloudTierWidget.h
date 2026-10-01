#pragma once
//
// K-04 — Feldolgozási szint (Gyors / Pontos) és a meeting nyelve, feladatonként (átírás vagy
// összefoglaló). A katalógus (/v1/models) metaadatai alapján figyelmeztet: nem diarizáló
// átírás, a nyelvhez nem ajánlott modell. Expert-modell kiválasztva → azt mutatja, és egy
// kattintással vissza lehet állni a szintekre. A választás AZONNAL a beállításokba kerül
// (egy igazságforrás: a fő ablak, a becslés-dialógus és a fiók-panel ugyanazt állítja).
//
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
    // step: Transcribe (átírás) vagy Summarize (összefoglaló). showLanguage: nyelv-választó is.
    CloudTierWidget(tanara::AppController* app, tanara::WorkflowStep step, bool showLanguage,
                    QWidget* parent = nullptr);

    void refresh();                       // beállítások / katalógus változása után

    // A nyelv-választó feltöltése és kiolvasása (közös a fiók-panellel).
    static void fillLanguages(QComboBox* combo, const QString& current);

signals:
    void changed();                       // szint / nyelv / Expert változott (→ újrabecslés)

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
