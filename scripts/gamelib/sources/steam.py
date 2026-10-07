"""Steam source: library from games/steam-library.json, downloads via DepotDownloader.

The Steam password and Steam Guard code are typed by the user into DepotDownloader; this code
never sees them. The Web API key for refreshing the library is asked for each time, not stored.
"""
import getpass, json, re, subprocess, tempfile, time, urllib.error, urllib.parse, urllib.request
from pathlib import Path

from .. import core

LIBRARY = core.REPO / "games" / "steam-library.json"
UA = {"User-Agent": "gpd-games/1.0 (personal library tool)"}
STORE_ITEMS = "https://api.steampowered.com/IStoreBrowseService/GetItems/v1/"
APPDETAILS = "https://store.steampowered.com/api/appdetails?appids={id}"
# 32-bit Linux games need multilib (lib32-glibc provides the 32-bit loader)
MULTILIB = Path("/lib/ld-linux.so.2").exists()
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
        is only offered with multilib installed; Wine runs 32-bit Windows games either way."""
        self._lookup_platforms(game)
        if "windows" not in game:  # store lookup failed (offline, delisted): offer both
            return ["linux", "windows"] if game.get("linux") else ["windows", "linux"]
        linux = game.get("linux") and (MULTILIB or not game.get("linux32"))
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
        cmd = self._depot_cmd(game, osname, user) + ["-validate", "-dir", str(dest)]
        print("$", " ".join(cmd))
        print("(DepotDownloader asks for your password / Steam Guard code the first time.)\n")
        return subprocess.run(cmd).returncode == 0

    def _depot_cmd(self, game, osname, user):
        return ["depotdownloader", "-app", game["id"], "-os", osname, "-username", user,
                *(["-osarch", "64"] if osname == "linux" and not MULTILIB else []), "-remember-password"]

    def exact_size(self, game, osname):
        """(bytes on disk, bytes to download) from the depot manifests, without downloading the
        game. Needs the saved Steam login (non-interactive); None if that isn't possible."""
        user = core.load_config().get("steam_username")
        if not user:
            return None
        with tempfile.TemporaryDirectory() as tmp:
            try:
                r = subprocess.run(self._depot_cmd(game, osname, user) + ["-manifest-only", "-dir", tmp],
                                   stdin=subprocess.DEVNULL, capture_output=True, timeout=120)
            except subprocess.TimeoutExpired:
                return None
            disk = download = 0
            manifests = list(Path(tmp).glob("manifest_*.txt"))
            for m in manifests:
                text = m.read_text(errors="replace")
                disk += int((re.search(r"Total bytes on disk\s*:\s*(\d+)", text) or [0, 0])[1])
                download += int((re.search(r"Total bytes compressed\s*:\s*(\d+)", text) or [0, 0])[1])
        if r.returncode != 0 or not manifests:
            return None
        self.update(game, size_mb=round(disk / 2**20))  # the list shows the real size from now on
        return disk, download

    def _cover_urls(self, game):
        """Portrait cover first. Newer games keep their art under hashed paths that only the
        store's asset list knows; the fixed URLs are the fallback for older ones."""
        urls = []
        q = {"ids": [{"appid": int(game["id"])}], "context": {"language": "english", "country_code": "NO"},
             "data_request": {"include_assets": True}}
        try:
            reply = _get_json(STORE_ITEMS + "?" + urllib.parse.urlencode({"input_json": json.dumps(q)}))
            assets = reply["response"]["store_items"][0].get("assets", {})
            for name in ("library_capsule_2x", "library_capsule"):
                if assets.get(name):
                    urls.append("https://shared.cloudflare.steamstatic.com/store_item_assets/"
                                + assets["asset_url_format"].replace("${FILENAME}", assets[name]))
        except Exception:
            pass
        return urls + [u.format(id=game["id"]) for u in COVER_URLS]

    def fetch_cover(self, game, path):
        for url in self._cover_urls(game):
            try:
                req = urllib.request.Request(url, headers=UA)
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
        # Names that didn't match a PCGamingWiki page ("Prey" is "Prey (2017)"): find the page by
        # app id, once per game (games resolved earlier keep their page name in "pcgw").
        todo = [(str(g["appid"]), old.get(str(g["appid"]), {}).get("pcgw")) for g in games if str(g["appid"]) not in pcgw]
        known = [(gid, page) for gid, page in todo if page]
        if known:
            pcgw.update(self._pcgw(known))
        missing = [gid for gid, page in todo if not page and gid not in pcgw]
        if missing:
            print(f"Looking up {len(missing)} more on PCGamingWiki by app id (one request each)...")
            pcgw.update(self._pcgw(list(self._pcgw_pages_by_appid(missing).items())))
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

    def _pcgw_pages_by_appid(self, ids):
        """{appid: page title} via PCGamingWiki's app id redirect (one request per game)."""
        pages = {}
        class NoRedirect(urllib.request.HTTPRedirectHandler):
            def redirect_request(self, *a, **k):
                return None
        opener = urllib.request.build_opener(NoRedirect)
        for n, gid in enumerate(ids, 1):
            req = urllib.request.Request(f"https://www.pcgamingwiki.com/api/appid.php?appid={gid}", headers=UA)
            try:
                opener.open(req, timeout=30)
            except urllib.error.HTTPError as e:
                loc = e.headers.get("Location", "")
                if e.code in (301, 302) and "/wiki/" in loc:
                    pages[gid] = urllib.parse.unquote(loc.split("/wiki/", 1)[1]).replace("_", " ")
            except Exception:
                pass
            if n % 50 == 0:
                print(f"  {n}/{len(ids)}")
            time.sleep(0.3)
        return pages

    def fill_drm_by_appid(self):
        """Resolve games with unknown DRM via their app id (no Steam key needed); updates the library."""
        data = json.loads(LIBRARY.read_text())
        unknown = [g for g in data["games"] if g.get("drm", "unknown") == "unknown"]
        pages = self._pcgw_pages_by_appid([g["id"] for g in unknown])
        info = self._pcgw(list(pages.items()))
        for g in unknown:
            i = info.get(g["id"])
            if i:
                g.update({"drm": i["drm"], "controller": i["controller"], "pcgw": i["page"]})
        LIBRARY.write_text(json.dumps(data, indent=1) + "\n")
        return len(unknown), len(pages), len(info)

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
