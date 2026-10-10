#pragma once
// tanara-cli tags people|person|set-person — címkék a személyeken (TagService). Kimenet ANGOL.
#include <QStringList>

namespace tanara { class AppController; }

namespace tanara::cli {

// args[1] == "tags", args[2] ∈ {people, person, set-person}. Vissza: kilépési kód; -1, ha az
// alparancs nem ez a csoport.
int runTagPeopleCommand(AppController& app, const QStringList& args);

} // namespace tanara::cli
