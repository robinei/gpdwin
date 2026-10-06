"""Game sources. Each module provides a class with:

    name                      short id, e.g. "steam" (stored in each installed game's record)
    title                     display name
    load_library() -> list    game dicts: {"source", "id", "name", "hours", "drm", "controller",
                              "linux", "tier", ...}; "drm" is "free" | "steam" | "unknown"
    install(game, dest, osname) -> bool
                              download into dest (interactive terminal, curses suspended)
    platforms(game) -> list   OS builds to offer, preferred first ("linux", "windows")
    fetch_cover(game, path)   save portrait box art (best effort)
    refresh() -> bool         interactive re-harvest of the library (optional)

To add GOG later: sources/gog.py (e.g. via `lgogdownloader`, which is in the AUR, or the GOG
API), register it in SOURCES, and store its library in games/gog-library.json.
"""
from .steam import SteamSource

SOURCES = {s.name: s for s in (SteamSource(),)}
