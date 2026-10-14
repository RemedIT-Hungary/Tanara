#include "demo/NoteDemoData.h"

#include "demo/DemoText.h"

namespace tanara_demo {

QVector<DemoNoteSuggestion> noteSuggestions()
{
    const QStringList tags{QStringLiteral("Nordvik")};
    return {
        {demoText("Nordvik heti meeting", "Nordvik weekly meeting"), demoText("2026. szept. 29.", "Sep 29, 2026"),
         demoText("Résztvevők: Kovács Anna, Szabó Bence, Lantos Réka. A „Nordwig” helyesen: Nordvik. "
                  "A „kvarc modul” helyesen: Qvarko-modul. Téma: a bevezetés ütemezése.",
                  "Participants: Anna Carter, Noah Turner, Rachel Lane. “Nordwig” is really Nordvik. “quark module” is really Qvarko module. Topic: rollout schedule."),
         tags},
        {demoText("Nordvik heti meeting – demó előtt", "Nordvik weekly meeting – before the demo"), demoText("2026. szept. 22.", "Sep 22, 2026"),
         demoText("A „pixel tár” helyesen: PixelTár. Résztvevők: Kovács Anna, Szabó Bence.",
                  "“pixel vault” is really PixelVault. Participants: Anna Carter, Noah Turner."),
         tags},
    };
}

} // namespace tanara_demo
