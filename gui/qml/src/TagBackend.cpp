#include "TagBackend.h"

#include "TagControllerBackend.h"
#include "TagDemoBackend.h"

#include "tanara/AppController.h"

namespace tanara_qml {

TagBackend* createControllerTagBackend(QObject* controller, QObject* parent)
{
    auto* app = qobject_cast<tanara::AppController*>(controller);
    return app ? new TagControllerBackend(app, parent) : nullptr;
}

TagBackend* createTagBackend(QObject* controller, QObject* parent)
{
    if (!controller) return new TagDemoBackend(TagDemoBackend::Content::Sample, parent);
    if (TagBackend* backend = createControllerTagBackend(controller, parent)) return backend;
    return new TagDemoBackend(TagDemoBackend::Content::Empty, parent);
}

} // namespace tanara_qml
