"""Game sources. Each module provides a class with:

    name                      short id, e.g. "steam" (stored in each installed game's record)
    title                     display name
    load_library() -> list    game dicts: {"source", "id", "name", "hours", "drm", "controller",
                              "linux", "tier", ...}; "drm" is "free" | "steam" | "unknown"
    install(game, dest, osname) -> bool
                              download into dest (interactive terminal, curses suspended)
    platforms(game) -> list   OS builds that exist and can run, preferred first ("linux", "windows")
    update(game, **fields)    change fields of a game in the library (optional; e.g. linux32=True)
    size_hint(game) -> MB     approximate install size, or None (optional)
    exact_size(game, osname) -> (disk bytes, download bytes) or None   (optional)
    fetch_cover(game, path)   save portrait box art (best effort)
    refresh() -> bool         interactive re-harvest of the library (optional)

To add GOG later: sources/gog.py (e.g. via `lgogdownloader`, which is in the AUR, or the GOG
API), register it in SOURCES, and store its library in games/gog-library.json.
"""
from .steam import SteamSource

SOURCES = {s.name: s for s in (SteamSource(),)}
