/*
 * inputd: the device's input and power policy. The Menu key brings up or closes shelf (the
 * frontend); idle dims, turns the screen off and suspends; the lid, the power button and the
 * charger feed into that.
 *
 * The Menu key (bottom right of the keyboard, KEY_COMPOSE) starts shelf if it isn't running, closes
 * it if it has the focus, or else switches to its workspace. While a game started from shelf runs
 * (a child process of it), the key is left to the game. The pad's Guide button is not used: it
 * sends nothing in the pad's mouse mode (docs/hardware.md).
 *
 * Idle (docs/power.md; the times are the "Idle policy" defines below). Idle time runs from the
 * last activity:
 *   2 min        brightness to a third (min 1%; the level before is saved and restored)
 *   3 min        lowest visible level (on AC: 10 min)
 *   5 min        screen off (screen.sh: also freezes shelf and the game), on battery only
 *   30 min       suspend-then-hibernate, on battery only
 * Activity: keyboard, pointer and touch (sway's ext-idle-notify: told when nothing happened for
 * 2 min, which also honours idle inhibitors, so games that inhibit idle keep it awake), the gamepad
 * (sway doesn't count it), opening the lid, the power button, plugging the charger in or out, and
 * waking from sleep. Activity undoes the steps (brightness back, screen on). The charger is part of
 * the decision at every moment, so unplugging after a long idle on AC starts the battery countdown
 * from the unplug.
 *
 * State, not events: events only update facts (lid closed, on AC, last activity, sway idle and
 * since when, when the lid closed), and facts that can be read (lid switch, charger) are read rather
 * than remembered. reconcile() works out from the facts what should be (input
 * off, screen off, brightness level, throttled, suspend) and changes only what differs from what
 * it last applied, so a missed, repeated or reordered event can't leave a wrong state. At start the
 * applied lid and screen state count as unknown and are applied either way (both are safe to
 * repeat), so restarting inputd also repairs what a previous one left behind. A step counts as
 * applied only if its command succeeded; a failed one (or one that hit the STEP_TIMEOUT) is tried
 * again every STEP_RETRY s until it does.
 *
 * Lid switch: closed means all input off (sway's devices; the pad is grabbed), screen off
 * (screen.sh: also freezes shelf and the game) and CPU/GPU throttled (dotfiles/sway/lid.sh);
 * opening it undoes that and counts as activity. While closed, idle runs from the close (or a later
 * unplug or wake) and ignores idle inhibitors (the game is frozen), and only the suspend step
 * applies. Brightness is left alone (level 0 doesn't turn this panel off). logind ignores the lid,
 * see docs/power.md.
 *
 * Power button (logind ignores it, HandlePowerKey=ignore), acted on at release, by the screen
 * state at the press (sway counts the press as activity, so the screen may be back on by the
 * release):
 *   screen dark (sway reports the output powered off: idle or lid): only wakes the screen. Never
 *                sleeps, so a press meant to "turn it on" can't put an awake device with its
 *                screen off to sleep.
 *   screen on:   suspend-then-hibernate.
 *   within RESUME_GUARD s of a resume (the sleep hook resume-time writes the uptime to
 *   RESUME_FILE): ignored, so the press that woke the device doesn't put it back to sleep.
 * Two releases within 2 s count once (the button may show up as several input devices).
 * So "dark screen: hold ~1 s" always turns it on: hibernated/off (the hardware needs the hold),
 * asleep, or screen off.
 *
 * Sleeps until there is something to do: poll() on the input devices, inotify on /dev/input (the
 * pad disconnects while the screen is off and across suspend), kernel uevents (charger), the
 * Wayland socket, and a timeout for the next idle step.
 *
 * Built and installed by `scripts/sync install` (manifest "build" entry); started by sway through
 * dotfiles/sway/inputd.sh, which restarts it after a crash (input stays disabled if it dies with
 * the lid closed; the restarted one repairs that). Without a Wayland connection (sway gone) it
 * exits, and so does the loop. To restart it after an update: `pkill -x inputd`.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/netlink.h>
#include <poll.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include "vendor/ext-idle-notify-v1-client-protocol.h"
#include "vendor/ext-idle-notify-v1-protocol.c"

/* ---- Idle policy: seconds of idle before each step, on battery and on AC (0: never). After a
 * change: `scripts/sync install` and `pkill -x inputd` (dotfiles/sway/inputd.sh restarts it). */
#define DIM_AFTER 120            /* both: brightness to DIM_PERCENT; also when sway reports idle */
#define DIM_PERCENT 33           /* of the level before dimming (at least 1%) */
#define LOW_AFTER_BATTERY 180    /* lowest visible level (raw 1; 0 is backlight off) */
#define LOW_AFTER_AC 600
#define OFF_AFTER_BATTERY 300    /* screen off (screen.sh: also freezes shelf and the game) */
#define OFF_AFTER_AC 0
#define SLEEP_AFTER_BATTERY 1800 /* suspend-then-hibernate (with the lid closed: from the close) */
#define SLEEP_AFTER_AC 0

#define PAD_NAME "Microsoft X-Box 360 pad"
#define LID_NAME "Lid Switch"
#define RESUME_GUARD 3.0 /* seconds after a resume in which the power button is ignored */
#define RESUME_FILE "/run/resume-time"
#define CHARGER_ONLINE "/sys/class/power_supply/bq24190-charger/online"
#define STEP_TIMEOUT "5"     /* seconds a shell step (screen.sh, lid.sh, brightnessctl) may take */
#define SLEEP_RETRY 60       /* seconds: still awake this long after asking to suspend: ask again */
#define STEP_RETRY 5         /* seconds: after a failed step, try again (not on every pass) */
#define BRIGHTNESS_SAVED "idle-brightness" /* in $XDG_RUNTIME_DIR: the level before dimming */
#define MAX_POWER 8    /* input devices with KEY_POWER (here: 2x ACPI Power Button, gpio-keys, Intel HID, keyboard) */

extern char **environ;

static int open_named(const char *want)
{
    DIR *d = opendir("/dev/input");
    struct dirent *e;
    int found = -1;
    while (d && found < 0 && (e = readdir(d))) {
        char path[300], name[256] = "";
        if (strncmp(e->d_name, "event", 5) != 0)
            continue;
        snprintf(path, sizeof path, "/dev/input/%s", e->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            continue;
        if (ioctl(fd, EVIOCGNAME(sizeof name - 1), name) >= 0 && strcmp(name, want) == 0) {
            found = fd;
        } else {
            close(fd);
        }
    }
    if (d)
        closedir(d);
    return found;
}

/* Open the input devices with KEY_POWER or KEY_COMPOSE (the Menu key); returns how many. */
static int open_power(int *fds, int max)
{
    DIR *d = opendir("/dev/input");
    struct dirent *e;
    int n = 0;
    while (d && n < max && (e = readdir(d))) {
        char path[300], name[256] = "";
        unsigned long keys[KEY_MAX / (8 * sizeof(long)) + 1] = { 0 };
        if (strncmp(e->d_name, "event", 5) != 0)
            continue;
        snprintf(path, sizeof path, "/dev/input/%s", e->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            continue;
        ioctl(fd, EVIOCGNAME(sizeof name - 1), name);
#define HAS(k) (keys[(k) / (8 * sizeof(long))] >> ((k) % (8 * sizeof(long))) & 1)
        if (strcmp(name, PAD_NAME) != 0 && ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys) >= 0 &&
            (HAS(KEY_POWER) || HAS(KEY_COMPOSE))) {
#undef HAS
            fds[n++] = fd;
        } else {
            close(fd);
        }
    }
    if (d)
        closedir(d);
    return n;
}

static double read_double(const char *path)
{
    double v = -1;
    FILE *f = fopen(path, "r");
    if (f) {
        if (fscanf(f, "%lf", &v) != 1)
            v = -1;
        fclose(f);
    }
    return v;
}

static int just_resumed(void)
{
    double resumed = read_double(RESUME_FILE);
    return resumed >= 0 && read_double("/proc/uptime") - resumed < RESUME_GUARD;
}

/* One sway IPC request (type: 0 RUN_COMMAND, 3 GET_OUTPUTS); the reply's JSON goes into out
 * (NUL-terminated, cut at size). Returns 0 if sway couldn't be asked. */
static int sway_ipc(uint32_t type, const char *payload, char *out, size_t size)
{
    const char *sock = getenv("SWAYSOCK");
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    uint32_t len = strlen(payload);
    int fd, ok = 0;
    if (!sock || strlen(sock) >= sizeof addr.sun_path || len > 1024)
        return 0;
    strcpy(addr.sun_path, sock);
    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return 0;
    char req[14 + 1024] = "i3-ipc";
    memcpy(req + 6, &len, 4);
    memcpy(req + 10, &type, 4);
    memcpy(req + 14, payload, len);
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) == 0 && write(fd, req, 14 + len) == 14 + (ssize_t)len) {
        char reply[16384];
        size_t got = 0;
        ssize_t n;
        while (got < sizeof reply - 1 && (n = read(fd, reply + got, sizeof reply - 1 - got)) > 0) {
            got += n;
            if (got >= 14) {
                memcpy(&len, reply + 6, 4);
                if (got >= 14 + (size_t)len)
                    break;
            }
        }
        reply[got] = 0;
        if (got > 14) {
            snprintf(out, size, "%s", reply + 14);
            ok = 1;
        }
    }
    close(fd);
    return ok;
}

/* Ask sway whether an output is powered off. Unknown counts as on. */
static int screen_dark(void)
{
    char r[16384];
    return sway_ipc(3, "", r, sizeof r) && (strstr(r, "\"power\": false") || strstr(r, "\"power\":false"));
}

static int open_pad(void)
{
    return open_named(PAD_NAME);
}

/* pid of shelf (0 if not running), and whether it has a child process (a game). */
static pid_t frontend(int *game_running)
{
    DIR *d = opendir("/proc");
    struct dirent *e;
    pid_t pid = 0;
    int ppids[4096], n = 0;
    *game_running = 0;
    while (d && (e = readdir(d)) && n < 4096) {
        char path[64], stat[512], *comm_end;
        int p = atoi(e->d_name), ppid;
        if (p <= 0)
            continue;
        snprintf(path, sizeof path, "/proc/%d/stat", p);
        FILE *f = fopen(path, "r");
        if (!f)
            continue;
        size_t len = fread(stat, 1, sizeof stat - 1, f);
        fclose(f);
        stat[len] = 0;
        /* "pid (comm) state ppid ...": comm may contain spaces, so find the last ')' */
        comm_end = strrchr(stat, ')');
        if (!comm_end || sscanf(comm_end + 2, "%*c %d", &ppid) != 1)
            continue;
        const char *comm = strchr(stat, '(') + 1;
        if (strncmp(comm, "shelf)", 6) == 0)
            pid = p;
        ppids[n++] = ppid;
    }
    if (d)
        closedir(d);
    for (int i = 0; pid && i < n; i++)
        if (ppids[i] == pid)
            *game_running = 1;
    return pid;
}

static void swaymsg(const char *command)
{
    char *argv[] = { "swaymsg", (char *)command, NULL };
    pid_t pid;
    if (posix_spawnp(&pid, "swaymsg", NULL, NULL, argv, environ) != 0)
        perror("inputd: swaymsg");
}

static int charger_online(void)
{
    char c = '0';
    int fd = open(CHARGER_ONLINE, O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        if (read(fd, &c, 1) != 1)
            c = '0';
        close(fd);
    }
    return c == '1';
}

/* Kernel uevents (power_supply: charger plugged in/out). */
static int uevent_socket(void)
{
    struct sockaddr_nl addr = { .nl_family = AF_NETLINK, .nl_groups = 1 };
    int fd = socket(AF_NETLINK, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, NETLINK_KOBJECT_UEVENT);
    if (fd >= 0 && bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        close(fd);
        fd = -1;
    }
    return fd;
}

static time_t mono_now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t); /* stops during suspend: idle is awake time */
    return t.tv_sec;
}

/* Seconds spent asleep since boot (BOOTTIME counts suspend, MONOTONIC doesn't). */
static double slept(void)
{
    struct timespec b, m;
    clock_gettime(CLOCK_BOOTTIME, &b);
    clock_gettime(CLOCK_MONOTONIC, &m);
    return (b.tv_sec - m.tv_sec) + (b.tv_nsec - m.tv_nsec) / 1e9;
}

static void suspend(void)
{
    char *argv[] = { "sudo", "-n", "systemctl", "suspend-then-hibernate", NULL };
    pid_t pid;
    if (posix_spawnp(&pid, "sudo", NULL, NULL, argv, environ) != 0)
        perror("inputd: sudo");
}

/* Run a shell command and wait for it (the steps must happen in order), at most STEP_TIMEOUT s
 * so a stuck one (swaymsg while sway hangs) can't stop the lid and power button from working.
 * Returns whether it succeeded (exit status 0); reconcile records a step as applied only then. */
static int sh_wait(const char *command)
{
    char *argv[] = { "timeout", STEP_TIMEOUT, "sh", "-c", (char *)command, NULL };
    pid_t pid;
    int status;
    if (posix_spawnp(&pid, "timeout", NULL, NULL, argv, environ) != 0) {
        perror("inputd: timeout");
        return 0;
    }
    while (waitpid(pid, &status, 0) < 0)
        if (errno != EINTR)
            return 0;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "inputd: failed (%s): %s\n",
                WIFEXITED(status) && WEXITSTATUS(status) == 124 ? "timed out" : "exit status", command);
        return 0;
    }
    return 1;
}

/* Collect finished background commands (swaymsg, sudo), so they don't linger as zombies. Only
 * reaps those: sh_wait waits for its own child itself. */
static void reap(void)
{
    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
}

static int pad_fd = -1;

/* ---- state: facts (set by events, or read by reconcile where they can be read) and what is
 * applied (changed only by reconcile). reconcile() works out from the facts what should be, and
 * changes only what differs, so the order and number of events can't leave it in a wrong state. */

static struct {
    int lid_closed;
    int on_ac;
    time_t last_activity;    /* MONOTONIC s: pad, lid opened, power button, charger, wake, sway */
    int sway_idle;           /* sway: no keyboard/pointer/touch for DIM_AFTER s, no inhibitor */
    time_t sway_idle_since;
    time_t lid_closed_at;
} fact;

static struct {
    int lid;                 /* input off, pad grabbed, CPU/GPU throttled (lid.sh) */
    int screen_off;          /* screen.sh off (screen off, shelf and the game frozen) */
    int dim;                 /* 0 normal, 1 DIM_PERCENT, 2 lowest visible level */
    time_t sleep_sent;       /* when suspend was last requested in this idle period (0: not) */
} applied;

static int lid_fd = -1;
static int charger_stale = 1; /* re-read the charger (16 ms over I2C, so not on every pass) */
static double was_asleep;     /* slept() at the last pass: when it grows, the device woke */
static time_t retry_after;    /* a step failed: don't apply again before this (0: no wait) */

static time_t next_due;      /* when the next idle step is due (0: none; sway says when idle starts) */

static void activity(void)
{
    fact.last_activity = mono_now();
}

static time_t max_t(time_t a, time_t b) { return a > b ? a : b; }

/* Seconds idle now; -1 while active (sway hasn't reported idle and the lid is open). While the lid
 * is closed, idle runs from the close and ignores inhibitors (the game is frozen). */
static long idle_seconds(time_t now)
{
    if (fact.lid_closed)
        return now - max_t(fact.lid_closed_at, fact.last_activity);
    if (!fact.sway_idle)
        return -1;
    return now - max_t(fact.sway_idle_since, fact.last_activity);
}

/* The brightness before dimming lives in $XDG_RUNTIME_DIR/BRIGHTNESS_SAVED (raw backlight value)
 * while dimmed, so a restarted inputd knows it is dimmed and what to go back to. */
static char *saved_path(void)
{
    static char p[256];
    const char *rt = getenv("XDG_RUNTIME_DIR");
    snprintf(p, sizeof p, "%s/" BRIGHTNESS_SAVED, rt ? rt : "/tmp");
    return p;
}

static int set_dim(int from, int to)
{
    char cmd[512];
    const char *f = saved_path();
    /* leaving normal: save the current level; the dim levels are DIM_PERCENT of the saved one (min 1%)
     * and the lowest visible level (raw 1; 0 is backlight off) */
    const char *save = from ? "" : "b=$(brightnessctl -m) && echo \"$b\" | cut -d, -f3 > \"$f\" && ";
    if (to == 0)
        snprintf(cmd, sizeof cmd, "f='%s'; { [ ! -s \"$f\" ] || brightnessctl -q set \"$(cat \"$f\")\"; } && rm -f \"$f\"", f);
    else if (to == 1)
        snprintf(cmd, sizeof cmd, "f='%s'; %sv=$(cat \"$f\") && m=$(brightnessctl -m | cut -d, -f5) && "
                 "p=$(( v * %d / m )) && brightnessctl -q set \"$(( p > 1 ? p : 1 ))%%\"", f, save, DIM_PERCENT);
    else
        snprintf(cmd, sizeof cmd, "f='%s'; %sbrightnessctl -q set 1", f, save); /* lowest visible */
    return sh_wait(cmd);
}

/* The pad is grabbed while the lid is closed, so nobody else (games, sway) sees its events. */
static void grab_pad(void)
{
    if (pad_fd >= 0)
        ioctl(pad_fd, EVIOCGRAB, applied.lid == 1);
}

/* The lid switch, as a fact. */
static void lid_fact(int closed)
{
    if (closed && !fact.lid_closed)
        fact.lid_closed_at = mono_now();
    if (!closed && fact.lid_closed)
        activity(); /* opening counts as activity */
    fact.lid_closed = closed;
}

static void reconcile(void)
{
    /* facts that can be read are read, not remembered from events: the lid switch every time
     * (cheap), the charger whenever a power_supply uevent came or uevents were lost, and whether
     * the device slept since the last pass (waking counts as activity) */
    double asleep = slept();
    if (asleep - was_asleep > 2)
        activity();
    was_asleep = asleep;
    if (charger_stale) {
        int ac = charger_online();
        if (ac != fact.on_ac)
            activity(); /* plugged in or out */
        fact.on_ac = ac;
        charger_stale = 0;
    }
    unsigned long sw[1] = { 0 };
    if (lid_fd >= 0 && ioctl(lid_fd, EVIOCGSW(sizeof sw), sw) >= 0)
        lid_fact(sw[0] >> SW_LID & 1);

    time_t now = mono_now();
    long t = idle_seconds(now);
    int low_after = fact.on_ac ? LOW_AFTER_AC : LOW_AFTER_BATTERY;
    int off_after = fact.on_ac ? OFF_AFTER_AC : OFF_AFTER_BATTERY;
    int sleep_after = fact.on_ac ? SLEEP_AFTER_AC : SLEEP_AFTER_BATTERY;
#define REACHED(after) ((after) > 0 && t >= (after))

    /* what should be */
    int lid = fact.lid_closed;
    int screen_off = fact.lid_closed || REACHED(off_after);
    int dim = fact.lid_closed ? applied.dim /* dark anyway: left as it is */
            : REACHED(low_after) ? 2 : REACHED(DIM_AFTER) ? 1 : 0;

    /* apply the differences: screen on first and off last, so a waking screen is lit. A step is
     * recorded as applied only if it succeeded; otherwise it is tried again from STEP_RETRY s on
     * (all steps are safe to repeat). */
    int failed = 0;
    if (now >= retry_after) {
        if (lid != applied.lid && !lid) {
            applied.lid = 0; /* ungrab the pad whatever happens: input must come back */
            grab_pad();
            if (!sh_wait("swaymsg -q 'input * events enabled' && ~/.config/sway/lid.sh open"))
                applied.lid = -1, failed = 1; /* unknown: applied again */
        }
        if (screen_off != applied.screen_off && !screen_off) {
            if (sh_wait("~/.config/sway/screen.sh on"))
                applied.screen_off = 0;
            else
                failed = 1;
        }
        if (dim != applied.dim) {
            if (set_dim(applied.dim, dim))
                applied.dim = dim;
            else
                failed = 1;
        }
        if (lid != applied.lid && lid) {
            applied.lid = 1;
            grab_pad();
            if (!sh_wait("swaymsg -q 'input * events disabled' && ~/.config/sway/lid.sh close"))
                applied.lid = -1, failed = 1;
        }
        if (screen_off != applied.screen_off && screen_off) {
            if (sh_wait("~/.config/sway/screen.sh off"))
                applied.screen_off = 1;
            else
                failed = 1;
        }
        retry_after = failed ? now + STEP_RETRY : 0;
    }

    /* suspend: requested once, again every SLEEP_RETRY s while still awake (it failed); a wake
     * counts as activity and ends the idle period. MONOTONIC stops during sleep, so a successful
     * suspend never looks like a retry is due. */
    if (!REACHED(sleep_after))
        applied.sleep_sent = 0;
    else if (!applied.sleep_sent || now - applied.sleep_sent >= SLEEP_RETRY) {
        applied.sleep_sent = now;
        suspend();
    }

    /* when to look again: the first step not reached yet, while idle time runs */
    next_due = 0;
    if (t >= 0) {
        int steps[] = { DIM_AFTER, low_after, off_after, sleep_after };
        for (int i = 0; i < 4; i++)
            if (steps[i] > 0 && steps[i] > t && (!next_due || now + steps[i] - t < next_due))
                next_due = now + steps[i] - t;
    }
    if (applied.sleep_sent && (!next_due || applied.sleep_sent + SLEEP_RETRY < next_due))
        next_due = applied.sleep_sent + SLEEP_RETRY;
    if (retry_after && (!next_due || retry_after < next_due))
        next_due = retry_after;
}

/* sway's idle notification: no input for DIM_AFTER s (and no idle inhibitor), and back. */
static void idled(void *data, struct ext_idle_notification_v1 *n)
{
    (void)data, (void)n;
    fact.sway_idle = 1;
    fact.sway_idle_since = mono_now() - DIM_AFTER;
}
static void resumed(void *data, struct ext_idle_notification_v1 *n)
{
    (void)data, (void)n;
    fact.sway_idle = 0;
    activity();
}
static const struct ext_idle_notification_v1_listener idle_listener = { idled, resumed };

static struct wl_seat *wl_seat;
static struct ext_idle_notifier_v1 *notifier;

static void reg_global(void *d, struct wl_registry *r, uint32_t name, const char *iface, uint32_t ver)
{
    (void)d, (void)ver;
    if (!strcmp(iface, wl_seat_interface.name) && !wl_seat)
        wl_seat = wl_registry_bind(r, name, &wl_seat_interface, 1);
    else if (!strcmp(iface, ext_idle_notifier_v1_interface.name))
        notifier = wl_registry_bind(r, name, &ext_idle_notifier_v1_interface, 1);
}
static void reg_remove(void *d, struct wl_registry *r, uint32_t name) { (void)d, (void)r, (void)name; }
static const struct wl_registry_listener reg_listener = { reg_global, reg_remove };

/* Connect to sway and ask to be told about idle. NULL if that isn't possible. */
static struct wl_display *idle_connect(void)
{
    struct wl_display *dpy = wl_display_connect(NULL);
    if (!dpy)
        return NULL;
    wl_registry_add_listener(wl_display_get_registry(dpy), &reg_listener, NULL);
    wl_display_roundtrip(dpy);
    if (!wl_seat || !notifier) {
        fprintf(stderr, "inputd: sway has no idle notifier\n");
        wl_display_disconnect(dpy);
        return NULL;
    }
    struct ext_idle_notification_v1 *n = ext_idle_notifier_v1_get_idle_notification(notifier, DIM_AFTER * 1000, wl_seat);
    ext_idle_notification_v1_add_listener(n, &idle_listener, NULL);
    wl_display_flush(dpy);
    return dpy;
}

/* Menu key: start shelf, or switch to it, or close it if it has the focus (sway asks it to close;
 * it saves its place and exits, and run doesn't restart it). Nothing while a game runs. */
static void on_menu(void)
{
    int game;
    char r[256];
    if (!frontend(&game))
        swaymsg("workspace number 1; exec ~/.config/shelf/run");
    else if (game)
        return;
    /* workspace=__focused__ too: on an empty workspace no window has the focus, and sway then
     * ignores con_id=__focused__ (it would close shelf on workspace 1) */
    else if (sway_ipc(0, "[app_id=\"^shelf$\" workspace=__focused__ con_id=__focused__] kill", r, sizeof r) &&
             (strstr(r, "\"success\": true") || strstr(r, "\"success\":true")))
        return; /* it had the focus: closing */
    else
        swaymsg("workspace number 1");
}

int main(void)
{

    int watch = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    /* IN_ATTRIB: udev sets the node's permissions just after the kernel creates it */
    inotify_add_watch(watch, "/dev/input", IN_CREATE | IN_ATTRIB);

    struct wl_display *dpy = idle_connect();
    if (!dpy)
        fprintf(stderr, "inputd: no idle handling (no Wayland connection)\n");
    enum { F_INOTIFY, F_PAD, F_LID, F_UEVENT, F_WAYLAND, F_POWER }; /* F_POWER...: power/Menu keys */
    struct pollfd fds[F_POWER + MAX_POWER] = {
        [F_INOTIFY] = { .fd = watch, .events = POLLIN },
        [F_PAD] = { .fd = (pad_fd = open_pad()), .events = POLLIN },
        [F_LID] = { .fd = (lid_fd = open_named(LID_NAME)), .events = POLLIN },
        [F_UEVENT] = { .fd = uevent_socket(), .events = POLLIN },
        [F_WAYLAND] = { .fd = dpy ? wl_display_get_fd(dpy) : -1, .events = POLLIN },
    };
    fact.on_ac = charger_online();
    charger_stale = 0;
    activity();
    /* applied state unknown at start (inputd may have died with the lid closed): the first
     * reconcile applies lid and screen either way, both are safe to repeat */
    applied.lid = applied.screen_off = -1;
    if (access(saved_path(), F_OK) == 0)
        applied.dim = 2; /* a previous inputd was dimmed: restore from the saved level */
    was_asleep = slept();
    time_t last_power = 0;
    int press_dark = -1; /* screen state when the power button went down; -1: not down */
    int power[MAX_POWER], npower = open_power(power, MAX_POWER), nfds = F_POWER + npower;
    for (int i = 0; i < npower; i++)
        fds[F_POWER + i] = (struct pollfd){ .fd = power[i], .events = POLLIN };

    for (;;) {
        reap();
        reconcile();
        int timeout = -1;
        if (next_due)
            timeout = next_due > mono_now() ? (int)(next_due - mono_now()) * 1000 : 0;
        if (dpy) {
            while (wl_display_prepare_read(dpy) != 0)
                wl_display_dispatch_pending(dpy);
            wl_display_flush(dpy);
        }
        int r = poll(fds, nfds, timeout);
        if (dpy) {
            if (r > 0 && fds[F_WAYLAND].revents & POLLIN)
                wl_display_read_events(dpy);
            else
                wl_display_cancel_read(dpy);
            if (wl_display_dispatch_pending(dpy) < 0 || (r > 0 && fds[F_WAYLAND].revents & (POLLERR | POLLHUP))) {
                fprintf(stderr, "inputd: Wayland connection lost, exiting\n");
                return 1;
            }
        }
        if (r < 0)
            continue;

        if (fds[F_UEVENT].fd >= 0 && fds[F_UEVENT].revents) {
            char buf[8192];
            ssize_t n;
            while ((n = recv(fds[F_UEVENT].fd, buf, sizeof buf - 1, 0)) > 0) {
                buf[n] = 0;
                if (strstr(buf, "/power_supply/")) /* "change@/devices/.../power_supply/..." */
                    charger_stale = 1;
            }
            if (n < 0 && errno == ENOBUFS) /* the queue overflowed: events were lost */
                charger_stale = 1;
        }

        /* the pad first, then /dev/input: a pad reset (USB reset after a wake) removes its node and
         * creates one with the same name at once, and both can arrive in one wake; the other order
         * would see the new node while the old one is still open and then never look again */
        if (fds[F_PAD].fd >= 0 && fds[F_PAD].revents) {
            struct input_event ev;
            ssize_t n;
            while ((n = read(fds[F_PAD].fd, &ev, sizeof ev)) == sizeof ev) {
                if (!fact.lid_closed && (ev.type == EV_KEY || ev.type == EV_ABS))
                    activity(); /* sway doesn't count the pad */
            }
            if (n < 0 && errno != EAGAIN) { /* ENODEV: unplugged, screen off, resume, reset */
                close(fds[F_PAD].fd);
                fds[F_PAD].fd = pad_fd = -1;
            }
        }

        if (fds[F_INOTIFY].revents) {
            char buf[4096];
            while (read(watch, buf, sizeof buf) > 0)
                ;
        }
        if (fds[F_PAD].fd < 0 && (fds[F_INOTIFY].revents || fds[F_PAD].revents)) {
            fds[F_PAD].fd = pad_fd = open_pad(); /* back already, or when inotify reports it */
            grab_pad();
        }

        if (fds[F_LID].fd >= 0 && fds[F_LID].revents) {
            struct input_event ev;
            while (read(fds[F_LID].fd, &ev, sizeof ev) == sizeof ev)
                ; /* reconcile reads the switch state; the event only wakes the loop */
        }

        for (int i = F_POWER; i < nfds; i++) {
            struct input_event ev;
            ssize_t n;
            if (!fds[i].revents)
                continue;
            while ((n = read(fds[i].fd, &ev, sizeof ev)) == sizeof ev) {
                if (ev.type == EV_KEY && ev.code == KEY_COMPOSE) { /* the Menu key */
                    if (ev.value == 1 && !fact.lid_closed)
                        on_menu();
                    continue;
                }
                if (ev.type != EV_KEY || ev.code != KEY_POWER)
                    continue;
                /* Screen state at the press: sway counts the press as activity, so the screen
                 * may be on again by the release. */
                if (ev.value == 1 && press_dark < 0)
                    press_dark = screen_dark();
                if (ev.value != 0)
                    continue;
                int dark = press_dark >= 0 ? press_dark : screen_dark();
                press_dark = -1;
                if (fact.lid_closed || just_resumed())
                    continue;
                /* several devices may report the same press: act once */
                if (last_power && mono_now() - last_power < 2)
                    continue;
                last_power = mono_now();
                if (dark) /* wake the screen (reconcile turns it on) */
                    activity();
                else
                    suspend();
            }
            if (n < 0 && errno != EAGAIN) { /* device gone (USB keyboard re-enumerated) */
                close(fds[i].fd);
                fds[i].fd = -1; /* poll ignores it */
            }
        }
    }
}
