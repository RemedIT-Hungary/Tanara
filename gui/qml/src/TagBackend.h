#pragma once
//
// The tag facade lives in the library module (tanara/library/tags/TagBackend.h); the view models
// keep using the short names of the tanara_qml namespace.
//
#include "tanara/library/tags/TagBackend.h"

namespace tanara_qml {

using tanara::library::PersonTagItem;
using tanara::library::TagAddSource;
using tanara::library::TagBackend;
using tanara::library::TagItem;
using tanara::library::TagMeetingItem;
using tanara::library::TagProfileItem;
using tanara::library::TagReasonItem;
using tanara::library::TagSuggestionItem;
using tanara::library::TagSuggestionState;

} // namespace tanara_qml
