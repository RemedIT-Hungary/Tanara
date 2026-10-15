#pragma once
//
// The user-facing text of a transport error. QNetworkReply::errorString() is localised by the SYSTEM
// locale (not by Tanara's UI language), so it must never reach the UI: the machine field is the
// QNetworkReply::NetworkError code, the human text comes from this one tr()-based mapper. Logs keep the
// raw Qt text next to the code.
#include <QString>

namespace tanara::net {

// `code` is a QNetworkReply::NetworkError value (0 = unknown / not recorded). Mapped codes render as a
// translated sentence fragment; an unmapped code (or 0) falls back to `rawQtText` (may be empty).
QString networkErrorText(int code, const QString& rawQtText = QString());

} // namespace tanara::net
