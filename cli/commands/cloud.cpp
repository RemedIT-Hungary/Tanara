// tanara-cli, cloud module: the Tanara Cloud commands (kept for development; see CloudCommands.cpp).
#include "Commands.h"
#include "CloudCommands.h"

#include "tanara/app/AppController.h"

namespace tanara::cli {

void registerCloudCommands(CommandRegistry& r)
{
    r.add({"cloud", "cloud", "Tanara Cloud: account, balance, login, estimate, ...",
           "cloud status | login | key <api_key> | logout | use [cloud|byo]\n"
           "cloud tier <stt|llm> <fast|accurate> | lang <code|auto> | models\n"
           "cloud estimate <id> [transcribe|summarize] [--mode quick|complex]\n"
           "cloud accept-terms [version] | pending\n"
           "cloud waitlist <email> --consent [--use-case meetings|interviews|audio_files|other] [--langs hu,en]",
           true, [](CliContext& ctx, const QStringList& a) { return runCloudCommand(ctx.app(), a); }});
}

} // namespace tanara::cli
