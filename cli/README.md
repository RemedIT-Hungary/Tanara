# tanara-cli

Szabály: a CLI (`cli/*.cpp`, `tanara-cli`) **csak angol** — parancsnevek, argumentum-helyőrzők
(`<folder>`, `<id>`, `<archive.zip>`), használati szöveg és minden kiírt üzenet sima angol
literál, **nincs** `tr()` / `QCoreApplication::translate("cli", …)`. A felhasználótól jövő adat
(címek, nevek) változatlanul megy ki; a core-ból érkező üzenetekhez a CLI mindig az angol
fordítást tölti be. A kód-kommentek magyarok, mint a repó többi részén. A GUI-ra ez nem vonatkozik.
