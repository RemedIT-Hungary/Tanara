// tanara-cli — headless teszt-hajtó (core-only, NINCS Widgets).
// Egyben bizonyíték, hogy a core valóban UI-független.
#include "tanara/Types.h"
#include <QCoreApplication>
#include <QTextStream>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    out << "tanara-cli " << tanara::libraryVersion() << "\n";

    // smoke: a contract-típusok használhatók
    tanara::AppSettings s;
    s.userSpeakerName = QStringLiteral("Ádám");
    out << "default language hint: " << s.languageHints.value(0) << "\n";
    out << "speaker: " << s.userSpeakerName << "\n";
    return 0;
}
