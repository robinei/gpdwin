# Handover to the on-device session (delete this file when done)

The installer session (remote, over ssh from Robin's PC) built this repo on 2026-10-06 and hands
maintenance to on-device sessions. Read `CLAUDE.md` first; it replaces the memory notes in
`~/.claude/projects/-home-robin/memory/` (`tweaks-log.md` asked to update `~/tweaks.md`; that
file is now archived, so update that memory to point at this repo's rules instead).

State at handover:
- Repo committed; `scripts/sync check` reported no drift.
- `aur/reviewed.tsv` has a baseline for the four AUR packages (see the notes column for how
  thoroughly each was looked at).
- Not yet exercised end to end: `scripts/maintain` from Pegasus (Claude + sudo keepalive),
  `scripts/update`, `scripts/aur build`. The first real maintenance run should watch for
  problems in these and fix them.

Suggested first tasks:
1. Run `/maintain` once (Robin starts it from Pegasus → Utilities → Maintenance).
2. Fix the memory notes mentioned above, then delete this file and commit.
3. Ask Robin about an off-device backup remote (docs/ideas.md #7).
