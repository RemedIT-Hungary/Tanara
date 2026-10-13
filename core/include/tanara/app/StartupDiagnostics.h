#pragma once
//
// Startup diagnostics (app module): version, platform, resolved folders, settings (SECRETS OMITTED),
// provider selection, audio devices seen, meetings in the store. It knows the AppController, so it
// lives in the composition layer, not in foundation logging.
// The detailed dump appears at DEBUG level; the header at INFO.

namespace tanara::app {

class AppController;

void logStartupDiagnostics(const AppController& app);

} // namespace tanara::app