"""Steam source: library from games/steam-library.json, downloads via DepotDownloader.

The Steam password and Steam Guard code are typed by the user into DepotDownloader; this code
never sees them. The Web API key for refreshing the library is asked for each time, not stored.
"""
import getpass, json, re, subprocess, time, urllib.parse, urllib.request
from pathlib import Path

from .. import core

LIBRARY = core.REPO / "games" / "steam-library.json"
UA = {"User-Agent": "gpd-games/1.0 (personal library tool)"}
STORE_ITEMS = "https://api.steampowered.com/IStoreBrowseService/GetItems/v1/"
APPDETAILS = "https://store.steampowered.com/api/appdetails?appids={id}"
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


def store_platforms(ids):
    """{appid: {"windows": bool, "linux": bool}} from the Steam store; 100 games per request, no key."""
    out = {}
    for i in range(0, len(ids), 100):
        q = {"ids": [{"appid": int(x)} for x in ids[i:i + 100]],
             "context": {"language": "english", "country_code": "NO"},
             "data_request": {"include_platforms": True}}
        reply = _get_json(STORE_ITEMS + "?" + urllib.parse.urlencode({"input_json": json.dumps(q)}))
        for item in reply["response"].get("store_items", []):
            p = item.get("platforms")
            if item.get("success") == 1 and p is not None:
                out[str(item["appid"])] = {"windows": bool(p.get("windows")), "linux": bool(p.get("steamos_linux"))}
    return out


def storage_mb(requirements):
    """Disk space from a store requirements block ("Storage: 2 GB available space"), in MB."""
    text = re.sub(r"<[^>]+>", " ", requirements.get("minimum", "") if isinstance(requirements, dict) else "")
    m = re.search(r"(?:Storage|Hard Drive|Hard Disk|Disk Space)[^:\d]*:?\s*([\d.,]+)\s*(GB|MB)", text, re.I)
    if not m:
        return None
    n = float(m.group(1).replace(",", "."))
    return round(n * 1024 if m.group(2).upper() == "GB" else n)


class SteamSource:
    name = "steam"
    title = "Steam"

    def load_library(self):
        try:
            return json.loads(LIBRARY.read_text())["games"]
        except FileNotFoundError:
            return []

    def platforms(self, game):
        """Builds that exist, preferred first. The Steam store is asked the first time a game is
        opened and the answer is kept in the library. A Linux build found to be 32-bit ("linux32")
        is left out: it can't run without multilib, while Wine runs 32-bit Windows games."""
        self._lookup_platforms(game)
        if "windows" not in game:  # store lookup failed (offline, delisted): offer both
            return ["linux", "windows"] if game.get("linux") else ["windows", "linux"]
        linux = game.get("linux") and not game.get("linux32")
        builds = (["linux"] if linux else []) + (["windows"] if game["windows"] else [])
        return builds or ["windows", "linux"]

    def _lookup_platforms(self, game):
        if "windows" in game:
            return
        try:
            found = store_platforms([game["id"]]).get(game["id"])
        except Exception:
            return
        if found:
            self.update(game, **found)

    def size_hint(self, game):
        """Publisher's disk space estimate in MB (store requirements), looked up once and kept."""
        if "size_mb" not in game:
            try:
                reply = _get_json(APPDETAILS.format(id=game["id"]))[game["id"]]
                req = reply["data"].get("pc_requirements") if reply.get("success") else None
            except Exception:
                return None
            self.update(game, size_mb=storage_mb(req or {}))
        return game["size_mb"]

    def update(self, game, **fields):
        """Change fields of one game, in memory and in the library file."""
        game.update(fields)
        data = json.loads(LIBRARY.read_text())
        for g in data["games"]:
            if g["id"] == game["id"]:
                g.update(fields)
        LIBRARY.write_text(json.dumps(data, indent=1) + "\n")

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
               *(["-osarch", "64"] if osname == "linux" else []),
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
        print(f"{len(games)} owned games. Asking the Steam store for platforms...")
        try:
            plats = store_platforms([str(g["appid"]) for g in games])
        except Exception as e:
            print("  Steam store request failed:", e); plats = {}
        print(f"Looking up PCGamingWiki (50 per request)...")
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
                        "pcgw": info.get("page", prev.get("pcgw")),
                        **{k: prev[k] for k in ("windows", "linux32", "size_mb") if k in prev},
                        **plats.get(gid, {})})
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
