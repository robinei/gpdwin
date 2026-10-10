# Pegasus Frontend on-disk data: implementation spec for a compatible C launcher

Source: Pegasus Frontend at commit `c3462e68` (the installed `pegasus-frontend-stable-git alpha16.r82.gc3462e68-1`,
log string `alpha16-82-gc3462e68-dirty`), Qt 5.15.19. The built-in grid theme is the submodule at
`d7533c8`. All `file:line` references are relative to `src/backend/` unless they say otherwise. Everything here
comes from the code, not the docs.

The local AUR patches (`aur/patches/pegasus-frontend-stable-git/0001..0004`, removed from the repo 2026-10-10
along with all Pegasus integration) only changed animations, gamepad polling and a "Starting..." overlay. None
of them touched data handling.

shelf implements this spec except the theme memory (1.6): since Pegasus stopped being the frontend
(2026-10-10), shelf keeps its place in its own state file and neither reads nor writes `theme_settings/`.

Throughout, "**unused here**" marks a feature that the real setup (section 8) does not use, so the C version can
skip it. Anything not marked is used and must be implemented.

---

## 0. Summary of the files involved

| File | Location | Pegasus reads | Pegasus writes | Used here |
|---|---|---|---|---|
| `game_dirs.txt` | every config dir (see 1.2) | at each scan | only from the Settings UI (add/remove dir) | yes (symlink into the repo) |
| `settings.txt` | `~/.config/pegasus-frontend/` only | at startup | whenever any setting is changed in the UI (rewrites the whole file) | yes, 1 line |
| `favorites.txt` | `~/.config/pegasus-frontend/` | at each scan | whenever a favourite is toggled (rewrites the whole file) | **file does not exist** |
| `stats.db` | `~/.config/pegasus-frontend/` | at each scan | after each game exits | yes (40 paths, 235 plays) |
| `theme_settings/<theme>.json` | `~/.config/pegasus-frontend/` | on theme load | on each `api.memory.set` (the grid theme calls it at every launch) | yes |
| `metafiles/*` | `~/.config/pegasus-frontend/metafiles/` | at each scan | never | **unused here** (dir absent) |
| `scripts/<event>/*` | every config dir | on events | never | **unused here** |
| `lastrun.log` | `~/.config/pegasus-frontend/` | never | always | Pegasus-owned, ignore |
| `metadata.pegasus.txt` and similar | top level of each game dir | at each scan | never | yes, 6 files |
| `media/` and `.media/` | inside each collection base dir | at each scan | never | yes |

---

## 1. Config directory and config files

### 1.1 The writable config dir

`paths::writableConfigDir()` (`Paths.cpp:52-65`, `149-153`):

- Normal mode: `QStandardPaths::writableLocation(AppConfigLocation)`. The organisation and application are both
  `pegasus-frontend` (`app/main.cpp:63-65`), so this would be `$XDG_CONFIG_HOME/pegasus-frontend/pegasus-frontend`.
  `remove_orgname()` (`Paths.cpp:40-44`) collapses the doubled suffix, giving
  **`${XDG_CONFIG_HOME:-$HOME/.config}/pegasus-frontend`**. The directory is created if missing (`mkpath`).
- Portable mode (`--portable`, or a `portable.txt` next to the binary, `app/main.cpp:73,91-97`):
  `<app dir>/config`. **Unused here.**
- On the device this is `/home/robin/.config/pegasus-frontend`.

### 1.2 The config search list (for `game_dirs.txt` and `scripts/`)

`paths::configDirs()` (`Paths.cpp:113-147`) is, in order, with duplicates removed:

1. `:` (the Qt resource root, so in effect never a real file),
2. the app dir (`QCoreApplication::applicationDirPath()`, i.e. `/usr/bin`),
3. `<app dir>/config`, if it exists,
4. if not portable: the writable config dir, then every `QStandardPaths::standardLocations(AppConfigLocation)`
   (`~/.config/pegasus-frontend`, then `$XDG_CONFIG_DIRS/pegasus-frontend`, by default
   `/etc/xdg/pegasus-frontend`), then every `AppDataLocation` (`~/.local/share/pegasus-frontend`,
   `/usr/local/share/pegasus-frontend`, `/usr/share/pegasus-frontend`), each with the doubled
   `/pegasus-frontend/pegasus-frontend` collapsed.

On the device only `~/.config/pegasus-frontend/game_dirs.txt` exists, and
`/usr/share/pegasus-frontend` and `/etc/xdg/pegasus-frontend` do not.

### 1.3 `game_dirs.txt`

Read by `AppSettings::parse_gamedirs` (`AppSettings.cpp:221-241`) and `read_game_dirs()`
(`providers/SearchContext.cpp:37-49`):

- `game_dirs.txt` is read from **every** directory in 1.2, and the lines of all of them are concatenated.
- UTF-8. Lines are read with a maximum length of 4096 characters.
- A line that starts with `#` (column 0) is skipped. Lines are **not trimmed**: a trailing space becomes part of
  the path.
- Each line is passed to `QFileInfo(line)`. A relative line is relative to Pegasus' **current working
  directory** (on the device, `/home/robin`). `~` is not expanded. The line is kept only if `isDir()` is true
  (symlinks to dirs count), so empty lines and missing dirs drop out silently.
- Each kept path is turned into `clean_abs_path` = `QDir::cleanPath(absoluteFilePath)` (`utils/PathTools.cpp:24-26`):
  absolute, `.` and `..` resolved lexically, duplicate and trailing slashes removed. **Symlinks are not resolved.**
- Duplicates are removed (`removeDuplicates`, which keeps the first occurrence).
- **The order of the lines does not matter**, because the metadata files found in them are sorted afterwards (2b.1).

Writing (Settings UI only, `model/internal/settings/Settings.cpp:43-57,133-200`): Pegasus collects the existing
lines into a `QSet` (passing each through `pretty_path`, the same as `cleanPath` on Linux), adds or removes one,
and rewrites `writableConfigDir/game_dirs.txt` as `path\n` per entry. Add-dir writes them in **hash order**
(unsorted), remove-dir writes them sorted. Comments are lost. The file is opened with `QFile::WriteOnly`, which
writes through a symlink, so the repo copy would be changed. A C launcher only needs to read this file.

### 1.4 `settings.txt`

Path: `writableConfigDir()/settings.txt` only (`parsers/SettingsFile.cpp:89-91`). It uses the **same line
syntax as metadata files** (it is parsed by `metafile::read_file`, `SettingsFile.cpp:98-109`, see 2.1). Keys are
dotted:

- `general.fullscreen`, `general.input-mouse-support`, `general.verify-files`: booleans. Accepted values are
  `yes/on/true/no/off/false`, case-insensitive (`utils/StringHelpers.cpp:38-53`). Defaults are fullscreen=true,
  mouse=true, verify-files=true (`AppSettings.h:41-46`).
- `general.locale` (string) and `general.theme` (a path relative to the config dir; `SettingsFile.cpp:181-183`).
- `providers.<codename>.enabled` and other provider options. Internal providers (`pegasus_metafiles`,
  `pegasus_favorites`, `pegasus_playtime`) cannot be configured. `pegasus_media` can.
- `keys.<event>`: a comma-separated key list, `none` for empty, `Comma` for a literal comma, and `GamepadA` and
  similar for pad buttons (`SettingsFile.cpp:227-264`).
- Unknown keys give a log warning and are otherwise ignored.

Writing (`SettingsFile.cpp:274-381`) happens whenever any setting is changed in the UI. It rewrites the **whole
file** with every general option, every non-internal provider's `enabled` line and options, and every key line, in
hash order, formatted as `%1.%2: %3\n`. Then it runs the `config-changed` and `settings-changed` scripts.
Unknown lines are lost.

**Here:** the file is one hand-written line, `general.fullscreen: false`, so Pegasus has never saved it.
`verify-files` is therefore true. The C launcher should treat this file as read-only and keep its own settings in
a separate file, because Pegasus would drop unknown keys the next time it saves.

### 1.5 `favorites.txt`: see section 6. `stats.db`: see section 5.

### 1.6 `theme_settings/<theme-dir-name>.json` (theme memory)

`model/memory/Memory.cpp:32-66,122-161`. The file name is the theme's root directory name, so for the built-in
grid theme it is `theme_settings/pegasus-theme-grid.json`. The content is the theme's `api.memory` map serialised
with `QJsonDocument::toJson(Compact)`, so keys come out sorted and there is no trailing newline. The file is
rewritten on every `set`/`unset`. If the JSON is invalid, Pegasus logs a warning and starts with an empty map, so
it does not crash.

The grid theme (`src/themes/pegasus-theme-grid/theme.qml:139-171` @ `d7533c8`) does this at every launch:
`set('collection', <collection.name>)` then `set('game', <game.title>)`. At startup it restores the selection by
matching the collection **name** exactly, then the game **title** exactly within that collection's (sorted) game
list. On the device the file contains `{"collection":"Utilities","game":"WiFi"}`.

*Optional for the C launcher:* write this file in the same format when it launches a game, so that Pegasus opens
on the same game.

### 1.7 Scripts (**unused here**)

`ScriptRunner.cpp:32-102`. For every config dir, executable regular files under `scripts/<event>/` (recursively,
sorted per directory) are run synchronously with `QProcess::execute`. The events are `quit`, `reboot`, `shutdown`,
`config-changed`, `settings-changed`, `controls-changed`, `game-start` (one argument: the absolute path of the
game file) and `game-end`. The device has no `scripts/` directory.

---

## 2. The `metadata.pegasus.txt` format

### 2.1 Line syntax (`parsers/MetaFile.cpp:66-146`)

Read as UTF-8 text through `QTextStream` (a BOM is skipped, and `\r` is removed by trimming). For each physical
line:

1. If the line **starts with `#` at column 0**, skip it (`:93`). This does not close the current entry. A `#`
   after leading whitespace is **not** a comment: it is a continuation line whose content is `# ...`.
2. If the line is empty or whitespace only, **close the current entry** (`:96-100`). Blank lines end an entry.
   They are **not** paragraph breaks.
3. If the line starts with whitespace (`QChar::isSpace`: space, tab, and so on) and has content, it is a
   **continuation line** (`:103-116`):
   - If no entry is open (no key yet, for example after a blank line), log an error and ignore the line.
   - If the trimmed content is exactly `.`, append a **null value** (a paragraph-break marker).
   - Otherwise append the trimmed content as another value line.
4. Otherwise close the current entry, then look for the **first** `:` in the trimmed line (`:126`):
   - If it is at position > 0, the key is `trimmed(left part).toLower()` (`:128`). If the right part, trimmed,
     is non-empty, it becomes the first value. The value may contain more colons (`game: Diablo: Hellfire` gives
     the value `Diablo: Hellfire`).
   - Otherwise (no colon, or a colon at position 0) log `line invalid` and skip the line (`:140`).
5. At EOF, close the current entry (`:144-145`).

"Closing" an entry (`:76-86`): if the key is non-empty and has no values, log `attribute value missing` and
drop it; otherwise dispatch it. So `key:` with an empty value and no continuation lines is dropped.

An entry is therefore `{line, key (lowercased), values: [line1, line2, ...]}`. A value line is either non-empty
trimmed text or null (from `.`).

**Keys are case-insensitive** (lowercased). Values keep their case.

### 2.2 How multi-line values are combined

There are two modes:

- **`merge_lines`** (`MetaFile.cpp:151-181`). It is used for `summary`, `description` and `launch`/`command`
  (collection and game), and for the settings file. Lines are joined with a single space. A null line (`.`)
  appends `"\n\n"`, and the next line is appended **without** a leading space. The result is then trimmed.
  For `summary` and `description` only, `replace_newlines` (`PegasusMetadata.cpp:199-203`) then turns the
  two-character escape `\n` (not preceded by a backslash) into a real newline, and turns `\\n` into a literal
  `\n`.
  - Example: `description: A` / `  B` / `  .` / `  C` gives `"A B\n\nC"`.
- **`first_line_of`** (`PegasusMetadata.cpp:185-197`). It is used for single-value keys (`collection`, `game`,
  `shortname`, `workdir`, `extensions`, `regex`, `sort-by`, `players`, `release`, `rating`). Only `values[0]` is
  used. Extra lines give a warning and are ignored. **Note:** `extensions` spread over several lines only uses the
  first line.
- **List keys** (`file(s)`, `developer(s)`, `publisher(s)`, `genre(s)`, `tag(s)`, `directory/directories`,
  `assets.*`, `x-*`) use **each value line as one item**. Only `extensions` is comma-split. `genre: Action, RPG` is
  one genre, `"Action, RPG"`.

### 2.3 Entry dispatch (`PegasusMetadata.cpp:499-545`)

Parser state per file: `cur_coll`, `cur_game`, `all_colls` (the collections seen so far **in this file**), and
`filters` (one FileFilter per `collection:` line).

- `collection: <name>` (`:506-516`): `get_or_create_collection(name)`. Collections are global and keyed by the
  **exact name** across all files (`SearchContext.cpp:71-80`). The parser sets the collection's
  `common_relative_basedir` to this file's directory (later files overwrite it), sets `cur_game = null`, appends
  the collection to `all_colls`, and pushes a new `FileFilter{collection, directories=[this file's dir]}`.
- `game: <title>` (`:518-528`): creates a new Game, sets its title (which also sets `sort_by` = title if
  `sort_by` is still empty, `model/gaming/Game.cpp:54-60`), and sets its launch basedir to this file's dir. Then
  it is **added to every collection declared so far in this file** (`all_colls`, not just the last one) by
  `game_add_to` (2.5). A `game:` before any `collection:` in its file belongs to no collection (see 2.7).
- Any other key with neither `cur_coll` nor `cur_game` set: warning, ignored.
- Next, a key starting with `x-` is stored as an extra (`:431-455`): `extra[key without "x-"] = [value lines]`,
  on the current game if there is one, otherwise on the collection. `x-` with nothing after it gives a warning.
  Used here only for `x-shelf-kind: tools` on the Utilities collection (shelf's tools tab; Pegasus keeps and
  ignores it).
- Next, a key matching `^assets?\.(.+)$` (`:158`, so `asset.` also works) is an asset (3.2).
- Otherwise, if `cur_game` is set, the key is a game key (2.4.2). Otherwise it is a collection key (2.4.1).
  Unknown keys give a warning and are ignored.

### 2.4 Recognised keys

#### 2.4.1 Collection keys (`PegasusMetadata.cpp:92-116`, applied at `:205-286`)

| Key(s) (lowercased) | Meaning | Value mode | Used here |
|---|---|---|---|
| `shortname` | `short_name = value.toLower()`; defaults to `name.toLower()` (`model/gaming/Collection.cpp:26-36`) | first line | yes |
| `launch`, `command` | `common_launch_cmd` | merge_lines | yes |
| `workdir`, `cwd` | `common_launch_workdir` | first line | unused here |
| `directory`, `directories` | extra scan dirs, each relative to the metafile dir. A path that is not an existing dir gives a warning and is skipped. Stored as `clean_abs_path`. | per line | unused here |
| `extension`, `extensions` | comma-split, trimmed, lowercased, empty items dropped; **appended** to the include list | first line | yes |
| `file`, `files` | include file list (relative paths, resolved later against **each** scan dir) | per line | unused here |
| `regex` | include regex (QRegularExpression/PCRE). If invalid, warning and ignored. **Replaces** any earlier one. | first line | unused here |
| `ignore-extension(s)` | as `extensions`, into the exclude group | first line | unused here |
| `ignore-file(s)` | as `files`, into the exclude group | per line | unused here |
| `ignore-regex` | as `regex`, into the exclude group | first line | unused here |
| `summary` | collection summary | merge_lines + `\n` escapes | yes (pc, utils) |
| `description` | collection description | merge_lines + `\n` escapes | unused here |
| `sortby`, `sort_by`, `sort-by` | collection `sort_by` (default = name) | first line | unused here |

Each `collection:` line gets **its own** filter, even when the same collection name appears again. A later
`collection: X` line in the same or another file adds a new filter, and the collection's
`launch`/`workdir`/`summary` setters simply overwrite earlier values.

#### 2.4.2 Game keys (`PegasusMetadata.cpp:117-147`, applied at `:288-428`)

| Key(s) | Meaning | Mode | Used here |
|---|---|---|---|
| `file`, `files` | the game's files (2.6) | per line | yes |
| `launch`, `command` | game launch cmd (overrides the collection's) | merge_lines | unused here |
| `workdir`, `cwd` | game workdir | first line | unused here |
| `developer(s)`, `publisher(s)`, `genre(s)`, `tag(s)` | appended to the lists; duplicates removed at the end (`SearchContext.cpp:268-271`) | per line | developer/publisher/genre yes (pc only), tags unused |
| `players` | `^(\d+)(-(\d+))?$`, value = max(a, b), then max(1, ·). No match: **silently ignored** | first line | yes (`1-4`, `1`) |
| `summary` | short description | merge_lines + escapes | unused here |
| `description` | long description | merge_lines + escapes | yes |
| `release` | `^(\d{4})(-(\d{1,2}))?(-(\d{1,2}))?$`; y = max(1, y), m clamped to 1..12, d clamped to 1..31; an invalid date such as Feb 30 is rejected | first line | yes (pc, `YYYY-MM-DD`) |
| `rating` | `^\d+%$` gives n/100; else `^\d(\.\d+)?$` gives a float (one digit before the dot); clamped to 0..1 | first line | unused here |
| `sorttitle`, `sortname`, `sort_title`, `sort_name`, `sort-title`, `sort-name`, `sortby`, `sort_by`, `sort-by` | game `sort_by` | first line | unused here |
| `x-*` | extras | per line | `x-shelf-kind` only (shelf) |
| `assets.*` / `asset.*` | 3.2 | per line | yes (`assets.boxFront`) |

Defaults (`model/gaming/Game.h:38-69`): players 1, rating 0.0, release date invalid (unset), not a favourite.

### 2.5 Launch/workdir inheritance (`SearchContext.cpp:82-92`, `163-176`)

`game_add_to(game, coll)` adds the game to the collection, then **for each of** `launch_cmd`, `launch_workdir`
and `launch basedir`, copies the collection's **current** value if the game's value is empty. Notes:

- For a `game:` entry, `game_add_to` runs on the `game:` line, before that game's own keys. A later
  `launch:`/`workdir:` on the game then overwrites the inherited value. The game's basedir is already set to its
  own file's dir, so it is never inherited.
- The values copied are whatever the collection holds **at that moment**. For a `game:` entry, that is the value
  after all earlier files (sorted, 2b.1) and the earlier part of the current file have been parsed. Games found by
  extension scanning are created with `create_game_for(coll)` *after all files are parsed*, so they get the
  collection's **final** values, including the basedir of the last file that declared the collection.
- A game in several collections takes the launch command of the **first** collection it is added to that has a
  non-empty one.
- **Here:** both files that declare `PC Games` set `launch: "{file.path}"`, so the timing makes no difference.

### 2.6 How `game:` files are attached (`PegasusMetadata.cpp:300-339`, `SearchContext.cpp:133-161`)

For each `file:` line:

- If it matches the URI regex `^[a-zA-Z][a-zA-Z0-9+\-.]+:.+` (`:164`), it is a URI file (for example
  `steam:123`). It is matched by exact string. **Unused here.** Note that a relative path such as `C:foo` or
  `ab:cd` also matches this regex.
- Otherwise `QFileInfo(metafile_dir, line)`: absolute lines stay absolute, relative ones are relative to the
  **metadata file's directory**. `~` is not expanded. If `verify_files` is true (the default) and the path does not
  exist, warning and skip. The key is `clean_abs_path` (lexical, **no symlink resolution**).
- If that path already belongs to the **same** game: "Duplicate" warning, skip. If it belongs to **another**
  game: "already belongs to a different game" warning, skip. Otherwise create a GameFile.
- A GameFile's display name is `pretty_filename` = `completeBaseName` (everything before the **last** dot) with `_`
  and `.` replaced by spaces (`model/gaming/GameFile.cpp:24-29`). When a game gets its first file and its title is
  still empty (only for auto-detected games), the title and sort_by are set to that name (`SearchContext.cpp:143-146`).

### 2.7 Which files a collection's filter includes (`providers/pegasus_metadata/PegasusFilter.cpp:125-178`)

Filters run **after all metadata files have been parsed** (`PegasusProvider.cpp:111-116`), in the order the
`collection:` lines were parsed. For each filter:

1. Duplicates are removed from (and the vectors sorted): directories, include/exclude extensions, include/exclude
   files (`:127-131`).
2. **Explicit files** (`:136-144`): every include `files:` entry is resolved against **every** filter directory
   (`resolve_filelist`, `:51-68`). For each result that is not in the resolved exclude files, and that exists when
   `verify_files` is set, the file is accepted (step 5). **Extension and regex rules do not apply to explicit
   files.**
3. A directory scan happens only if there is at least one include extension or a non-empty valid include regex
   (`:146-149`).
4. For each filter directory D:
   - **Direct files of D** (`:157-164`): `QDir::Files | NoDotAndDotDot`, which means regular files and symlinks
     to files, **no hidden files** (names starting with `.`), and no broken symlinks.
   - **Every direct subdirectory of D except exactly `D/media`** (`:32-49`). Hidden subdirectories (including
     `.media`) are skipped because `QDir::Hidden` is not set. Each subdirectory is scanned **recursively without a
     depth limit**, following symlinks, with `QDir::Dirs | QDir::Files | NoDotAndDotDot` (`:166-176`). Because
     `Dirs` is included, **directory entries are also tested**, so a directory named `foo.iso` inside a subdir is
     accepted as a game. Hidden entries are skipped at every level. Only the top-level `media` is excluded: a
     `sub/media/x.png` would be tested like any other file.
   - Test for each entry (`file_passes_filter`, `:74-93`), where `ext = suffix().toLower()` (the text after the
     **last** dot, empty if there is none):
     - **excluded** if `ext ∈ exclude.extensions`, or `clean_abs_path ∈ resolved exclude files`, or the
       exclude regex (if any) matches `finfo.filePath()`;
     - otherwise **included** if `ext ∈ include.extensions` or the include regex matches `finfo.filePath()`.
     - Regexes are unanchored *searches*, case-sensitive, on the full path string as iterated
       (`D/sub/.../name`; D is absolute).
5. **Accepting** a path (`accept_filtered_file`, `:95-108`): if a GameFile with that exact `clean_abs_path` already
   exists (from a `game:` entry or another collection), the scan **reuses its game** and only adds it to this
   collection. Otherwise it creates a new game (inheriting launch settings, 2.5) with that one file and the
   filename-derived title.

### 2.8 Merging `game:` entries with auto-detected files

- Metadata parsing runs before any scanning, so a `game:` whose `file:` points at a file the scan also finds
  becomes **one game**: the metadata title and fields, with membership in every collection whose filter matched.
- Matching is **exact string equality of the clean absolute path**. A `game:` that refers to the file through a
  different path (another symlink, or `..` that resolves elsewhere) is a **different** game, and you get a
  duplicate.
- **A game can have several files** (several `file:` lines). That is **unused here**: every game has exactly
  one file.
- In `.cue`/`.bin` sets only the extension that is listed matters. Here PS1 lists `cue` but not `bin`, so the
  `.bin` is ignored.

### 2.9 Final cleanup (`SearchContext.cpp:178-251`)

- Games that were never added to any collection ("parentless") are deleted with a warning. Example: a `game:`
  before any `collection:` whose file no filter matched.
- Games with no files (all `file:` lines missing or duplicates, or no `file:` at all) are removed from every
  collection.
- Collections with no games left are deleted with a warning (the device log shows this for the built-in `GOG`
  provider's empty collection).
- Developer, publisher, genre and tag lists are de-duplicated (keeping the first occurrence).

### 2.10 Ordering

- **Collections** are sorted by `sort_by` (default = name) with `QString::localeAwareCompare`
  (`SearchContext.cpp:296`, `model/gaming/Collection.cpp:54-56`).
- **Games in a collection** (`Collection::setGames`, `Collection.cpp:44-52`) and the global list
  (`SearchContext.cpp:297`) are sorted by `sort_by` (default = title, or the pretty filename for auto-detected
  games) with `localeAwareCompare` (`Game.cpp:148-150`). `std::sort` is not stable, so equal keys come out in an
  arbitrary order.
- **A game's files** are sorted by pretty name, the same way (`Game.cpp:127`, `GameFile.cpp:65-67`).
- The grid theme does **not** re-sort: it shows `collection.games` through a filter-only proxy
  (`theme.qml:115-135`).
- `localeAwareCompare` in this Qt build uses **ICU collation** for the current locale (`LANG=en_US.UTF-8` on the
  device; libQt5Core links libicu). ICU's default treats spaces and punctuation as significant and ranks them
  before letters. **glibc `strcoll()` does not give the same order.** I checked both on this data:
  - ICU (Pegasus): `Cave Story+` < `Caves of Qud`, and `Game Saves` < `Games`.
  - glibc `strcoll`/`sort`: `Caves of Qud` < `Cave Story+`, and `Games` < `Game Saves`.
  - To match exactly, use ICU's C API (`ucol_open("en_US")` + `ucol_strcoll`, with libicu already on the device),
    or a key of the form "case-folded string, whitespace < punctuation < digits < letters".
- The expected order here (ICU):
  - Collections: PC Games, PlayStation, PSP, Super Nintendo, Utilities.
  - Utilities: BIOS Setup, Fetch Box Art, Game Saves, Games, Maintenance, RetroArch, System Monitor, Terminal,
    Update System, WiFi.
  - PC Games: A Short Hike, Bastion, Cave Story+, Caves of Qud, Commander Keen Complete Pack, Death Road to
    Canada, Diablo, Diablo: Hellfire, FEZ, Heretic + Hexen, Hyper Light Drifter, Kingdom: Classic, Psychonauts,
    Risk of Rain (2013), Sam & Max 101: Culture Shock, Spelunky, Starcom: Nexus, Stardew Valley, SteamWorld
    Heist, Strife: Veteran Edition, Super Meat Boy, The Binding of Isaac: Rebirth, The Legend of Zelda: A Link to
    the Past, Undertale, Unepic. Note that there is no article stripping: "The ..." sorts under T.

### 2.11 Relative path rules (summary)

| Item | Relative to |
|---|---|
| `game_dirs.txt` line | Pegasus' process CWD |
| game `file:` | the metadata file's dir |
| collection `directories:` | the metadata file's dir |
| collection `files:` / `ignore-files:` | **each** filter dir (the metafile dir plus `directories:`) |
| `assets.*` value | the metadata file's dir (http/https URLs are kept as they are) |
| `launch` command token containing `/` or `\` | the game's launch basedir (the dir of the metafile that declared the `game:`, or for scanned games the dir of the **last** metafile that declared the collection) |
| `workdir` | the same launch basedir |
| `favorites.txt` line | the config dir |

The path is always `QDir::cleanPath(absolute)`: lexical only, no `realpath`, no `~`.

---

## 2b. Multiple metadata files and directories

### 2b.1 Discovery and order (`providers/pegasus_metadata/PegasusProvider.cpp:32-119`)

- In `writableConfigDir()/metafiles/` and in **the top level of each game dir** (not recursive), any regular file
  (or symlink to one) whose name is exactly `metadata.pegasus.txt` or `metadata.txt`, or ends with
  `.metadata.pegasus.txt` or `.metadata.txt`, is a metadata file. Hidden files are not listed.
- All paths (`clean_abs_path`) are collected, then **sorted lexicographically** (`VEC_REMOVE_DUPLICATES` is
  `std::sort` + `unique`, `utils/StdHelpers.h:27-29`; QString `<` compares UTF-16 code units, so for ASCII it is
  byte order) and de-duplicated. **The files are parsed in that sorted order**, whatever the order of the lines in
  game_dirs.txt. The device log confirms this: installed, pc, ps1, psp, snes, utils.

### 2b.2 Merging rules

- **Collections are merged by exact name** across files: `PC Games` is declared in both
  `~/Games/installed/metadata.pegasus.txt` and `~/Games/pc/metadata.pegasus.txt`, and gives **one** collection with
  25 games. Scalar collection properties (`shortname`, `launch`, `workdir`, `summary`, `description`, `sort-by`,
  and the launch basedir) are **last writer wins** in the sorted file order. Assets and extras accumulate. Each
  declaration has its own file filter, rooted at its own dir.
- **Games are never merged by title.** Two `game:` entries for the same file: the first one parsed (in sorted file
  order, then line order) gets the file. The second one gets a warning, ends up with no files, and is dropped.
  Its metadata is **not** merged into the first.
- **The same game in two collections:** there is one Game object listed in both. That happens if:
  1. the `game:` line follows two `collection:` lines in the same file (it joins every collection declared earlier
     in that file), or
  2. two collections' filters match the same file path, or
  3. a `game:` entry's file is also matched by another collection's filter.
  Title and metadata come from the single `game:` entry (or from the filename). Play stats and favourite status
  are per Game, so they are shared.
- Global `metafiles/` work the same way. They are **unused here**.

---

## 3. Assets

### 3.1 Asset types and name aliases (`PegasusAssets.cpp:28-100`, `types/AssetType.h`)

| Asset type (QML property) | Accepted names (case-sensitive; exact match first) |
|---|---|
| boxFront | `boxfront`, `boxFront`, `box_front`, `boxart2D`, `boxart2d` |
| boxBack | `boxback`, `boxBack`, `box_back` |
| boxSpine | `boxspine`, `boxSpine`, `box_spine`, `boxside`, `boxSide`, `box_side` |
| boxFull | `boxfull`, `boxFull`, `box_full`, `box` |
| cartridge | `cartridge`, `disc`, `cart` |
| logo | `logo`, `wheel` |
| marquee | `marquee` |
| bezel | `bezel`, `screenmarquee`, `border` |
| panel | `panel` |
| cabinetLeft / cabinetRight | `cabinetleft`, `cabinetLeft`, `cabinet_left` / likewise for right |
| tile | `tile` |
| banner | `banner` |
| steam | `steam`, `steamgrid`, `grid` |
| poster | `poster`, `flyer` |
| background | `background` |
| music | `music` |
| screenshot | `screenshot`, `screenshots` |
| titlescreen | `titlescreen` |
| video | `video`, `videos` |

If there is no exact match, the **first map entry that is a prefix of the name** is used (`:94-97`), for example
`screenshot2` gives screenshot. The map is a hash map, so when several prefixes match the choice is
**non-deterministic**: `boxFront2` can become boxFront or boxFull (via `box`).

### 3.2 `assets.*` keys in metadata (`PegasusMetadata.cpp:458-497`)

- Key: `assets.<name>` or `asset.<name>`. Because keys are lowercased, `assets.boxFront` arrives as `boxfront`,
  which is in the map. An unknown name gives a warning and is ignored.
- Each value line is one asset:
  - a value starting with `http://` or `https://` is kept as it is (**unused here**);
  - otherwise `QFileInfo(metafile_dir, value)`. If `verify_files` is set and the file is missing, it is skipped
    with a warning. Otherwise it is stored as `QUrl::fromLocalFile(absoluteFilePath).toString()`, a `file://` URL.
    The path is absolute but not cleaned.
- There is **no extension check** for `assets.*`, so `.jpg` and `.jpeg` both work. (`gpd-cover.jpg` is used
  here.)
- The asset goes on the current game, or on the collection if no game is open yet.

### 3.3 `media/` folder convention (`providers/pegasus_media/MediaProvider.cpp:37-129`)

This runs after metadata parsing and filtering. The provider order is `AppSettings.cpp:98-140`.

- Media roots: for each **filter directory** of every collection (`pegasus_game_dirs`: each metafile's dir plus
  its `directories:`), `<dir>/media` and `<dir>/.media`. These are walked recursively, following symlinks, files
  only.
- Each media file `F` has a lookup key: `clean_abs_dir(F)` with the `/media` (or `/.media`) segment removed
  directly after the root. For `<dir>/media/<X>/boxFront.png` the key is `<dir>/<X>`. For
  `<dir>/media/sub/<X>/a.png` the key is `<dir>/sub/<X>`.
- Lookup table, for every game file registered so far (`MediaProvider.cpp:65-83`): `clean_abs_dir(file) + "/" +
  completeBaseName(file)`, **and** `clean_abs_dir(file) + "/" + game.title`. The first insertion wins.
- So a game's media folder is **`<game file's dir, relative to the media root's parent>/media/<file name without
  its last extension>/`** or **`.../media/<game title>/`**. Files directly in `media/` (such as `media/wifi.png`)
  map to the key `<dir>` and match no game.
- File test: `str_to_type(completeBaseName(F))` (3.1), and the suffix must be in the allowed list,
  **case-sensitively**:
  - images: `png`, `jpg`, `webp`, `apng` (**not** `jpeg`, `gif`, or `PNG`);
  - video: `webm`, `mp4`, `avi`;
  - music: `mp3`, `ogg`, `wav`.
- Matches are appended (`Assets::add_file`). Files with the same type are appended in directory iteration order,
  which is unsorted readdir order.
- **Here:** `~/Games/{snes,psp,ps1}/media/<rom basename>/boxFront.png`. These are written by
  `scripts/fetch-boxart.py`, which uses Python's `Path.stem`, the same as `completeBaseName`.

The Skraper layout is also compiled in (`providers/skraper/SkraperAssetsProvider.cpp`): `<dir>/{skraper,media,.media}/<box2dfront|supporttexture|box3d|...>/<basename>.<any ext>`.
**Unused here** (the log shows "0 assets found").

### 3.4 Precedence and the grid theme's box art

- `Assets::add_uri` (`model/gaming/Assets.cpp:55-63`) appends and skips exact duplicates. `assets.X` (the QML
  property) is the **first** entry (`Assets.cpp:39-47`). Metadata `assets.*` lines are added first, so an
  `assets.boxFront:` line **overrides** a `media/<name>/boxFront.png` file. Media-dir files come next, and Skraper
  files last.
- The grid theme's grid tile (`layer_grid/GameGridItem.qml:48-59` @ `d7533c8`) uses the first non-empty of:
  `game.boxFront`, `game.poster`, `game.banner`, `game.steam`, `game.tile`, `game.cartridge`, then the same six
  of the **collection**, then nothing (the title text is shown). The background uses `game.background ||
  game.screenshots[0]` (`layer_grid/BackgroundImage.qml:30-31`). The details panel shows `logo`, the first
  `screenshots` entry and `videos`. Collection logos are built-in SVGs keyed by `shortName`
  (`layer_platform/PlatformCard.qml:72`), which is why `utils.svg` is reported as missing.
- **Here** only `boxFront` is used, on games only.

---

## 4. Launching (`ProcessLauncher.cpp`)

### 4.1 Pipeline (`onLaunchRequested`, `:159-224`)

1. `raw = game.launchCmd()` (game value, or the inherited collection value). There is no per-file command.
2. **Tokenise** `raw` with Pegasus' own tokenizer (`utils/CommandTokenizer.cpp:50-83`; no shell is involved):
   - Skip whitespace (`QChar::isSpace`).
   - If the token starts with `'` or `"`, it runs to the **next identical quote**, inclusive. With no closing
     quote it runs to the end of the string. If it is fully quoted and longer than one character, the quotes are
     removed. `''` gives an empty argument.
   - Otherwise the token runs to the next whitespace. **Quotes inside such a token are literal**:
     `--opt="a b"` gives two tokens, `--opt="a` and `b"`.
   - A quoted token ends right after its closing quote, so `"a b"c` gives `a b` and `c`.
   - Every token is then **trimmed**, so a quoted `" x "` gives `x`.
   - There are no backslash escapes, no variable expansion, no globbing, no `;`/`|`/`&&`, and no `~`.
3. **Substitute variables in each token** (`replace_variables`, `:56-79`), *after* splitting, so a path that
   contains spaces stays one argument. These are literal, case-sensitive string replacements, done in this order:
   - `{file.path}` gives `pretty_path(fi)` = `cleanPath(absoluteFilePath)` (no symlink resolution);
   - `{file.uri}` gives `QUrl::fromLocalFile(abs).toString(FullyEncoded)`, for example `file:///home/x/a%20b.iso`
     (**unused here**);
   - `{file.name}` gives the file name with its extension (**unused here**);
   - `{file.basename}` gives `completeBaseName` (**unused here**);
   - `{file.dir}` gives `cleanPath(absolutePath)`, the containing dir (**unused here**);
   - then `{env.NAME}` (regex `{env.([^}]+)}`, where the dot matches any character) gives the value of `$NAME`,
     or an empty string if it is unset. This is done repeatedly from left to right (`:40-54`). **Unused here.**
   - (`{file.documenturi}` exists only on Android.)
4. The command is the first token. If there are no tokens, or the first token is empty, Pegasus logs "no launch
   command defined" and the launch fails. If the command contains `/` or `\`, it is made absolute against the
   game's launch basedir (`abs_launchcmd`, `:134-142`). Otherwise it is looked up in `PATH` by QProcess.
5. **Working dir** (`:213-219`):
   - default: if the (resolved) command contains a slash, the command's **own directory**; otherwise the **game
     file's directory**;
   - if the game/collection `workdir` is non-empty, it gets the same variable substitution and is then made
     absolute against the launch basedir (`abs_workdir`, `:144-150`).
6. The `game-start` scripts run (with the game file's absolute path as the argument), then
   `QProcess::start(command, args)` (`:226-262`). The **environment is inherited unchanged** from Pegasus. On the
   device that includes `MANGOHUD=1`, `LD_PRELOAD=...libMangoHud_shim.so`, `QT_BEARER_POLL_TIMEOUT=-1` and a
   `PATH` with `~/.config/pegasus-frontend/bin` first, all set by `dotfiles/pegasus-frontend/run`. stdout, stderr
   and stdin are **forwarded** to Pegasus' own. The C launcher's wrapper should set the same environment.

Results here:

- `launch: "{file.path}"` gives the command `/home/robin/Games/pc/diablo.sh` with no arguments and the workdir
  `/home/robin/Games/pc`. For utils the workdir is `/home/robin/Games/utils/tools`. For installed games it is
  `/home/robin/Games/installed/<game>`.
- `retroarch -f -L /usr/lib/libretro/snes9x_libretro.so "{file.path}"` gives `retroarch` from PATH with the
  arguments `[-f, -L, /usr/lib/libretro/snes9x_libretro.so, /home/robin/Games/snes/Chrono Trigger (U) [!].smc]`
  and the workdir `/home/robin/Games/snes`.

### 4.2 During and after the game

- Pegasus **stays alive as the parent**. When the process starts (`QProcess::started`), `processLaunchOk` makes
  it record the launch time (5.2), **tear down the whole QML frontend** and stop SDL gamepad handling
  (`Backend.cpp:260-264`). After teardown it **blocks** in `waitForFinished(-1)` (`:264-271`).
- After the game exits, whatever the exit code and even on a crash (`:301-319`), it runs the `game-end` scripts,
  emits `processFinished`, writes the play-time row (5.2), **rebuilds the frontend** (the theme reloads and
  restores its selection from theme memory) and restarts the gamepad (`Backend.cpp:266-270`). **It does not
  rescan games or metadata.**
- If the process fails to start (`FailedToStart`), an error event is shown and **no stats are written**
  (`:281-299`).
- Launches and finishes are ignored for stats while a scan is running (`ProviderManager.cpp:148-164`).

### 4.3 Games with several files (**unused here**)

`Game::launch()` (`Game.cpp:110-118`): with one file, that file is launched. With more than one, it emits
`launchFileSelectorRequested`, which becomes `api.eventSelectGameFile(game)` (`model/Api.cpp:76-80`), and the theme
must offer a picker and call `gamefile.launch()`. **The built-in grid theme has no handler for it**, so in Pegasus
with this theme a multi-file game cannot be launched (local patch 0004 only hides the overlay). Variables always
refer to the chosen file.

---

## 5. Play time (`providers/pegasus_playtime/PlaytimeStats.cpp`)

### 5.1 Schema (exact text, as `sqlite_master` holds it on the device)

```sql
CREATE TABLE paths(id INTEGER PRIMARY KEY,path TEXT UNIQUE NOT NULL)
CREATE TABLE plays(id INTEGER PRIMARY KEY,path_id INTEGER NOT NULL REFERENCES plays(id),start_time INTEGER NOT NULL,duration INTEGER NOT NULL)
```

(`PlaytimeStats.cpp:54-86`.) Notes:

- The foreign key points at `plays(id)`, which is an upstream bug. It is harmless because foreign keys are not
  enabled.
- `sqlite_autoindex_paths_1` comes from UNIQUE.
- There is **no version or migration table**, and `PRAGMA user_version` is 0. The journal mode is `delete` (the
  default). Pegasus sets no pragmas. The Qt QSQLITE driver applies its default busy timeout (5000 ms in Qt 5, overridable only via `QSQLITE_BUSY_TIMEOUT`, which Pegasus does not set).
- The tables are created (only if missing, checked with `QSqlDatabase::tables()`) inside the write transaction, the
  first time a game finishes. If `stats.db` does not exist, Pegasus skips loading (`:163-164`) and SQLite creates
  the file on the first write.

### 5.2 When and what is written

- **Start:** `onGameLaunched` (`:222-226`) stores `QDateTime::currentDateTimeUtc()` when QProcess reports
  *started*.
- **End:** `onGameFinished` (`:228-246`) runs after the process has exited and the UI teardown has completed.
  `duration = start.secsTo(now)`, whole seconds rounded toward zero. **There is no minimum.** Zero-length rows are
  written (the device has 12 of them).
- The row is written asynchronously (`start_processing`, `:248-292`). Each batch opens the DB, begins a
  transaction, creates the tables if missing, writes, and commits:

```sql
-- get_path_id (:88-123), path = clean_abs_path(gamefile.fileinfo())
SELECT id FROM paths WHERE path = ?;
-- if no row:
INSERT INTO paths VALUES(null, ?);
SELECT last_insert_rowid() FROM paths;
-- save_play_entry (:125-138)
INSERT INTO plays VALUES(null, ?, ?, ?);   -- path_id, start_time, duration
```

- `start_time` = `start.toSecsSinceEpoch()`: **seconds since the Unix epoch (UTC)**, the launch moment.
- `duration` = seconds (an integer, ≥ 0).
- `path` = `QDir::cleanPath(QFileInfo(gamefile path).absoluteFilePath())` of the **launched file**. It is
  absolute and lexically cleaned, **not symlink-resolved**. The device stores
  `/home/robin/Games/pc/diablo.sh` even though `~/Games/pc` is a symlink to `~/gpd/pegasus/pc`. **The C launcher
  must not call `realpath()`**, or the history splits. The path is built from the game_dirs.txt path plus the
  metafile-relative `file:` value, cleaned.
- Games with no file (URI-only games, **unused here**) would store garbage: the URI is treated as a relative path
  and made absolute against the CWD. Stats are only *read* back by file path (`:198`, "TODO: URI support").
- If Pegasus is killed during a game, no row is written.

For the C version: the same statements with the same bindings give identical rows. Recommended:
`BEGIN; SELECT id FROM paths WHERE path=?; [INSERT INTO paths VALUES(NULL,?)]; INSERT INTO plays VALUES(NULL,?,?,?); COMMIT;`
with `start_time = time(NULL)` taken right after a successful `fork/exec` and `duration = time(NULL) - start_time`
after `waitpid`. Pegasus truncates milliseconds. Second-resolution `time()` differences can be off by ±1 s, which
is acceptable. Use `CLOCK_REALTIME` milliseconds and `/1000` if you want to be exact.

### 5.3 Reading and aggregation (`:161-220`, `GameFile.cpp:57-63`, `Game.cpp:81-108`)

```sql
SELECT paths.path, plays.start_time, plays.duration FROM plays INNER JOIN paths ON plays.path_id=paths.id;
```

For each row whose path **exactly equals** the path of a currently known game file (otherwise the row is skipped
but **kept** in the DB):

- `play_count += 1`;
- `play_time += max(0, duration)`;
- `last_played` = `fromSecsSinceEpoch(start_time + duration)`, which is the **end** time. The value is
  *overwritten* by each row in result order (no `ORDER BY`), so in practice it is the last row's end. Because rows
  are inserted in chronological order this equals `max(start_time + duration)`. The C version should use the max.

Per game, the totals are the sums over its files and `last_played` is the maximum over its files. After a game
exits in the same session, Pegasus adds 1 to the count, adds the duration, and takes the maximum with
`start + duration`, in memory only.

Stale paths stay in the DB: `utils/tools/desktop.sh` and `snes/Breath of Fire.smc` are there now.

---

## 6. Favourites (`providers/pegasus_favorites/Favorites.cpp`)

- Path: `writableConfigDir()/favorites.txt` (`:35-38`). **It does not exist on the device**, so favourites have
  never been used here.
- **Read** at every scan (`:54-86`): UTF-8, one entry per line. A line starting with `#` is skipped. Lines are
  not trimmed. Each line is first looked up as a URI (exact match). Otherwise it becomes
  `clean_abs_path(QFileInfo(config_dir, line))`, so an absolute line is used as written and a relative line is
  relative to the config dir. That path must be **exactly equal** to a game file's path. If it matches, the
  **whole game** is marked favourite. Lines that match nothing are ignored.
- **Write** (`:88-151`): every time a favourite is toggled (and not during a scan), Pegasus rewrites the **whole
  file** from the current in-memory game list:
  ```
  # List of favorites, one path per line
  /abs/clean/path/of/file1
  /abs/clean/path/of/file2
  ```
  It writes one line for **every file** of every favourite game: `clean_abs_path` if the file exists, or the raw
  stored path (or URI) if it does not. Lines end with `\n` (`Qt::endl`). Relative paths are written only in
  portable mode. The write runs on a worker thread without a temporary file, which is a plain truncate-and-write.
- **Consequence:** an entry for a game that Pegasus cannot see when it rewrites the file **is lost**. The C
  launcher should write exactly this format with the same path strings, and should not add any other content
  (other comments are dropped as well).

---

## 7. Things a replacement must preserve so Pegasus keeps working

1. **stats.db schema:** do not add columns to `paths` or `plays`. Pegasus inserts with positional
   `VALUES(null, ?, ?, ?)` / `VALUES(null, ?)`, so an extra column makes every insert fail and the play time is
   silently lost. Do not rename tables. Extra tables and indexes are fine. Keep `paths.path` UNIQUE. Do not keep a
   write transaction open while Pegasus might write (it waits at most about 5 s). Keep journal mode `delete`;
   WAL is persistent and harmless, but avoid it.
2. **Path strings** in `paths.path` and `favorites.txt` must be byte-identical to what Pegasus computes (2.11).
   Use lexical cleaning only, with no `realpath()`, no `~`, no trailing slash, and no `//`.
3. **favorites.txt:** keep the format in section 6. Pegasus rewrites the whole file and keeps only what it can
   match.
4. **settings.txt:** do not write it, or if you must, use valid `key: value` lines that Pegasus knows. Pegasus
   rewrites it completely the next time any setting changes.
5. **theme_settings/pegasus-theme-grid.json** must be valid JSON: an object with string values `collection` and
   `game`. Invalid JSON is ignored, not fatal.
6. **Do not put files in scanned game dirs that match a collection's extensions**, at any depth except the
   top-level `media/`. Pegasus will list them as games. snes accepts `zip` and `7z`, ps1 accepts `m3u`, and psp
   accepts `iso`. A cache such as `snes/.cache/foo.zip` is safe only because hidden dirs are skipped.
7. **Do not create files named `metadata.txt`, `metadata.pegasus.txt`, `*.metadata.txt` or
   `*.metadata.pegasus.txt`** in the top level of any game dir or in `~/.config/pegasus-frontend/metafiles/`
   unless they are valid metadata files. Pegasus will parse them.
8. **Metadata files**, if the C launcher ever writes them (for example the fetch-boxart generated block):
   - keep `key: value` on one line, comments in column 0, and continuation lines indented;
   - **blank lines end an entry**: a paragraph break inside a description must be an indented `.`;
   - collection keys must come **before** the first `game:` of that section, because after `game:` every key
     applies to the game;
   - remember that the generated sections in `ps1/psp/snes` are overwritten by `fetch-boxart.py` from the marker
     line `# --- generated by fetch-boxart.py, edits below are overwritten ---`, and
     `installed/metadata.pegasus.txt` by `scripts/gamelib/core.py`.
9. **media/ layout:** box art must be named `boxFront.png` (or `.jpg`/`.webp`/`.apng`, with a lowercase extension
   and the exact case of an alias from 3.1) in `media/<file basename>/`. A `.jpeg` or `BoxFront.PNG` is ignored by
   Pegasus.
10. **Built-in third-party providers are enabled** in this build (Steam, GOG, ES2, Logiqx, Lutris, Skraper). Steam
    and GOG find nothing now. If Steam is ever installed on the device, Pegasus would show a `Steam` collection
    that the C launcher does not know about. Optional: disable it with `providers.steam.enabled: false`.
11. `lastrun.log` belongs to Pegasus. game_dirs.txt is a symlink into the repo, so do not rewrite it.

---

## 8. What this setup actually uses

Six game dirs (`dotfiles/pegasus-frontend/game_dirs.txt`, symlinked into `~/.config/pegasus-frontend/`):
`~/Games/{pc,snes,psp,ps1,utils,installed}`. `~/Games/pc` and `~/Games/utils` are **symlinks** to
`~/gpd/pegasus/{pc,utils}` (the repo's `pegasus/` dir), and all stored paths use the `~/Games/...` form. There
are no global metafiles, no `.media`, no scripts and no favourites. Pegasus found **43 games in 5 collections**,
which matches the device log.

| Metadata file (sorted parse order) | Collection | shortname | Collection keys | Games | Game keys used | Box art |
|---|---|---|---|---|---|---|
| `installed/metadata.pegasus.txt` (generated by `scripts/gamelib/core.py`) | PC Games | pc | `launch: "{file.path}"` | 22 | `file: <slug>/gpd-launch.sh`, `assets.boxFront: <slug>/gpd-cover.jpg`, `description` | `assets.boxFront` → `.jpg` next to the launcher |
| `pc/metadata.pegasus.txt` | PC Games (merged) | pc | `summary`, `launch: "{file.path}"` | 3 | `file`, `assets.boxFront: media/<x>.png`, `developer`, `publisher`, `release` (YYYY-MM-DD), `genre`, `players` (`1-4`, `1`), `description` | `assets.boxFront` (flat `media/` files) |
| `ps1/metadata.pegasus.txt` | PlayStation | psx | `extensions: cue, chd, pbp, m3u`, `launch: retroarch -f -L <core> "{file.path}"` | 1 | `file` (in a generated block) | `media/<rom stem>/boxFront.png` |
| `psp/metadata.pegasus.txt` | PSP | psp | `extensions: iso, cso, chd, pbp`, `launch: PPSSPPSDL --fullscreen "{file.path}"` | 1 | `file` | `media/<rom stem>/boxFront.png` |
| `snes/metadata.pegasus.txt` | Super Nintendo | snes | `extensions: sfc, smc, fig, swc, zip, 7z`, `launch: retroarch -f -L /usr/lib/libretro/snes9x_libretro.so "{file.path}"` | 6 | `file` | `media/<rom stem>/boxFront.png` |
| `utils/metadata.pegasus.txt` | Utilities | utils | `summary`, `launch: "{file.path}"` | 10 | `file: tools/<x>.sh` (subdir), `assets.boxFront: media/<x>.png`, `description` | `assets.boxFront` |

Totals: PC Games 25, Utilities 10, Super Nintendo 6, PSP 1, PlayStation 1.

- Every ROM in snes/psp/ps1 also has a `game:` entry, so extension scanning adds no extra games today. It still
  matters for new ROMs dropped in before `fetch-boxart.py` runs; those get the filename as their title. Scanning
  recurses into subdirs, but no subdirs other than `media/` exist.
- Comments: column-0 `#` lines (the generated-block markers, and the header of `installed`).
- `verify_files` is on (the default), so missing `file:` or asset paths are dropped.
- Theme: built-in `pegasus-theme-grid` (`:/themes/pegasus-theme-grid/`). Its memory file is
  `theme_settings/pegasus-theme-grid.json`.
- stats.db: 40 paths and 235 plays, from 2026-10-06 to 2026-10-10. It includes zero-duration rows and 2 stale
  paths.

**Unused here (the C version may skip these):** multi-line values, `.` paragraph breaks, `\n` escapes, collection
`description`, `directories`, `files`, `regex`, all `ignore-*`, `workdir`/`cwd`, `sort-by`/sort titles (games and
collections), `x-*` extras, `tags`, `rating`, game `summary`, game-level `launch`, URI files, multi-file games,
http asset URLs, collection-level assets, asset types other than boxFront, the `.media/` dir, the Skraper layout,
`{file.uri|name|basename|dir}`, `{env.*}`, global `metafiles/`, scripts, portable mode, favourites (the file is
absent, though the format must still be supported if the C launcher offers favourites), `settings.txt` beyond
`general.fullscreen`.

**Minimum feature set for the C version to see the same 43 games with the same art, launch them the same way and
keep stats compatible:**

1. Read game_dirs.txt.
2. Find the top-level `metadata.pegasus.txt` files and sort their paths.
3. Parse the line syntax (2.1) with the keys `collection`, `shortname`, `summary`, `extensions`, `launch`, `game`,
   `file`, `assets.boxfront`, `developer`, `publisher`, `genre`, `release`, `players` and `description`.
4. Merge collections by name.
5. Scan by extension (top-level files plus recursive non-hidden subdirs except `media`), and merge with `game:`
   files by clean absolute path.
6. Map `media/<stem>/boxFront.{png,jpg,webp,apng}`, with `assets.*` taking precedence.
7. Sort with ICU collation.
8. Tokenise and substitute `{file.path}`, resolve a command containing `/` against the metafile dir, and set the
   workdir to the command's dir or the file's dir.
9. fork/exec, wait, and insert into stats.db exactly as in 5.2.
10. Compute play count, total time, and last played = max(start + duration).
