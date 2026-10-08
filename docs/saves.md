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
- Verified 2026-10-08: Super Meat Boy (40800) cloud files are byte-identical to the GPD's and the
  desktop's `savegame.dat`; list takes ~3 s. Gotcha: import `requests` only after the gevent
  patching in `connect()` (otherwise the download hangs).

## History
- 2026-10-07: desktop (cloud-synced) saves copied to the GPD for both games; previous GPD files
  in `~/save-backups/2026-10-07/` on the GPD (SMB had only the default savegame, Bastion none).
