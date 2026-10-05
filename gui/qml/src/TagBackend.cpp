#include "TagBackend.h"

#include "TagDemoBackend.h"

namespace tanara_qml {

TagBackend* createControllerTagBackend(QObject* /*controller*/, QObject* /*parent*/)
{
    // Wave 2: itt jön létre a tanara::AppController::tags() / profiles() / tagSuggestions*
    // jeleire épülő backend. Addig nincs ilyen.
    return nullptr;
}

TagBackend* createTagBackend(QObject* controller, QObject* parent)
{
    if (!controller) return new TagDemoBackend(TagDemoBackend::Content::Sample, parent);
    if (TagBackend* backend = createControllerTagBackend(controller, parent)) return backend;
    return new TagDemoBackend(TagDemoBackend::Content::Empty, parent);
}

} // namespace tanara_qml
