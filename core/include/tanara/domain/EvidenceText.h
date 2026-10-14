#pragma once
//
// Domain: the display-time rendering of an Evidence (EvidenceKeys.h). Every consumer that shows an
// evidence (badges, tooltips, "Why them?" popovers) goes through these; the text follows the active UI
// language, so a stored analysis never freezes the language it was made in.
#include "tanara/domain/Participant.h"

namespace tanara {

// The short chip text ("voice 82%", "microphone"). For an Evidence without a key: its `text`.
QString evidenceText(const Evidence& e);
// The tooltip sentence. For an Evidence without a rendered detail: its `detail`.
QString evidenceDetail(const Evidence& e);
// The track sides the evidence is about ("mic" / "loopback"); empty if none (or unknown).
QStringList evidenceSides(const Evidence& e);

} // namespace tanara
