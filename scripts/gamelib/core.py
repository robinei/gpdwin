"""Shared game-installer logic: installed-game registry, executable detection, launchers,
Pegasus-format metadata. Source-specific code (downloading, library lists) lives in gamelib/sources/."""
import json, os, re, shutil, stat, subprocess
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
    write_metadata()


def link_saves(rec):
    """Re-attach the game's save folders to the central store ~/Saves (scripts/saves-link). Returns the
    printable lines: what was linked, or a note that no save location is known for the game."""
    root = Path(__file__).resolve().parents[2]
    try:
        games = json.loads((root / "games/saves.json").read_text())["games"]
    except (OSError, ValueError, KeyError):
        return []
    if not any(str(g["appid"]) == str(rec["id"]) and g.get("data") for g in games):
        return ["No save location known for this game yet: its saves stay wherever the game writes them "
                "(inside the Wine prefix or the install folder). See docs/saves.md, 'Central save store'."]
    r = subprocess.run([str(root / "scripts/saves-link"), "--apply", str(rec["id"])],
                       capture_output=True, text=True)
    return (r.stdout + r.stderr).strip().splitlines()


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
    if osname == "linux":
        # A shell script that starts one of the binaries is the game's own launcher (sets
        # LD_LIBRARY_PATH to bundled libs, e.g. Hyper Light Drifter's run.sh): prefer it.
        names = {Path(c).name for c, _ in cands}
        for i, (c, score) in enumerate(cands):
            try:
                text = (root / c).read_text(errors="replace") if (root / c).stat().st_size < 65536 else ""
            except OSError:
                continue
            if text.startswith("#!") and any(n != Path(c).name and n in text for n in names):
                cands[i] = (c, round(score + 40, 1))
    return sorted(cands, key=lambda c: -c[1])


# ---------- fixes for old native games ----------
def write_steam_appid(rec):
    """Steam games built with the Steam API call SteamAPI_RestartAppIfNecessary: with no Steam
    client they try to start Steam and quit (SteamWorld Heist). A steam_appid.txt next to the
    executable makes the API skip that; the game then runs without Steam. Returns the files written."""
    root = Path(rec["dir"])
    if rec.get("source") != "steam" or not any(
            p.name.lower() in ("libsteam_api.so", "steam_api.dll", "steam_api64.dll") for p in root.rglob("*")):
        return []
    written = []
    for d in {root, (root / rec["exe"]).parent} if rec.get("exe") else {root}:
        f = d / "steam_appid.txt"
        if not f.exists():
            f.write_text(f"{rec['id']}\n")
            written.append(f)
    return written


def elf_bits(path):
    """32 or 64 for an ELF file, None otherwise."""
    try:
        with open(path, "rb") as f:
            head = f.read(5)
    except OSError:
        return None
    return {1: 32, 2: 64}.get(head[4]) if head[:4] == b"\x7fELF" and len(head) == 5 else None


def launch_target_32bit(dirpath, exe):
    """True if the game's entry point is 32-bit only and this system can't run it (no multilib).
    For a binary, check it; for a launch script, check the binaries next to it: 32-bit ones and
    no 64-bit one (many games ship both and let the script pick)."""
    if Path("/lib/ld-linux.so.2").exists():
        return False
    target = Path(dirpath) / exe
    cands = [target]
    try:
        if target.read_bytes()[:2] == b"#!":
            cands = [p for p in target.parent.iterdir() if p.is_file() and ".so" not in p.name]
    except OSError:
        pass
    bits = {elf_bits(c) for c in cands}
    return 32 in bits and 64 not in bits

def disable_old_bundled_sdl(dirpath):
    """Old Linux ports bundle an old SDL: SDL2 without Wayland support (FNA/MonoKickstart, e.g.
    Bastion; with no X server it finds no display), or the original SDL 1.2 (native Psychonauts:
    its fullscreen and mouse handling break under Xwayland on the rotated panel). Move such copies
    aside so the system's compat libraries are used (sdl2-compat and sdl12-compat on SDL3, Wayland
    capable), but only if one exists for the same architecture (lib32-* for 32-bit games). The
    game's own rpath ($ORIGIN) would otherwise always pick the bundled copy. Returns the renamed
    paths."""
    moved = []
    for name, modern in (("libSDL2-2.0.so.0", b"wayland"), ("libSDL-1.2.so.0", b"SDL12COMPAT")):
        for lib in Path(dirpath).rglob(name):
            if lib.is_symlink() or not lib.is_file() or modern in lib.read_bytes():
                continue
            libdir = {32: "/usr/lib32", 64: "/usr/lib"}.get(elf_bits(lib))
            if not libdir or not (Path(libdir) / name).exists():
                continue
            target = lib.with_name(lib.name + ".bundled")
            lib.rename(target)
            moved.append(str(target))
    return moved


COMPAT_LIBS = Path.home() / ".local/share/gpd/compat/gamemaker-lib32"


def add_compat_libs(dirpath):
    """Old GameMaker Linux runners (2015-2016: Hyper Light Drifter, Risk of Rain) need OpenSSL 1.0
    and a Steam-runtime libcurl (symbol version CURL_OPENSSL_3), which Arch doesn't have. HLD ships
    them in lib/, others don't. If a 32-bit binary in the game needs libcrypto.so.1.0.0 and the
    game lacks it, copy our saved set (taken from HLD, see docs/frontend.md) into its lib/, which
    the game's run.sh puts on LD_LIBRARY_PATH. Returns the files copied."""
    root = Path(dirpath)
    if not COMPAT_LIBS.is_dir() or any(root.rglob("libcrypto.so.1.0.0")):
        return []
    for p in root.rglob("*"):
        if p.is_file() and elf_bits(p) == 32 and ".so" not in p.name:
            r = subprocess.run(["readelf", "-d", str(p)], capture_output=True, text=True)
            if "[libcrypto.so.1.0.0]" in r.stdout:
                lib = root / "lib"
                lib.mkdir(exist_ok=True)
                copied = []
                for f in COMPAT_LIBS.iterdir():
                    shutil.copy2(f, lib / f.name)
                    copied.append(str(lib / f.name))
                return copied
    return []


# ---------- launcher + metadata ----------
LAUNCHER = """#!/bin/sh
# Generated by ~/gpd/scripts/games for {name} ({source} {id}). Edit freely: it is not
# overwritten when the metadata is regenerated. Runner: {runner}.
cd "$(dirname "$0")/{workdir}" || exit 1
{env}exec {cmd} "$@"
"""


WINE32 = Path.home() / "Games/tools/wine32"          # old-style (non-WoW64) Wine, see docs/frontend.md


def pe_is_32bit(path):
    """True/False for a 32-/64-bit Windows executable, None if it is not a PE file we can read."""
    try:
        with open(path, "rb") as f:
            head = f.read(64)
            if head[:2] != b"MZ":
                return None
            f.seek(int.from_bytes(head[60:64], "little"))
            sig = f.read(6)
    except OSError:
        return None
    if sig[:4] != b"PE\0\0":
        return None
    machine = int.from_bytes(sig[4:6], "little")
    return {0x14c: True, 0x8664: False}.get(machine)



def write_launcher(rec, force=False):
    d = Path(rec["dir"])
    path = d / LAUNCHER_NAME
    # `"runner": "custom"` in .gpd-game.json: a hand-written launcher the generator never overwrites
    if path.exists() and (not force or rec.get("runner") == "custom"):
        return path
    exe = Path(rec["exe"])
    workdir = str(exe.parent) if str(exe.parent) != "." else "."
    # 32-bit Windows games default to the old-style Wine in a true 32-bit prefix: the system Wine is the new
    # WoW64 build, on which 32-bit D3D games get slower and slower while playing (buffer copies,
    # docs/frontend.md: Sam & Max, Spelunky). `"runner": "wine"` in .gpd-game.json forces the system Wine,
    # `"runner": "wine32"` forces the old-style one (also for a 64-bit exe).
    runner_pref = rec.get("runner")
    want32 = runner_pref == "wine32" or (runner_pref in (None, "") and pe_is_32bit(d / exe) is True)
    if rec["os"] == "windows" and want32 and (WINE32 / "bin/wine").exists():
        runner = "old-style Wine (32-bit prefix ~/.wine32)"
        env = ('export WINEPREFIX="$HOME/.wine32" WINEARCH=win32 WINEDEBUG=-all\n'
               'export WINEDLLOVERRIDES="mscoree,mshtml,winegstreamer=d"\n')
        cmd = f'"$HOME/Games/tools/wine32/bin/wine" "./{exe.name}"'
    elif rec["os"] == "windows":
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


def write_metadata():
    """Regenerate ~/Games/installed/metadata.pegasus.txt. Same collection name and shortname as
    ~/Games/pc, so these games merge into PC Games."""
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
        desc = rec.get("description") or f"Installed from {rec['source'].title()} ({runner})."
        lines += [f"description: {desc}", ""]
    (INSTALL_ROOT / "metadata.pegasus.txt").write_text("\n".join(lines))
    ensure_game_dir_listed()
