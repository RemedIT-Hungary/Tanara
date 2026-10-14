#pragma once
// tanara-cli tags people|person|set-person: tags on people (TagService). Output is English.
#include <QStringList>

namespace tanara { namespace app { class AppController; } }

namespace tanara::cli {

// args[1] == "tags", args[2] in {people, person, set-person}. Returns the exit code, or -1 if
// the subcommand is not in this group.
int runTagPeopleCommand(app::AppController& app, const QStringList& args);

} // namespace tanara::cli
