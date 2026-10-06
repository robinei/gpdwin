# ~/gpd — GPD Win 1 setup and maintenance

Everything that makes this handheld what it is: configs, system files, scripts, and notes.

**Maintenance**
- Pegasus → Utilities → **Maintenance**: a Claude Code session runs the routine
  (`.claude/commands/maintain.md`): Arch news, updates, AUR review, pacnew, drift, changelog.
- Pegasus → Utilities → **Update System**: the same without Claude (`scripts/update`):
  you read the AUR diffs yourself.

**How files are managed** (`manifest.tsv`, `scripts/sync`)
- Your configs (sway, foot, wob, fish functions, Pegasus collections...) are symlinks into
  `dotfiles/` and `pegasus/`. Edit them where they are; `git diff` shows the change.
- System files (`/etc`, `/boot/loader`, ...) and files programs rewrite (RetroArch) are copies:
  `scripts/sync check` shows drift, `scripts/sync capture` copies live → repo,
  `scripts/sync install` copies repo → live.

**AUR** (`scripts/aur`): `check`, `review PKG` (diff since last reviewed), `build PKG`.

**Rebuilding the device**: install Arch as in `docs/boot.md` and `docs/packages.md`,
clone this repo to `~/gpd`, `sudo -v && scripts/sync install`.

Docs: `docs/` (start with `changelog.md`). Notes for Claude sessions: `CLAUDE.md`.
Backed up to `git@github.com:robinei/gpdwin.git` (private; pushed with a deploy key, `~/.ssh/id_ed25519`).
