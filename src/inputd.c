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
 * Idle (docs/power.md). Idle time runs from the last activity:
 *   2 min        brightness to a third (min 1%; the level before is saved and restored)
 *   3 min        1% (on AC: 10 min)
 *   5 min        screen off (screen.sh: also freezes shelf and the game), on battery only
 *   30 min       suspend-then-hibernate, on battery only
 * Activity: keyboard, pointer and touch (sway's ext-idle-notify: told when nothing happened for
 * 2 min, which also honours idle inhibitors, so games that inhibit idle keep it awake), the gamepad
 * (sway doesn't count it), opening the lid, the power button, plugging the charger in or out, and
 * waking from sleep. Activity undoes the steps (brightness back, screen on). The charger is part of
 * the decision at every moment, so unplugging after a long idle on AC starts the battery countdown
 * from the unplug (it used to be checked only when a step fired, once per idle period).
 *
 * State, not events: events only update facts (lid closed, on AC, last activity, sway idle and
 * since when, when the lid closed). reconcile() works out from the facts what should be (input
 * off, screen off, brightness level, throttled, suspend) and changes only what differs from what
 * it last applied, so a missed, repeated or reordered event can't leave a wrong state. At start the
 * applied lid and screen state count as unknown and are applied either way (both are safe to
 * repeat), so restarting inputd also repairs what a previous one left behind.
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
 * Built and installed by `scripts/sync install` (manifest "build" entry); started by sway
 * (dotfiles/sway/autostart). Without a Wayland connection (sway gone) it exits.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/netlink.h>
#include <sys/inotify.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include "vendor/ext-idle-notify-v1-client-protocol.h"
#include "vendor/ext-idle-notify-v1-protocol.c"

#define PAD_NAME "Microsoft X-Box 360 pad"
#define LID_NAME "Lid Switch"
/* Idle steps, seconds since the last activity */
#define DIM_AFTER 120          /* a third of the brightness; also sway's idle notification */
#define LOW_AFTER_BATTERY 180  /* 1% */
#define LOW_AFTER_AC 600
#define OFF_AFTER 300          /* screen off, battery only */
#define SLEEP_AFTER 1800       /* suspend-then-hibernate, battery only */
#define RESUME_GUARD 3.0 /* seconds after a resume in which the power button is ignored */
#define RESUME_FILE "/run/resume-time"
#define CHARGER_ONLINE "/sys/class/power_supply/bq24190-charger/online"
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

/* Run a shell command and wait for it (the idle steps must happen in order). */
static void sh_wait(const char *command)
{
    char *argv[] = { "sh", "-c", (char *)command, NULL };
    pid_t pid;
    if (posix_spawnp(&pid, "sh", NULL, NULL, argv, environ) != 0)
        perror("inputd: sh");
    else
        waitpid(pid, NULL, 0); /* SIGCHLD is ignored: returns once it exited */
}

static int pad_fd = -1;

/* ---- state: facts (updated by events, nothing else) and what is applied (changed only by
 * reconcile). reconcile() works out from the facts what should be, and changes only what differs,
 * so the order and number of events can't leave it in a wrong state. */

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
    int dim;                 /* 0 normal, 1 a third, 2 1% */
    int sleep_sent;          /* suspend requested in this idle period */
} applied;

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

/* The pad is grabbed while the lid is closed, so nobody else (games, sway) sees its events. */
static void grab_pad(void)
{
    if (pad_fd >= 0)
        ioctl(pad_fd, EVIOCGRAB, applied.lid);
}

static void reconcile(void)
{
    time_t now = mono_now();
    long t = idle_seconds(now);
    int low_after = fact.on_ac ? LOW_AFTER_AC : LOW_AFTER_BATTERY;

    /* what should be */
    int lid = fact.lid_closed;
    int screen_off = fact.lid_closed || (!fact.on_ac && t >= OFF_AFTER);
    int dim = fact.lid_closed ? applied.dim /* dark anyway: left as it is */
            : t >= low_after ? 2 : t >= DIM_AFTER ? 1 : 0;

    /* apply the differences: screen on first and off last, so a waking screen is lit */
    if (lid != applied.lid && !lid) {
        swaymsg("input * events enabled");
        applied.lid = 0;
        grab_pad();
        sh_wait("~/.config/sway/lid.sh open");
    }
    if (screen_off != applied.screen_off && !screen_off) {
        sh_wait("~/.config/sway/screen.sh on");
        applied.screen_off = 0;
    }
    if (dim != applied.dim) {
        char cmd[256];
        /* save the brightness when leaving normal, else restore it first (a third of the
         * original, not of the dimmed level) */
        const char *base = applied.dim ? "brightnessctl -q -r" : "brightnessctl -q -s";
        if (dim == 0)
            snprintf(cmd, sizeof cmd, "brightnessctl -q -r");
        else if (dim == 1)
            snprintf(cmd, sizeof cmd, "%s; c=$(brightnessctl -m | cut -d, -f4 | tr -d %%); "
                     "brightnessctl -q set \"$(( c / 3 > 1 ? c / 3 : 1 ))%%\"", base);
        else
            snprintf(cmd, sizeof cmd, "%s; brightnessctl -q set 1", base);
        sh_wait(cmd);
        applied.dim = dim;
    }
    if (lid != applied.lid && lid) {
        swaymsg("input * events disabled");
        applied.lid = 1;
        grab_pad();
        sh_wait("~/.config/sway/lid.sh close");
    }
    if (screen_off != applied.screen_off && screen_off) {
        sh_wait("~/.config/sway/screen.sh off");
        applied.screen_off = 1;
    }

    /* suspend: once per idle period (a wake counts as activity and starts a new one) */
    if (t < SLEEP_AFTER)
        applied.sleep_sent = 0;
    else if (!fact.on_ac && !applied.sleep_sent) {
        applied.sleep_sent = 1;
        suspend();
    }

    /* when to look again: the first step not reached yet, while idle time runs */
    next_due = 0;
    if (t >= 0) {
        int steps[] = { DIM_AFTER, low_after, OFF_AFTER, SLEEP_AFTER };
        for (int i = 0; i < 4; i++)
            if (steps[i] > t && (!next_due || now + steps[i] - t < next_due))
                next_due = now + steps[i] - t;
    }
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

/* The lid switch, as a fact. */
static void lid_fact(int closed)
{
    if (closed && !fact.lid_closed)
        fact.lid_closed_at = mono_now();
    if (!closed && fact.lid_closed)
        activity(); /* opening counts as activity */
    fact.lid_closed = closed;
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
    signal(SIGCHLD, SIG_IGN); /* reap swaymsg automatically */

    int watch = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    /* IN_ATTRIB: udev sets the node's permissions just after the kernel creates it */
    inotify_add_watch(watch, "/dev/input", IN_CREATE | IN_ATTRIB);

    struct wl_display *dpy = idle_connect();
    if (!dpy)
        fprintf(stderr, "inputd: no idle handling (no Wayland connection)\n");
    struct pollfd fds[5 + MAX_POWER] = {
        { .fd = watch, .events = POLLIN },
        { .fd = (pad_fd = open_pad()), .events = POLLIN },
        { .fd = open_named(LID_NAME), .events = POLLIN },
        { .fd = uevent_socket(), .events = POLLIN },
        { .fd = dpy ? wl_display_get_fd(dpy) : -1, .events = POLLIN },
    };
    fact.on_ac = charger_online();
    activity();
    /* applied state unknown at start (inputd may have died with the lid closed): the first
     * reconcile applies lid and screen either way, both are safe to repeat */
    applied.lid = applied.screen_off = -1;
    double was_asleep = slept();
    time_t last_power = 0;
    int press_dark = -1; /* screen state when the power button went down; -1: not down */
    int power[MAX_POWER], npower = open_power(power, MAX_POWER), nfds = 5 + npower;
    for (int i = 0; i < npower; i++)
        fds[5 + i] = (struct pollfd){ .fd = power[i], .events = POLLIN };

    unsigned long sw[1] = { 0 };
    if (fds[2].fd >= 0 && ioctl(fds[2].fd, EVIOCGSW(sizeof sw), sw) >= 0)
        lid_fact(sw[0] >> SW_LID & 1); /* started with the lid closed: reconcile applies it */

    for (;;) {
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
            if (r > 0 && fds[4].revents & POLLIN)
                wl_display_read_events(dpy);
            else
                wl_display_cancel_read(dpy);
            if (wl_display_dispatch_pending(dpy) < 0 || (r > 0 && fds[4].revents & (POLLERR | POLLHUP))) {
                fprintf(stderr, "inputd: Wayland connection lost, exiting\n");
                return 1;
            }
        }
        if (r < 0)
            continue;

        double asleep = slept();
        if (asleep - was_asleep > 2) /* woke from sleep */
            activity();
        was_asleep = asleep;

        if (fds[3].fd >= 0 && fds[3].revents) {
            char buf[8192];
            ssize_t n;
            int power_event = 0;
            while ((n = recv(fds[3].fd, buf, sizeof buf - 1, 0)) > 0) {
                buf[n] = 0;
                power_event |= strstr(buf, "/power_supply/") != NULL;
            }
            int ac = power_event ? charger_online() : fact.on_ac;
            if (ac != fact.on_ac) /* plugged in or out: counts as activity */
                activity();
            fact.on_ac = ac;
        }

        if (fds[0].revents) {
            char buf[4096];
            while (read(watch, buf, sizeof buf) > 0)
                ;
            if (fds[1].fd < 0) {
                fds[1].fd = pad_fd = open_pad();
                grab_pad();
            }
        }

        if (fds[1].fd >= 0 && fds[1].revents) {
            struct input_event ev;
            ssize_t n;
            while ((n = read(fds[1].fd, &ev, sizeof ev)) == sizeof ev) {
                if (!fact.lid_closed && (ev.type == EV_KEY || ev.type == EV_ABS))
                    activity(); /* sway doesn't count the pad */
            }
            if (n < 0 && errno != EAGAIN) { /* ENODEV once the pad is unplugged */
                close(fds[1].fd); /* pad went away; inotify brings it back */
                fds[1].fd = pad_fd = -1;
            }
        }

        if (fds[2].fd >= 0 && fds[2].revents) {
            struct input_event ev;
            while (read(fds[2].fd, &ev, sizeof ev) == sizeof ev)
                if (ev.type == EV_SW && ev.code == SW_LID)
                    lid_fact(ev.value);
        }

        for (int i = 5; i < nfds; i++) {
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
                struct timespec t;
                clock_gettime(CLOCK_MONOTONIC, &t);
                if (last_power && t.tv_sec - last_power < 2)
                    continue;
                last_power = t.tv_sec;
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
