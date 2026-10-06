"""Steam source: library from games/steam-library.json, downloads via DepotDownloader.

The Steam password and Steam Guard code are typed by the user into DepotDownloader; this code
never sees them. The Web API key for refreshing the library is asked for each time, not stored.
"""
import getpass, json, re, subprocess, time, urllib.parse, urllib.request
from pathlib import Path

from .. import core

LIBRARY = core.REPO / "games" / "steam-library.json"
UA = {"User-Agent": "gpd-games/1.0 (personal library tool)"}
COVER_URLS = [
    "https://shared.cloudflare.steamstatic.com/store_item_assets/steam/apps/{id}/library_600x900_2x.jpg",
    "https://shared.cloudflare.steamstatic.com/store_item_assets/steam/apps/{id}/library_600x900.jpg",
    "https://cdn.cloudflare.steamstatic.com/steam/apps/{id}/library_600x900.jpg",
    "https://cdn.cloudflare.steamstatic.com/steam/apps/{id}/header.jpg",
]


def _get_json(url, headers=None):
    req = urllib.request.Request(url, headers=headers or UA)
    with urllib.request.urlopen(req, timeout=60) as r:
        return json.load(r)


class SteamSource:
    name = "steam"
    title = "Steam"

    def load_library(self):
        try:
            return json.loads(LIBRARY.read_text())["games"]
        except FileNotFoundError:
            return []

    def platforms(self, game):
        return ["linux", "windows"] if game.get("linux") else ["windows", "linux"]

    def install(self, game, dest, osname):
        cfg = core.load_config()
        user = cfg.get("steam_username")
        if not user:
            user = input("Steam username (saved in ~/.config/gpd/games.json): ").strip()
            if not user:
                return False
            cfg["steam_username"] = user
            core.save_config(cfg)
        cmd = ["depotdownloader", "-app", game["id"], "-os", osname, "-username", user,
               "-remember-password", "-validate", "-dir", str(dest)]
        print("$", " ".join(cmd))
        print("(DepotDownloader asks for your password / Steam Guard code the first time.)\n")
        return subprocess.run(cmd).returncode == 0

    def fetch_cover(self, game, path):
        for url in COVER_URLS:
            try:
                req = urllib.request.Request(url.format(id=game["id"]), headers=UA)
                with urllib.request.urlopen(req, timeout=30) as r:
                    data = r.read()
                if len(data) > 1000:
                    Path(path).write_bytes(data)
                    return True
            except Exception:
                continue
        return False

    # ----- library refresh: Steam Web API + PCGamingWiki -----
    def refresh(self):
        cfg = core.load_config()
        print("Refresh the Steam library (owned games, DRM status from PCGamingWiki).")
        print("Get a Web API key at https://steamcommunity.com/dev/apikey (it is not stored).")
        key = getpass.getpass("Web API key: ").strip()
        if not key:
            return False
        steamid = cfg.get("steamid")
        if not steamid:
            who = input("SteamID64 or profile name (steamcommunity.com/id/<name>): ").strip()
            if who.isdigit():
                steamid = who
            else:
                r = _get_json(f"https://api.steampowered.com/ISteamUser/ResolveVanityURL/v1/?key={key}&vanityurl={who}")
                steamid = r["response"].get("steamid")
                if not steamid:
                    print("Could not resolve that name."); return False
            cfg["steamid"] = steamid
            core.save_config(cfg)
        owned = _get_json(f"https://api.steampowered.com/IPlayerService/GetOwnedGames/v1/?key={key}"
                          f"&steamid={steamid}&include_appinfo=1&include_played_free_games=1")["response"]
        games = owned.get("games", [])
        print(f"{len(games)} owned games. Looking up PCGamingWiki (50 per request)...")
        old = {g["id"]: g for g in self.load_library()}
        pcgw = self._pcgw([(str(g["appid"]), g["name"]) for g in games])
        out = []
        for g in games:
            gid = str(g["appid"])
            prev = old.get(gid, {})
            info = pcgw.get(gid, {})
            out.append({"source": "steam", "id": gid, "name": re.sub(r"[™®]", "", g["name"]).strip(),
                        "hours": round(g.get("playtime_forever", 0) / 60, 1),
                        "drm": info.get("drm", prev.get("drm", "unknown")),
                        "controller": info.get("controller", prev.get("controller")),
                        "linux": prev.get("linux"), "tier": prev.get("tier"),
                        "pcgw": info.get("page", prev.get("pcgw"))})
        out.sort(key=lambda g: g["name"].lower())
        LIBRARY.parent.mkdir(exist_ok=True)
        LIBRARY.write_text(json.dumps({"generated": time.strftime("%Y-%m-%d"), "games": out}, indent=1) + "\n")
        new = len(set(x["id"] for x in out) - set(old))
        print(f"Saved {len(out)} games ({new} new) to {LIBRARY}. New games have no tier yet.")
        return True

    def _pcgw(self, items):
        def clean(n):
            return re.sub(r"[™®©]", "", n).strip().replace("_", " ")
        byname = {}
        for gid, name in items:
            byname.setdefault(clean(name), []).append(gid)
        names, result = list(byname), {}
        for i in range(0, len(names), 50):
            batch = names[i:i + 50]
            q = urllib.parse.urlencode({"action": "query", "prop": "revisions", "rvprop": "content",
                                        "rvslots": "main", "titles": "|".join(batch), "redirects": 1,
                                        "format": "json"})
            try:
                data = _get_json(f"https://www.pcgamingwiki.com/w/api.php?{q}")["query"]
            except Exception as e:
                print("  PCGamingWiki request failed:", e); continue
            m = {t: t for t in batch}
            for k in ("normalized", "redirects"):
                for x in data.get(k, []):
                    for t, v in list(m.items()):
                        if v == x["from"]:
                            m[t] = x["to"]
            pages = {p["title"]: p for p in data["pages"].values()}
            for t in batch:
                p = pages.get(m[t])
                text = p["revisions"][0]["slots"]["main"]["*"] if p and "revisions" in p else ""
                if not text:
                    continue
                ids = {x for s in re.findall(r"\|\s*steam appid\s*=\s*([\d, ]+)", text) for x in re.findall(r"\d+", s)}
                drm = None
                for row in re.findall(r"\{\{Availability/row\|\s*Steam\s*\|([^}]*)\}\}", text):
                    parts = [x.strip() for x in row.split("|")]
                    if len(parts) >= 2:
                        drm = parts[1]
                ctrl = re.search(r"\|\s*controller support\s*=\s*(\w+)", text)
                for gid in byname[t]:
                    if gid in ids:
                        result[gid] = {"page": m[t],
                                       "drm": "free" if drm and drm.lower().startswith("drm-free") else ("steam" if drm else "unknown"),
                                       "controller": bool(ctrl and ctrl.group(1).lower() == "true")}
            print(f"  {min(i + 50, len(names))}/{len(names)}")
            time.sleep(1)
        return result
