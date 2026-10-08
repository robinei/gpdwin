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
  per game in `games/saves.json`: status (in sync / GPD newer / desktop newer / only on one side /
  differ), newest save time on each side, Steam Cloud state; below it the per-file detail.
  Keys: `p` push GPD→desktop, `g` pull desktop→GPD, Enter copy newer→older, `r` refresh, `q` quit.
  Each copy asks for confirmation and warns when the target side is newer or Steam is running.
  `scripts/save-status --list` prints the same table.
- Copies overwrite every save file the source side has for that game; files only on the target
  side are left alone. Overwritten files are backed up first: GPD `~/save-backups/<time>/<appid>/`,
  desktop `~/save-backups/<time>/<appid>/`. Modification times are preserved by the copy.
- After a push, start the game once in Steam on the desktop so it uploads to Steam Cloud
  (Steam Cloud column: "needs Steam launch" until it has).
- Desktop side: `desktop/save-status-remote`, run over ssh by the GPD's restricted key
  (`~/.ssh/authorized_keys`: `restrict,command="/home/robin/Code/gpdwin/desktop/save-status-remote"`).
  The command line picks the action (`SSH_ORIGINAL_COMMAND`): `status`, `get APPID` (tar of the
  game's files named by index), `put APPID` (same format in). It only touches the paths in
  `games/saves.json` (tar member names are indexes, anything else is rejected), so the key can
  read and write only those files. Remove the authorized_keys line to revoke. PTY requests fail and
  port forwarding is prohibited (`restrict`).
  Desktop firewall (ufw): `sudo ufw allow from 192.168.1.218 to any port 22 proto tcp comment
  'GPD save status'` (tied to the GPD's IP; reserve both IPs in the router).
- The desktop clone (`~/Code/gpdwin`) must be pulled for changes to `desktop/` or
  `games/saves.json` to take effect there; with an old script, copying fails with a message saying so.
- New game: add it to `games/saves.json` (both paths) and `docs/saves.md`.
- `desktop_host` in `games/saves.json` is the desktop (`robin@desktop.lan`).

## History
- 2026-10-07: desktop (cloud-synced) saves copied to the GPD for both games; previous GPD files
  in `~/save-backups/2026-10-07/` on the GPD (SMB had only the default savegame, Bastion none).
