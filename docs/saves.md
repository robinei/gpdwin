# Game saves: GPD ⇄ desktop

The desktop runs Steam (Proton for these games) and syncs Steam Cloud; the GPD runs builds
installed with `scripts/games`, without Steam. Saves are copied/synced between the two; the
desktop is the bridge to Steam Cloud (`desktop/steam-cloud-status` shows what needs a Steam
launch to upload). Planned: Syncthing, one folder per game (paths may differ per device).

| Game (AppID) | GPD (build) | Desktop (build) | Notes |
|---|---|---|---|
| Super Meat Boy (40800) | `~/.local/share/SuperMeatBoy/UserData/` (Linux) | `steamapps/common/Super Meat Boy/UserData/` (Windows/Proton; cloud root 1) | Progress: `savegame.dat` (same format). Settings: `reg0.dat` (Linux) vs `Reg0.dat` (Windows): different contents, not copied. |
| Bastion (107100) | `~/.local/share/Bastion/` (Linux, FNA) | `userdata/<id>/107100/remote/` (Windows/Proton; cloud root 0) | Files are `<profile>.{sav,keyctrls,mousectrls}`; Linux uses the profile name from `activeProfile` (`Profile1.sav`), Steam Cloud stores lowercase (`profile1.sav`). Same .NET code, same format. |

## Save Status (Pegasus: Utilities > Save Status)
- `scripts/save-status` (GPD) hashes the GPD's files from `games/saves.json` and asks the desktop
  over ssh for its side: per game "same on both" / "GPD newer" / "desktop newer" / only on one
  side, plus Steam Cloud state ("NEEDS STEAM LAUNCH on desktop" when the desktop's files differ
  from Steam's last sync).
- Desktop: `~/.ssh/authorized_keys` has the GPD's key restricted with
  `restrict,command="/home/robin/Code/gpdwin/desktop/save-status-remote"`: it can only run that
  read-only script (JSON out). Remove the line to revoke. Verified: other commands return the
  status JSON, PTY requests fail, port forwarding is "administratively prohibited".
  Desktop firewall (ufw): `sudo ufw allow from 192.168.1.218 to any port 22 proto tcp comment
  'GPD save status'` (tied to the GPD's IP; reserve both IPs in the router). The desktop clone must be pulled for
  changes to `desktop/` or `games/saves.json` to take effect there.
- New game: add it to `games/saves.json` (both paths) and `docs/saves.md`.
- `desktop_host` in `games/saves.json` is the desktop's IP (192.168.1.216, DHCP: reserve it).

## History
- 2026-10-07: desktop (cloud-synced) saves copied to the GPD for both games; previous GPD files
  in `~/save-backups/2026-10-07/` on the GPD (SMB had only the default savegame, Bastion none).
