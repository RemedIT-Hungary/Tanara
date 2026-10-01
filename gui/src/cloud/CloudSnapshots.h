#pragma once
// Fejlesztői QA-mód: a Tanara Cloud képernyők PNG-be mentése (`tanara --ui-snapshots <mappa>`).
#include <QString>

namespace tanara { class AppController; }

namespace tanara_gui {
class MainWindow;
int runCloudSnapshots(tanara::AppController& app, MainWindow& window, const QString& dir);
}
