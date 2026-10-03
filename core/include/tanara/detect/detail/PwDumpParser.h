#pragma once
//
// PwDumpParser — a `pw-dump` (PipeWire) JSON kimenetéből dönti el, folyik-e hívás.
// TISZTA függvény: nincs folyamat-indítás, nincs I/O → önállóan unit-tesztelhető
// (a LinuxCaptureDetector futtatja a pw-dump-ot és ide adja a nyers JSON-t).
//
// Jel: egy AKTÍV mikrofon-fogó stream — PipeWire Node, amelynek
//   info.props["media.class"] == "Stream/Input/Audio"  (egy app fogja a mikrofont)
//   info.state == "running"                             (nem szüneteltetett)
// és a tulajdonos bináris ISMERT hívás-app, de NEM a miénk (ön-kizárás).
//
#include "tanara/detect/IMeetingDetector.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace tanara::detail {

// json: a `pw-dump` teljes kimenete. knownApps: bináris/app-név részletek, amelyek
// hívás-appot jelölnek (pl. "zoom","teams","discord"). selfBinary: a saját processz
// binárisa (pl. "tanara"), amit sosem veszünk meetingnek. Nincs találat → {active:false}.
MeetingSignal parsePwDump(const QByteArray& json,
                          const QStringList& knownApps,
                          const QString& selfBinary);

// Ember-olvasható app-név a (kisbetűs) binárisból, ill. az application.name-ből: az ismert
// appokra szép nevet ad ("teams" → "Microsoft Teams"), különben az application.name, annak
// híján a bináris. (A lejátszás-útvonal figyelő — audio/PlaybackRouting.h — is ezt használja.)
QString prettyAppName(const QString& binaryLower, const QString& appNameProp);

} // namespace tanara::detail
