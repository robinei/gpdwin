"""Curses UI: browse owned games with filters, install/uninstall, add to Pegasus."""
import curses, locale, os, shutil, subprocess, sys
from pathlib import Path

from . import core
from .sources import SOURCES

DRM_FILTERS = [("DRM-free", lambda g: g["drm"] == "free"),
               ("not Steam-DRM", lambda g: g["drm"] != "steam"),
               ("any DRM", lambda g: True)]
TIER_FILTERS = [("any tier", lambda g: True),
                ("runs well", lambda g: g.get("tier") == "good"),
                ("well+maybe", lambda g: g.get("tier") in ("good", "maybe")),
                ("playable", lambda g: g.get("tier") in ("good", "maybe", "mouse"))]
def size_text(mb):
    """Approximate install size for the list: 200M, 1.5G; blank when unknown."""
    if not mb:
        return ""
    return f"{mb}M" if mb < 1000 else f"{mb / 1024:.1f}G"


CTRL_FILTERS = [("any input", lambda g: True), ("controller", lambda g: g.get("controller"))]
TIER_SHORT = {"good": "well", "maybe": "maybe", "mouse": "mouse", "heavy": "heavy", None: ""}
HELP = "↑↓ PgUp/PgDn move  / search  d DRM  t tier  c controller  i installed  s sort  Enter actions  R refresh  q quit"


class App:
    def __init__(self):
        self.games = [g for s in SOURCES.values() for g in s.load_library()]
        self.inst = core.installed()
        self.drm = self.tier = self.ctrl = 0
        self.only_installed = False
        self.search = ""
        self.sort = 0           # 0 name, 1 hours played
        self.pos = self.top = 0
        self.msg = ""
        self.changed = False

    # ----- data -----
    def is_installed(self, g):
        return (g["source"], g["id"]) in self.inst

    def visible(self):
        out = [g for g in self.games
               if DRM_FILTERS[self.drm][1](g) and TIER_FILTERS[self.tier][1](g) and CTRL_FILTERS[self.ctrl][1](g)
               and (not self.only_installed or self.is_installed(g))
               and self.search.lower() in g["name"].lower()]
        if self.sort == 1:
            out.sort(key=lambda g: -g.get("hours", 0))
        return out

    # ----- drawing -----
    def draw(self, scr):
        scr.erase()
        h, w = scr.getmaxyx()
        rows = self.visible()
        self.pos = max(0, min(self.pos, len(rows) - 1))
        listh = h - 4
        if self.pos < self.top:
            self.top = self.pos
        elif self.pos >= self.top + listh:
            self.top = self.pos - listh + 1
        head = (f" Games  {len(rows)}/{len(self.games)}  | {DRM_FILTERS[self.drm][0]} | {TIER_FILTERS[self.tier][0]}"
                f" | {CTRL_FILTERS[self.ctrl][0]}{' | installed' if self.only_installed else ''}"
                f"{' | search: ' + self.search if self.search else ''} | free {core.human(core.free_space())} ")
        scr.addnstr(0, 0, head.ljust(w), w, curses.A_REVERSE)
        scr.addnstr(1, 0, f"   {'name':<{max(10, w - 42)}} {'tier':<6}{'drm':<6}{'pad':<4}{'os':<4}{'size':>6}{'hours':>6}", w, curses.A_BOLD)
        for i, g in enumerate(rows[self.top:self.top + listh]):
            idx = self.top + i
            mark = "✓" if self.is_installed(g) else " "
            name = g["name"][:max(10, w - 42)]
            line = (f" {mark} {name:<{max(10, w - 42)}} {TIER_SHORT.get(g.get('tier'), ''):<6}"
                    f"{g['drm']:<6}{'yes' if g.get('controller') else '':<4}{('L32' if g.get('linux32') else 'L') if g.get('linux') else '':<4}"
                    f"{size_text(g.get('size_mb')):>6}{g.get('hours', 0):>6}")
            attr = curses.A_REVERSE if idx == self.pos else 0
            if self.is_installed(g):
                attr |= curses.color_pair(1)
            scr.addnstr(2 + i, 0, line.ljust(w), w, attr)
        scr.addnstr(h - 2, 0, (self.msg or "").ljust(w), w, curses.color_pair(2))
        scr.addnstr(h - 1, 0, HELP[:w - 1], w - 1, curses.A_DIM)
        scr.refresh()
        return rows

    def prompt(self, scr, text):
        h, w = scr.getmaxyx()
        curses.echo(); curses.curs_set(1)
        scr.addnstr(h - 2, 0, text.ljust(w), w)
        s = scr.getstr(h - 2, len(text), 60).decode(errors="replace")
        curses.noecho(); curses.curs_set(0)
        return s

    def menu(self, scr, title, options):
        """Small selection list; returns the chosen key or None."""
        h, w = scr.getmaxyx()
        sel = 0
        while True:
            bw = min(w - 4, max(len(title), *(len(o[1]) for o in options)) + 6)
            bh = len(options) + 4
            win = curses.newwin(bh, bw, (h - bh) // 2, (w - bw) // 2)
            win.box()
            win.addnstr(1, 2, title, bw - 4, curses.A_BOLD)
            for i, (_, label) in enumerate(options):
                win.addnstr(3 + i, 2, label, bw - 4, curses.A_REVERSE if i == sel else 0)
            win.refresh()
            k = scr.getch()
            if k in (curses.KEY_UP, ord("k")):
                sel = (sel - 1) % len(options)
            elif k in (curses.KEY_DOWN, ord("j")):
                sel = (sel + 1) % len(options)
            elif k in (10, 13, curses.KEY_ENTER, ord(" ")):
                return options[sel][0]
            elif k in (27, ord("q")):
                return None

    # ----- actions -----
    def outside(self, scr, fn):
        """Run fn() in the plain terminal (curses suspended)."""
        curses.def_prog_mode(); curses.endwin()
        os.system("clear")
        try:
            result = fn()
        except KeyboardInterrupt:
            print("\nInterrupted."); result = False
        except Exception as e:
            print(f"\nError: {e}"); result = False
        input("\nPress Enter to return ")
        curses.reset_prog_mode(); scr.refresh()
        return result

    def actions(self, scr, g):
        src = SOURCES[g["source"]]
        rec = self.inst.get((g["source"], g["id"]))
        info = f"{g['name']}  ({src.title} {g['id']})"
        if not rec and hasattr(src, "size_hint") and src.size_hint(g):
            info += f"  ~{size_text(g['size_mb'])} (store estimate)"
        if rec:
            opts = [("exe", f"Change executable (now {rec['exe']})"), ("launcher", "Rewrite launcher script"),
                    ("uninstall", f"Uninstall ({core.human(rec.get('size', 0))})"), (None, "Back")]
        else:
            opts = [(f"install:{p}", f"Install {p} build" + (" (preferred)" if i == 0 else ""))
                    for i, p in enumerate(src.platforms(g))] + [(None, "Back")]
            if g["drm"] == "steam":
                opts.insert(0, (None, "Note: PCGamingWiki says this needs the Steam client"))
        choice = self.menu(scr, info, opts)
        if not choice:
            return
        if choice.startswith("install:"):
            self.outside(scr, lambda: self.install(g, choice.split(":")[1]))
        elif choice == "uninstall":
            if self.prompt(scr, f"Delete {rec['dir']}? type yes: ") == "yes":
                core.uninstall(rec); self.changed = True
                self.msg = f"Uninstalled {g['name']}."
        elif choice == "exe":
            self.outside(scr, lambda: self.pick_exe(rec, force_ask=True))
        elif choice == "launcher":
            core.write_launcher(rec, force=True); core.write_pegasus(); self.changed = True
            self.msg = "Launcher rewritten."
        self.inst = core.installed()

    def pick_exe(self, rec, force_ask=False):
        cands = core.find_executables(rec["dir"], rec["os"], rec["name"])
        if not cands:
            print("No executable found. Edit gpd-launch.sh by hand in", rec["dir"]); return False
        best = cands[0][0]
        clear_winner = len(cands) == 1 or cands[0][1] - cands[1][1] >= 5
        if force_ask or not clear_winner:
            print("Executables found (best guess first):")
            for i, (c, score) in enumerate(cands[:15], 1):
                print(f"  {i:2}. {c}   [{score}]")
            ans = input(f"Number to use [1]: ").strip() or "1"
            if ans.isdigit() and 1 <= int(ans) <= len(cands[:15]):
                best = cands[int(ans) - 1][0]
        rec["exe"] = best
        if rec["os"] == "linux" and core.launch_target_32bit(rec["dir"], best):
            print("\nWARNING: this Linux build is 32-bit and this system has no 32-bit runtime (no\n"
                  "multilib), so it can't start. Uninstall it and install the Windows build instead\n"
                  "(Wine runs 32-bit Windows games without multilib).")
        core.save_record(rec)
        core.write_launcher(rec, force=True)
        core.write_pegasus()
        self.changed = True
        print(f"Using {best}. Launcher: {Path(rec['dir']) / core.LAUNCHER_NAME}")
        return True

    def install(self, g, osname):
        src = SOURCES[g["source"]]
        dest = core.game_dir(g)
        print(f"Installing {g['name']} ({osname}) into {dest}\nFree space: {core.human(core.free_space())}\n")
        dest.mkdir(parents=True, exist_ok=True)
        if not src.install(g, dest, osname):
            if osname == "linux" and core.dir_size(dest) == 0:
                print("\nNothing was downloaded. If DepotDownloader found no depots, this game has no\n"
                      "64-bit Linux build (32-bit ones are skipped: no multilib here).")
                shutil.rmtree(dest, ignore_errors=True)
                return self.offer_windows(g)
            print("\nDownload failed. Files so far are kept in", dest, "(install again to resume).")
            return False
        if osname == "linux":
            cands = core.find_executables(dest, "linux", g["name"])
            if cands and core.launch_target_32bit(dest, cands[0][0]):
                print("\nThis Linux build is 32-bit only, and this system has no 32-bit runtime (no\n"
                      "multilib), so it can't start. Removing it.")
                shutil.rmtree(dest, ignore_errors=True)
                if hasattr(src, "update"):
                    src.update(g, linux32=True)   # remembered: only Windows is offered from now on
                return self.offer_windows(g)
        rec = {"source": g["source"], "id": g["id"], "name": g["name"], "os": osname,
               "dir": str(dest), "exe": "", "size": core.dir_size(dest)}
        print(f"\nDownloaded {core.human(rec['size'])}.")
        if src.fetch_cover(g, dest / core.COVER_NAME):
            print("Cover art saved.")
        core.save_record(rec)
        if osname == "linux":
            for m in core.disable_old_bundled_sdl(dest):
                print(f"Bundled SDL2 without Wayland support moved aside: {m}")
        if self.pick_exe(rec):
            print(f"Added to Pegasus PC Games ({'Wine' if osname == 'windows' else 'native'}).")
        return True

    def offer_windows(self, g):
        if "windows" not in SOURCES[g["source"]].platforms(g):
            print("There is no Windows build either.")
            return False
        ans = input("Download the Windows build instead (Wine runs 32-bit Windows games)? [Y/n] ").strip().lower()
        return self.install(g, "windows") if ans in ("", "y", "yes") else False

    def refresh(self, scr):
        srcs = [s for s in SOURCES.values() if hasattr(s, "refresh")]
        s = srcs[0] if len(srcs) == 1 else SOURCES.get(self.menu(scr, "Refresh which library?", [(x.name, x.title) for x in srcs]))
        if s and self.outside(scr, s.refresh):
            self.games = [g for x in SOURCES.values() for g in x.load_library()]

    # ----- main loop -----
    def run(self, scr):
        curses.curs_set(0)
        curses.use_default_colors()
        curses.init_pair(1, curses.COLOR_GREEN, -1)
        curses.init_pair(2, curses.COLOR_YELLOW, -1)
        while True:
            rows = self.draw(scr)
            k = scr.getch()
            self.msg = ""
            page = max(1, scr.getmaxyx()[0] - 5)
            if k in (ord("q"), 27):
                return
            elif k in (curses.KEY_UP, ord("k")): self.pos -= 1
            elif k in (curses.KEY_DOWN, ord("j")): self.pos += 1
            elif k == curses.KEY_PPAGE: self.pos -= page
            elif k == curses.KEY_NPAGE: self.pos += page
            elif k == curses.KEY_HOME: self.pos = 0
            elif k == curses.KEY_END: self.pos = len(rows) - 1
            elif k == ord("/"): self.search = self.prompt(scr, "search: "); self.pos = 0
            elif k == ord("d"): self.drm = (self.drm + 1) % len(DRM_FILTERS); self.pos = 0
            elif k == ord("t"): self.tier = (self.tier + 1) % len(TIER_FILTERS); self.pos = 0
            elif k == ord("c"): self.ctrl = (self.ctrl + 1) % len(CTRL_FILTERS); self.pos = 0
            elif k == ord("i"): self.only_installed = not self.only_installed; self.pos = 0
            elif k == ord("s"): self.sort = 1 - self.sort; self.msg = "sorted by " + ("hours played" if self.sort else "name")
            elif k == ord("R"): self.refresh(scr)
            elif k in (10, 13, curses.KEY_ENTER) and rows:
                self.actions(scr, rows[self.pos])


def restart_pegasus_after_exit():
    """Pegasus only rescans metadata on start. Restart it once this tool (its child) has exited."""
    if not subprocess.run(["pgrep", "-x", "pegasus-fe"], capture_output=True).stdout:
        return
    me = os.getpid()
    # Pegasus sometimes ignores SIGTERM; escalate to SIGKILL so we never end up with two.
    script = (f"while kill -0 {me} 2>/dev/null; do sleep 0.3; done; sleep 0.5; pkill -x pegasus-fe; "
              f"for i in 1 2 3 4 5 6 7 8 9 10; do pgrep -x pegasus-fe >/dev/null || break; sleep 0.3; done; "
              f"pkill -KILL -x pegasus-fe; sleep 0.5; "
              f"pgrep -x pegasus-fe >/dev/null || swaymsg exec ~/.config/pegasus-frontend/run")
    subprocess.Popen(["setsid", "-f", "sh", "-c", script], stdin=subprocess.DEVNULL,
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def main():
    locale.setlocale(locale.LC_ALL, "")
    app = App()
    curses.wrapper(app.run)
    if app.changed:
        ans = input("Games changed. Restart Pegasus now so they show up? [Y/n] ").strip().lower()
        if ans in ("", "y", "yes"):
            restart_pegasus_after_exit()
            print("Pegasus restarts when this window closes.")
    return 0
