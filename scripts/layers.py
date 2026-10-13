"""Which layers this machine uses (docs/repo-layout.md): the common components, one device, one user.

The host file ~/.config/gpd/host (not in the repo; key=value, # comments) says:
  device=gpd-win1                    devices/<device> in the repo
  user=robin                         users/<user> in the repo ...
  user_dir=~/gpd-user                ... or a user layer kept elsewhere (e.g. a private repo); wins over user=
  components=base desktop inputd shelf   components/<name>, applied in this order (default: all of DEFAULT)

Without a host file (or a key in it) the user layer is the only users/* other than users/example, and the
device is unknown (sync refuses to install then). GPD_USER_DIR in the environment overrides the user layer.
"""
import os
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
HOST = Path(os.environ.get("XDG_CONFIG_HOME") or Path.home() / ".config") / "gpd" / "host"
DEFAULT = ["base", "desktop", "inputd", "shelf"]


def host():
    vals = {}
    try:
        for line in HOST.read_text().splitlines():
            line = line.split("#", 1)[0].strip()
            if "=" in line:
                k, v = line.split("=", 1)
                vals[k.strip()] = v.strip().strip('"\'')
    except FileNotFoundError:
        pass
    return vals


def device_dir():
    d = host().get("device")
    return REPO / "devices" / d if d else None


def user_dir():
    env = os.environ.get("GPD_USER_DIR")
    if env:
        return Path(env).expanduser()
    h = host()
    if h.get("user_dir"):
        return Path(h["user_dir"]).expanduser()
    if h.get("user"):
        return REPO / "users" / h["user"]
    found = [p for p in (REPO / "users").glob("*") if p.is_dir() and p.name != "example"]
    return found[0] if len(found) == 1 else None


def user_file(rel):
    """A file of the user layer (e.g. "games/saves.json"); exits with a hint when there is no user layer."""
    u = user_dir()
    if not u:
        raise SystemExit(f"no user layer: set user= or user_dir= in {HOST} (docs/repo-layout.md)")
    return u / rel


def components():
    c = host().get("components")
    return c.split() if c else list(DEFAULT)


def layers():
    """[(label, root, manifest)] in the order sync applies them; later layers win for the same live path."""
    out = [(f"component {c}", REPO, REPO / "components" / c / "manifest.tsv") for c in components()]
    d = device_dir()
    if d:
        out.append((f"device {d.name}", d, d / "manifest.tsv"))
    u = user_dir()
    if u:
        out.append((f"user {u.name}", u, u / "manifest.tsv"))
    return out


def listed(name):
    """The lines of every layer's <name> file (packages.txt, services.txt next to its manifest), in layer
    order, without comments and repeats."""
    out = []
    for label, root, manifest in layers():
        f = manifest.parent / name
        if f.exists():
            for line in f.read_text().splitlines():
                line = line.split("#", 1)[0].strip()
                if line and line not in out:
                    out.append(line)
    return out


def packages():
    return listed("packages.txt")


def services():
    return listed("services.txt")


if __name__ == "__main__":  # `python3 scripts/layers.py`: print them (also for shell scripts: layers.sh)
    print(f"host file: {HOST} ({'found' if HOST.exists() else 'missing'})")
    print(f"device_dir={device_dir() or ''}")
    print(f"user_dir={user_dir() or ''}")
    print(f"components={' '.join(components())}")
