# Desktop-side tools

Run on the Linux desktop (which has Steam), not on the GPD. Clone: `~/Code/gpdwin`.

- `steam-cloud-status`: which Steam Cloud games have local saves that Steam hasn't uploaded yet.
  Read-only: compares Steam's per-game cloud cache (`userdata/<id>/<appid>/remotecache.vdf`,
  size + SHA-1 per file at the last sync) with the files on disk. Saves synced over from the GPD
  (planned: Syncthing, one folder per game) show as "changed" until the game is started once via
  Steam (Steam only syncs on game launch/exit). `-v` lists everything; pass AppIDs to filter.
  Root mapping (ERemoteStorageFileRoot): 0 userdata remote, 1 install dir, 2 Documents,
  3 AppData/Local, 4 AppData/Roaming, 9 Saved Games, 12 AppData/LocalLow (inside the Proton
  prefix `compatdata/<appid>/pfx/drive_c/users/steamuser`; verified on 8 games). Native Linux
  builds with Windows roots need Steam's binary appinfo overrides: reported as unresolved.
