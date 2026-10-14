# tanara-cli

Szabály: a CLI (`cli/*.cpp`, `tanara-cli`) **csak angol** — parancsnevek, argumentum-helyőrzők
(`<folder>`, `<id>`, `<archive.zip>`), használati szöveg és minden kiírt üzenet sima angol
literál, **nincs** `tr()` / `QCoreApplication::translate("cli", …)`. A felhasználótól jövő adat
(címek, nevek) változatlanul megy ki; a core-ból érkező üzenetekhez a CLI mindig az angol
fordítást tölti be. A kód-kommentek magyarok, mint a repó többi részén. A GUI-ra ez nem vonatkozik.

## Felépítés (P14)

- `CommandRegistry.{h,cpp}`: a parancs-tábla (`Command{name, module, summary, usage, needsServices, run}`),
  a `CliContext` (lustán épített `AppController` / `app::Services`), a `help` és a „Did you mean” javaslat.
- `commands/<modul>.cpp`: modulonként egy `registerXCommands` (recording, watcher, library, processing,
  editing, people, cloud, migrate); a mappán dolgozó parancsok külön fájlban (`commands/*Command*.cpp`).
- Új parancs: add hozzá a modul fájljához egy `r.add({...})` sorral; a `help` és a `test_cli_help` magától látja.
- `tanara-cli help` modulonként csoportosít, `help <parancs>` a használatot adja, `help --json` gépi lista.
