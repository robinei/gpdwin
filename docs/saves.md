# Game saves: GPD ⇄ desktop

The desktop runs Steam (Proton for these games) and syncs Steam Cloud; the GPD runs builds
installed with `scripts/games`, without Steam. Saves are copied/synced between the two; the
desktop is the bridge to Steam Cloud (`desktop/steam-cloud-status` shows what needs a Steam
launch to upload). Copying is manual, per game, in the Game Saves tool (below).

| Game (AppID) | GPD (build) | Desktop (build) | Notes |
|---|---|---|---|
| Super Meat Boy (40800) | `~/.local/share/SuperMeatBoy/UserData/` (Linux) | `steamapps/common/Super Meat Boy/UserData/` (Windows/Proton; cloud root 1) | Progress: `savegame.dat` (same format). Settings: `reg0.dat` (Linux) vs `Reg0.dat` (Windows): different contents, not copied. |
| Bastion (107100) | `~/.local/share/Bastion/` (Linux, FNA) | `userdata/<id>/107100/remote/` (Windows/Proton; cloud root 0) | Files are `<profile>.{sav,keyctrls,mousectrls}`; Linux uses the profile name from `activeProfile` (`Profile1.sav`), Steam Cloud stores lowercase (`profile1.sav`). Same .NET code, same format. |

## Game Saves (Pegasus: Utilities > Game Saves; `scripts/save-status`)
- Full-screen curses UI, manual per game, no automatic sync (Syncthing was rejected). One row
  per game in `games/saves.json`: status against the desktop and against Steam Cloud (in sync /
  GPD newer / other side newer / only on one side / differ), newest save time on GPD, desktop and
  cloud; below it the per-file detail for the selected game.
  Keys: `c` pull Steam Cloud→GPD (works with the desktop off), `g` pull desktop→GPD, `p` push
  GPD→desktop, Enter pull from whichever is newer (cloud, then desktop; or push if the GPD is
  newer), `r` refresh, `q` quit. Each copy asks for confirmation and warns when the target side is
  newer or Steam is running. The cloud is read-only: to get a save into it, push to the desktop and
  start the game once in Steam there.
- Cloud names per file: `"cloud"` in `games/saves.json` (a folder entry uses a name prefix, e.g.
  Stardew `%WinAppDataRoaming%StardewValley/Saves/`; Bastion's cloud names are lowercase). Files
  are matched by name only: the cloud's per-file platform flag (`All` for the games here) is ignored,
  so saves made by a Windows install (Proton on the desktop) can be pulled onto the Linux build; the
  games' formats are the same on both (Stardew: XML), but the game version should match.
  `scripts/save-status --list` prints the same table.
- Copies overwrite every save file the source side has for that game; files only on the target
  side are left alone. Overwritten files are backed up first: GPD `~/save-backups/<time>/<appid>/`,
  desktop `~/save-backups/<time>/<appid>/`. Modification times are preserved by the copy.
- After a push, start the game once in Steam on the desktop so it uploads to Steam Cloud
  (Steam Cloud column: "needs Steam launch" until it has).
- Desktop side: `desktop/save-status-remote`, run over ssh by the GPD's restricted key
  (`~/.ssh/authorized_keys`: `restrict,command="/home/robin/Code/gpdwin/desktop/save-status-remote"`).
  The command line picks the action (`SSH_ORIGINAL_COMMAND`): `status [APPID]`, `get APPID`
  (tar of the game's files), `put APPID` (same format in). A tar member is named `N` (file entry
  N of the game) or `N/rel/path` (file inside folder entry N, `"dir": true` in saves.json); other
  names are rejected. So the key can only read and write the paths in the desktop's own copy of
  `games/saves.json` (and below its folders). The GPD asks about each game separately at startup;
  a game the desktop's copy doesn't list shows "git pull in ~/Code/gpdwin there". Remove the authorized_keys line to revoke. PTY requests fail and
  port forwarding is prohibited (`restrict`).
  Desktop firewall (ufw): `sudo ufw allow from 192.168.1.218 to any port 22 proto tcp comment
  'GPD save status'` (tied to the GPD's IP; reserve both IPs in the router).
- The desktop clone (`~/Code/gpdwin`) must be pulled for changes to `desktop/` or
  `games/saves.json` to take effect there; with an old script, copying fails with a message saying so.
- New game: add it to `games/saves.json` (both paths) and `docs/saves.md`.
- `desktop_host` in `games/saves.json` is the desktop (`robin@desktop.lan`).

## Steam Cloud directly from the GPD (`scripts/steam-cloud`)
- Read-only access to a game's cloud files without the desktop: `steam-cloud list APPID`,
  `steam-cloud download APPID DIR` (files keep Steam's names, e.g. `%GameInstall%/UserData/savegame.dat`,
  mtime = cloud time, plus `manifest.json`). Uploading is not implemented: pushing back to the
  cloud goes through the desktop's Steam client (push with Game Saves, then start the game once).
- Why a custom tool: `steamctl` (PyPI) uses a library that predates Valve's 2023 login change
  ("Steam no longer accepts plaintext passwords at the CM"), so its logins fail with a bogus
  "invalid password". `steam-next` (fork of the same library) implements the new flow; `steamctl` on
  top of it also fails (renamed helpers, missing `Cloud.EnumerateUserApps`), so we use `steam-next` directly.
- Setup: `scripts/steam-cloud setup` (private venv in `~/.local/share/gpd/steam-cloud/venv`, steam-next
  pinned to 3.0.0 from PyPI; not a pacman/AUR package). `scripts/steam-cloud login` once (password + Steam
  Guard, asked in the terminal; use `! ` from Claude): it keeps a refresh token in
  `~/.local/share/gpd/steam-cloud/token.json` (mode 600, not in the repo, valid for months; `logout` deletes it,
  changing the Steam password revokes it). No password is stored.
- `download ... --only NAME` fetches just those files (exact name, or a prefix ending in `/`); a file whose
  download link fails (a 404 on an old Psychonauts Profile 1 file) is skipped and reported instead of
  aborting, exit code 3. The Game Saves tool passes only the files it tracks.
- Verified 2026-10-08: Super Meat Boy (40800) cloud files are byte-identical to the GPD's and the
  desktop's `savegame.dat`; list takes ~3 s. Gotcha: import `requests` only after the gevent
  patching in `connect()` (otherwise the download hangs).

## Finding a game's save locations (`scripts/saves-discover`, needs `ludusavi` on both machines)
- `ludusavi-bin` (AUR, vendored in `aur/pkgbuilds/`) is installed on the GPD and must be installed on the
  desktop too. `saves-discover [APPID...]` asks ludusavi what save files exist (GPD: directly; desktop:
  `save-status-remote discover APPID...` over ssh, so the desktop clone must be pulled), adds the game's
  Steam Cloud file names (`steam-cloud list-many`) and prints a *draft* `games/saves.json` entry per game
  plus notes on what it could not pair. It never writes; review the draft and add it by hand.
- Pairing: a found file belongs to the manifest save path whose literal tail (`StardewValley/Saves`)
  it sits under; same relative paths on both machines -> `"dir": true` entry, else files are paired by
  trailing path components. Cloud names match by trailing components (the `%Root%` token is only a
  prefix). Cloud names that differ in case from the local names (Bastion, Super Meat Boy) force
  file-level entries, since a folder entry would create lowercase duplicates on Linux.
- Speed: every ludusavi run re-reads its 17 MB manifest (~3 s on the GPD). `discover` makes at most
  two runs for any number of apps (one `manifest show --api` dump for apps not cached yet, one
  `backup --preview` for all of them): all 13 games take ~8 s cold, ~3.6 s cached. Cache:
  `~/.cache/gpd/ludusavi-slices.json`, dropped when ludusavi updates `~/.cache/ludusavi/manifest.yaml`.
  (An earlier version parsed the manifest YAML in Python: 65 s.)
- A game can have several copies on the desktop. `discover` also returns `steam_synced`, the files
  Steam's own records (`remotecache.vdf`, resolved by root) say it syncs to the cloud, and the draft
  prefers that copy over a path-similar one.
- Limits: ludusavi lists config files too (Stardew `startup_preferences`), those are skipped because they
  are not under a *save* path; it finds nothing for games it cannot see (Wine prefixes need a `roots`
  entry in `~/.config/ludusavi/config.yaml`; Strife's manifest path is the game folder; unsaved games).
- Mapped 2026-10-08 (GPD side verified on disk, desktop paths are Proton-prefix guesses marked
  `"unverified_desktop": true` until the desktop session checks them): SteamWorld Heist, FEZ,
  Kingdom: Classic, Death Road to Canada, Hyper Light Drifter, Risk of Rain, Heretic + Hexen.
- Desktop paths checked by the desktop session (2026-10-08): Stardew Valley runs the native Linux
  build there (`~/.config/StardewValley/Saves`, both farms match the cloud); the other six native
  games were changed to the same paths as on the GPD but could not be verified (not installed on the
  desktop); Heretic + Hexen stays a Proton guess. All still carry `unverified_desktop`.
- A folder entry can have `"ignore": [patterns]` (fnmatch on the path inside it): Stardew ignores
  Steam's `steam_autocloud.vdf` marker, Hyper Light Drifter ignores `gameprefs.dat` (settings, would
  otherwise be copied between devices). Narrow HLD to its save files once one exists.
- Psychonauts (2026-10-08): the cloud holds two profiles; **cloud profile 2** has the 2026 saves (slots 0-3
  from 2026-04-28), profile 1 is from 2010-2011. The game has **no profile selector and always uses
  `Profiles/Profile 1/`**, so the tool maps cloud `profile 2/...` onto the GPD's `Profile 1/` folder (the
  GPD's own yesterday's Profile 1 was backed up to `~/save-backups/20261008-163010/psychonauts-gpd-profile1`).
  A stray copy in `Profiles/Profile 2/` (pulled first) is unused. Cloud names are lowercase (`%GameInstall%profiles/profile 2/savedgame0`) while the GPD
  uses `SavedGame0`/`Profile 2- Raz`, hence file-level entries with explicit cloud names. Desktop paths
  are unverified guesses. Profile 2 comes from a Windows install (its `.ini` has CRLF line endings and the
  Windows pad numbering): the `Profile 2- Raz.ini` is NOT tracked, because on the GPD it carries the Linux
  pad bindings from docs/frontend.md (original kept as `Profile 2- Raz.ini.gpd-bak`) and a cloud pull
  would overwrite them. After pulling a Windows profile, re-apply those bindings. Loading in the game: not tested.
- Not mapped yet: Strife VE (GPD saves in
  `~/.local/share/strife-ve/savegames`; desktop location unclear), Cave Story+ (`Profile-*.dat` in the game folder).
- Cloud facts: SteamWorld Heist's cloud holds only controls.cfg/gamepads.cfg (not saves); FEZ, Kingdom,
  Risk of Rain, Hyper Light Drifter, Death Road to Canada have no cloud files for this account.

## Super Meat Boy: two copies on the desktop (checked by the desktop session, 2026-10-08)
- Steam's cloud record uses root 1 (the install folder) even for the Linux build: the cloud copy is
  `steamapps/common/Super Meat Boy/UserData/savegame.dat` (record time and hash match it, and Steam's
  `steam_autocloud.vdf` marker is there). That is the desktop path in `saves.json`, and it is right.
- The native Linux game plays from `~/.local/share/SuperMeatBoy/UserData/` (created on its first start,
  with the Linux-only `reg0.dat`), which Steam does not upload. All three files had the cloud's hash.
- Open: progress made on the desktop may never reach Steam Cloud unless the game also writes the
  install-folder copy, and a save pushed to the install folder may not be read by the desktop's Linux
  game once its native copy exists. Settle it by playing on the desktop and watching which file changes.

## History
- 2026-10-07: desktop (cloud-synced) saves copied to the GPD for both games; previous GPD files
  in `~/save-backups/2026-10-07/` on the GPD (SMB had only the default savegame, Bastion none).
