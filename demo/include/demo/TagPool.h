#pragma once
//
// tanara_demo: the ONE made-up tag pool (12 tags, ids as in the library demo data) that the
// recorder's tag input, the tag windows and the meeting tag rows all draw from. The names follow
// the demo language (Hungarian / English), the ids never change.
#include <QString>

#include <vector>

namespace tanara_demo {

struct PoolTag {
    const char* id;
    const char* hu;
    const char* en;
    QString name() const;   // in the demo language
};

namespace tagpool {

// The pool in display order.
const std::vector<PoolTag>& all();
// The tag with this id; nullptr if none.
const PoolTag* byId(const QString& id);
// The name of the tag with this id in the demo language (empty if none).
QString name(const char* id);

} // namespace tagpool

} // namespace tanara_demo
