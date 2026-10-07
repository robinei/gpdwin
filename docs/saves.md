# Game saves: GPD ⇄ desktop

The desktop runs Steam (Proton for these games) and syncs Steam Cloud; the GPD runs builds
installed with `scripts/games`, without Steam. Saves are copied/synced between the two; the
desktop is the bridge to Steam Cloud (`desktop/steam-cloud-status` shows what needs a Steam
launch to upload). Planned: Syncthing, one folder per game (paths may differ per device).

| Game (AppID) | GPD (build) | Desktop (build) | Notes |
|---|---|---|---|
| Super Meat Boy (40800) | `~/.local/share/SuperMeatBoy/UserData/` (Linux) | `steamapps/common/Super Meat Boy/UserData/` (Windows/Proton; cloud root 1) | Progress: `savegame.dat` (same format). Settings: `reg0.dat` (Linux) vs `Reg0.dat` (Windows): different contents, not copied. |
| Bastion (107100) | `~/.local/share/Bastion/` (Linux, FNA) | `userdata/<id>/107100/remote/` (Windows/Proton; cloud root 0) | Files are `<profile>.{sav,keyctrls,mousectrls}`; Linux uses the profile name from `activeProfile` (`Profile1.sav`), Steam Cloud stores lowercase (`profile1.sav`). Same .NET code, same format. |

## History
- 2026-10-07: desktop (cloud-synced) saves copied to the GPD for both games; previous GPD files
  in `~/save-backups/2026-10-07/` on the GPD (SMB had only the default savegame, Bastion none).
