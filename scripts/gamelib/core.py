"""Shared game-installer logic: installed-game registry, executable detection, launchers,
Pegasus metadata. Source-specific code (downloading, library lists) lives in gamelib/sources/."""
import json, os, re, shutil, stat
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
INSTALL_ROOT = Path.home() / "Games" / "installed"
GAME_DIRS = Path.home() / ".config" / "pegasus-frontend" / "game_dirs.txt"
CONFIG = Path.home() / ".config" / "gpd" / "games.json"
REGISTRY_NAME = ".gpd-game.json"
LAUNCHER_NAME = "gpd-launch.sh"
COVER_NAME = "gpd-cover.jpg"


# ---------- config (non-secret: usernames, SteamID) ----------
def load_config():
    try:
        return json.loads(CONFIG.read_text())
    except (FileNotFoundError, ValueError):
        return {}


def save_config(cfg):
    CONFIG.parent.mkdir(parents=True, exist_ok=True)
    CONFIG.write_text(json.dumps(cfg, indent=1) + "\n")


# ---------- installed games ----------
def slug(name):
    s = re.sub(r"[^A-Za-z0-9]+", "-", name).strip("-").lower()
    return s or "game"


def installed():
    """{(source, id): record} for every game dir under INSTALL_ROOT."""
    out = {}
    if INSTALL_ROOT.is_dir():
        for reg in INSTALL_ROOT.glob(f"*/{REGISTRY_NAME}"):
            try:
                rec = json.loads(reg.read_text())
            except ValueError:
                continue
            rec["dir"] = str(reg.parent)
            out[(rec["source"], rec["id"])] = rec
    return out


def game_dir(game):
    return INSTALL_ROOT / slug(game["name"])


def save_record(rec):
    d = Path(rec["dir"])
    data = {k: v for k, v in rec.items() if k != "dir"}
    (d / REGISTRY_NAME).write_text(json.dumps(data, indent=1) + "\n")


def dir_size(path):
    total = 0
    for root, _, files in os.walk(path):
        for f in files:
            try:
                total += os.lstat(os.path.join(root, f)).st_size
            except OSError:
                pass
    return total


def free_space(path=Path.home()):
    return shutil.disk_usage(path).free


def human(n):
    for unit in ("B", "K", "M", "G", "T"):
        if n < 1024 or unit == "T":
            return f"{n:.0f}{unit}" if unit in ("B", "K") else f"{n:.1f}{unit}"
        n /= 1024


def uninstall(rec):
    shutil.rmtree(rec["dir"])
    write_pegasus()


# ---------- executable detection ----------
WIN_SKIP = re.compile(r"unins|setup|redist|vcredist|vc_redist|dxsetup|directx|dotnet|ndp\d|crash|report|"
                      r"ue4prereq|prereq|physx|oalinst|easyanticheat|eac_|battleye|helper|updater|"
                      r"touchup|cleanup|install", re.I)
LIN_SKIP = re.compile(r"crash|report|helper|updater|uninstall", re.I)


def _similar(a, b):
    a, b = slug(a).replace("-", ""), slug(b).replace("-", "")
    if not a or not b:
        return 0
    return 3 if a == b else (2 if a in b or b in a else 0)


def find_executables(dirpath, osname, game_name):
    """Candidate entry points, best first: list of (relative path, score)."""
    root = Path(dirpath)
    cands = []
    for p in root.rglob("*"):
        if not p.is_file() or p.name.startswith(".") or p.name in (LAUNCHER_NAME,):
            continue
        rel = p.relative_to(root)
        depth = len(rel.parts) - 1
        head = b""
        if osname == "windows":
            if p.suffix.lower() != ".exe" or WIN_SKIP.search(p.name):
                continue
        else:
            if LIN_SKIP.search(p.name) or ".so" in p.name:
                continue
            try:
                with p.open("rb") as f:
                    head = f.read(4)
            except OSError:
                head = b""
            is_elf = head == b"\x7fELF"
            is_shebang = head[:2] == b"#!"
            is_script = is_shebang or p.suffix in (".sh", ".x86_64", ".x86", ".bin")
            if not (is_script or is_elf):   # exec bits may be missing after download
                continue
        size = p.stat().st_size
        score = _similar(p.stem, game_name) * 10 - depth * 3 + min(size / 2**20, 50) / 10
        if osname == "linux" and (p.suffix == ".sh" or (head[:2] == b"#!" if osname == "linux" else False)):
            score += 4                      # the game's own launch script sets up cwd/env
        if "launcher" in p.name.lower():
            score -= 2
        cands.append((str(rel), round(score, 1)))
    return sorted(cands, key=lambda c: -c[1])


# ---------- fixes for old native games ----------
def disable_old_bundled_sdl(dirpath):
    """Old Linux ports (FNA/MonoKickstart, e.g. Bastion) bundle an SDL2 without Wayland support;
    with no X server it finds no display. Move such copies aside so the system SDL (sdl2-compat on
    SDL3, Wayland-capable) is used. Returns the renamed paths."""
    moved = []
    for lib in Path(dirpath).rglob("libSDL2-2.0.so.0"):
        if lib.is_symlink() or not lib.is_file():
            continue
        if b"wayland" not in lib.read_bytes():
            target = lib.with_name(lib.name + ".bundled")
            lib.rename(target)
            moved.append(str(target))
    return moved


# ---------- launcher + Pegasus ----------
LAUNCHER = """#!/bin/sh
# Generated by ~/gpd/scripts/games for {name} ({source} {id}). Edit freely: it is not
# overwritten when Pegasus metadata is regenerated. Runner: {runner}.
cd "$(dirname "$0")/{workdir}" || exit 1
{env}exec {cmd} "$@"
"""


def write_launcher(rec, force=False):
    d = Path(rec["dir"])
    path = d / LAUNCHER_NAME
    if path.exists() and not force:
        return path
    exe = Path(rec["exe"])
    workdir = str(exe.parent) if str(exe.parent) != "." else "."
    if rec["os"] == "windows":
        runner, env = "wine (shared prefix ~/.wine)", "export WINEDEBUG=-all\n"
        cmd = f'wine "./{exe.name}"'
    else:
        runner, env = "native", ""
        target = d / exe
        target.chmod(target.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
        with target.open("rb") as f:
            is_script = f.read(2) == b"#!"
        if is_script:
            # the script may exec other binaries whose exec bits DepotDownloader dropped
            for b in d.rglob("*"):
                if b.is_file() and not b.is_symlink():
                    with b.open("rb") as f:
                        if f.read(4) == b"\x7fELF" and ".so" not in b.name:
                            b.chmod(b.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
        cmd = f'"./{exe.name}"'
    path.write_text(LAUNCHER.format(name=rec["name"], source=rec["source"], id=rec["id"],
                                    runner=runner, workdir=workdir, env=env, cmd=cmd))
    path.chmod(0o755)
    return path


def ensure_game_dir_listed():
    lines = GAME_DIRS.read_text().splitlines() if GAME_DIRS.exists() else []
    if str(INSTALL_ROOT) not in lines:
        lines.append(str(INSTALL_ROOT))
        GAME_DIRS.write_text("\n".join(lines) + "\n")   # game_dirs.txt is a repo symlink
        return True
    return False


def write_pegasus():
    """Regenerate ~/Games/installed/metadata.pegasus.txt. Same collection name and shortname as
    ~/Games/pc, so Pegasus merges these games into PC Games."""
    INSTALL_ROOT.mkdir(parents=True, exist_ok=True)
    lines = ["# Generated by ~/gpd/scripts/games; edits are overwritten.",
             "collection: PC Games", "shortname: pc", 'launch: "{file.path}"', ""]
    for rec in sorted(installed().values(), key=lambda r: r["name"].lower()):
        d = Path(rec["dir"])
        if not (d / LAUNCHER_NAME).exists():
            continue
        rel = d.name
        lines += [f"game: {rec['name']}", f"file: {rel}/{LAUNCHER_NAME}"]
        if (d / COVER_NAME).exists():
            lines.append(f"assets.boxFront: {rel}/{COVER_NAME}")
        runner = "Wine" if rec["os"] == "windows" else "native Linux"
        lines += [f"description: Installed from {rec['source'].title()} ({runner}).", ""]
    (INSTALL_ROOT / "metadata.pegasus.txt").write_text("\n".join(lines))
    ensure_game_dir_listed()
